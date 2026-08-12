#!/usr/bin/env python3
"""dll-disasm.py -- disassemble one handler out of RIPSCRIP.DLL by RVA.

No decompiler is available in this environment, so handler semantics are
recovered by reading the prologue's argument fetches.  The driver keeps a
decoded-argument array and each handler indexes it; the ORDER of those
indexes against the ORDER of the GDI/helper call arguments is what
identifies each field.

Two annotations are applied, and the second matters more than it looks:

  - immediates that point at a readable string are shown inline.  This is
    how most handlers were named: they push their own name before calling
    the error reporter.

  - indirect calls through the import table are resolved to
    DLL!Function.  Handlers that reference NO strings are opaque to every
    string-based method, and several stayed unidentified for exactly that
    reason.  Naming what a handler CALLS gets past that: '|3D' was
    settled by seeing WINMM!timeGetTime in its callee, and the
    skewed-oval renderer was confirmed by GDI32!Polygon.

Usage:
    python scripts/dll-disasm.py <Ripscrip.dll> 0x01f904 [--count 90]
"""
import argparse
import struct
import sys

IB = 0x10000000  # preferred image base for this DLL


def load(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    coff = pe + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    optsz = struct.unpack_from("<H", d, coff + 16)[0]
    secs = []
    so = coff + 20 + optsz
    for i in range(nsec):
        o = so + i * 40
        nm = d[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vs, va, rs, rp = struct.unpack_from("<IIII", d, o + 8)
        secs.append(dict(n=nm, rva=va, vs=vs, raw=rp, rs=rs))
    return d, secs


def read_imports(d, secs, rva2off):
    """Map each import-address-table VA to 'DLL!Function'.

    A handler that pushes no strings is invisible to string mining, but its
    indirect calls still say what it does.  This is the map that makes
    'call dword ptr [0x100966fc]' read as 'WINMM.dll!timeGetTime'.
    """
    out = {}
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    opt = pe + 4 + 20
    imp_rva = struct.unpack_from("<I", d, opt + 104)[0]
    o = rva2off(imp_rva)
    if o is None:
        return out
    while True:
        oft, _ts, _fc, name_rva, ft = struct.unpack_from("<IIIII", d, o)
        if oft == 0 and name_rva == 0:
            break
        no = rva2off(name_rva)
        dll = d[no:d.find(b"\0", no)].decode("ascii", "replace") if no else "?"
        thunks = rva2off(oft or ft)
        i = 0
        while thunks is not None:
            t = struct.unpack_from("<I", d, thunks + 4 * i)[0]
            if t == 0:
                break
            va = IB + ft + 4 * i
            if t & 0x80000000:
                out[va] = f"{dll}!#{t & 0xFFFF}"
            else:
                q = rva2off(t)
                if q is not None:
                    q += 2                     # skip the ordinal hint
                    out[va] = f"{dll}!{d[q:d.find(chr(0).encode(), q)].decode('ascii', 'replace')}"
            i += 1
        o += 20
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll")
    ap.add_argument("rva", help="handler RVA, e.g. 0x01f904")
    ap.add_argument("-c", "--count", type=int, default=80,
                    help="max instructions to print (default 80)")
    args = ap.parse_args()

    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    except ImportError:
        sys.exit("capstone required:  pip install capstone")

    d, secs = load(args.dll)

    def rva2off(r):
        for s in secs:
            if s["rva"] <= r < s["rva"] + max(s["vs"], s["rs"]):
                return r - s["rva"] + s["raw"]
        return None

    def read_cstr(va):
        o = rva2off(va - IB)
        if o is None:
            return None
        end = d.find(b"\0", o)
        if end < 0 or end - o > 120:
            return None
        try:
            return d[o:end].decode("ascii")
        except UnicodeDecodeError:
            return None

    imports = read_imports(d, secs, rva2off)

    rva = int(args.rva, 0)
    off = rva2off(rva)
    if off is None:
        sys.exit(f"RVA {rva:#x} is not inside any section")

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    code = d[off:off + args.count * 16]

    print(f"; handler at RVA {rva:#08x}  (file offset {off:#x})")
    print(f"; {args.dll}\n")

    n = 0
    for ins in md.disasm(code, IB + rva):
        note = ""
        # an indirect call through the IAT names the API being invoked
        if ins.mnemonic == "call" and ins.op_str.startswith("dword ptr ["):
            t = ins.op_str[len("dword ptr ["):-1]
            if t.startswith("0x"):
                api = imports.get(int(t, 16))
                if api:
                    note = f"        ; {api}"
        # otherwise annotate immediates that point at a readable string
        if not note:
            for tok in ins.op_str.replace(",", " ").split():
                t = tok.strip("[]")
                if t.startswith("0x") and len(t) >= 8:
                    s = read_cstr(int(t, 16))
                    if s and len(s) > 3 and s.isprintable():
                        note = f"        ; \"{s}\""
                        break
        print(f"  {ins.address - IB:#08x}  {ins.mnemonic:<7} {ins.op_str}{note}")
        n += 1
        if ins.mnemonic == "ret" or n >= args.count:
            break


if __name__ == "__main__":
    main()
