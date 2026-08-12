#!/usr/bin/env python3
"""dll-handler-imports.py -- characterise every dispatch handler by the Win32
APIs it reaches.

Three of RIPlib's evidence classes (internal name strings, assertion strings,
field diagnostics) all key on STRINGS, so a handler that pushes none is
invisible to all three at once.  Several commands stayed unidentified for
exactly that reason.

This pass asks a different question: not what a handler SAYS, but what it
CALLS.  For each dispatch entry it walks the handler and its direct callees
to a bounded depth and reports the imported APIs reached.  A handler that
reaches GDI32!Polygon draws; one that reaches WINMM!timeGetTime waits; one
that reaches USER32!FillRect erases.  That is often enough to classify a
command whose name was never recoverable.

Usage:
    python scripts/dll-handler-imports.py <Ripscrip.dll> [--depth 2]
"""
import argparse
import struct
import sys
from pathlib import Path

IB = 0x10000000
TABLE_RVA = 0x080820
ENTRY_SIZE = 40
ENTRY_COUNT = 129

# APIs that say little about what a command MEANS.  Reporting them for every
# handler buries the ones that carry signal.  The bulk of these come from the
# lock/unlock, caret-hide and dirty-rect scaffolding that brackets nearly
# every handler, so they appear almost everywhere and discriminate nothing.
NOISE = {
    "KERNEL32.dll!EnterCriticalSection", "KERNEL32.dll!LeaveCriticalSection",
    "KERNEL32.dll!GetLastError", "KERNEL32.dll!SetLastError",
    "KERNEL32.dll!HeapAlloc", "KERNEL32.dll!HeapFree",
    "KERNEL32.dll!GlobalLock", "KERNEL32.dll!GlobalUnlock",
    "KERNEL32.dll!GlobalFlags", "KERNEL32.dll!GlobalFree",
    "USER32.dll!DrawFocusRect", "USER32.dll!InflateRect",
    "USER32.dll!HideCaret", "USER32.dll!ShowCaret", "USER32.dll!SetCaretPos",
    "USER32.dll!MessageBoxA",          # the error reporter, on every path
    # the offscreen-DC dance every drawing command performs
    "GDI32.dll!BitBlt", "GDI32.dll!CreateCompatibleBitmap",
    "GDI32.dll!CreateCompatibleDC", "GDI32.dll!DeleteDC",
    "GDI32.dll!DeleteObject", "GDI32.dll!SelectObject",
    "GDI32.dll!GetBkColor", "GDI32.dll!SetBkColor",
    "GDI32.dll!GetBkMode", "GDI32.dll!SetBkMode",
    "GDI32.dll!GetTextColor", "GDI32.dll!SetTextColor",
    "GDI32.dll!CreateRectRgn", "GDI32.dll!SelectClipRgn",
    "GDI32.dll!GetStockObject",
}


def load(path):
    d = Path(path).read_bytes()
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


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll")
    ap.add_argument("-d", "--depth", type=int, default=2,
                    help="how many call levels to follow (default 2)")
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

    # ---- import address table -> DLL!Function -----------------------------
    imports = {}
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    opt = pe + 4 + 20
    o = rva2off(struct.unpack_from("<I", d, opt + 104)[0])
    while o is not None:
        oft, _t, _f, name_rva, ft = struct.unpack_from("<IIIII", d, o)
        if oft == 0 and name_rva == 0:
            break
        no = rva2off(name_rva)
        dll = d[no:d.find(b"\0", no)].decode("ascii", "replace")
        th = rva2off(oft or ft)
        i = 0
        while th is not None:
            t = struct.unpack_from("<I", d, th + 4 * i)[0]
            if t == 0:
                break
            q = rva2off(t)
            if not (t & 0x80000000) and q is not None:
                nm = d[q + 2:d.find(b"\0", q + 2)].decode("ascii", "replace")
                imports[IB + ft + 4 * i] = f"{dll}!{nm}"
            i += 1
        o += 20

    text = next(s for s in secs if s["n"] == ".text")
    tlo, thi = text["rva"], text["rva"] + text["vs"]
    md = Cs(CS_ARCH_X86, CS_MODE_32)

    def scan(rva, depth, seen):
        """Return the set of imported APIs reachable from `rva`."""
        if depth < 0 or rva in seen or not (tlo <= rva < thi):
            return set()
        seen.add(rva)
        off = rva2off(rva)
        if off is None:
            return set()
        found, n = set(), 0
        for ins in md.disasm(d[off:off + 1200], IB + rva):
            if ins.mnemonic == "call":
                op = ins.op_str
                if op.startswith("dword ptr [0x"):
                    api = imports.get(int(op[len("dword ptr ["):-1], 16))
                    if api and api not in NOISE:
                        found.add(api)
                elif op.startswith("0x"):
                    found |= scan(int(op, 16) - IB, depth - 1, seen)
            n += 1
            if ins.mnemonic == "ret" or n > 300:
                break
        return found

    base = rva2off(TABLE_RVA)
    print(f"{'slot':>4} {'ltr':>4} {'handler':>10}  APIs reached (depth {args.depth})")
    for i in range(ENTRY_COUNT):
        raw = d[base + i * ENTRY_SIZE: base + (i + 1) * ENTRY_SIZE]
        h = struct.unpack_from("<I", raw, 1)[0] - IB
        L = raw[15]
        ch = chr(L) if 0x20 <= L < 0x7F else None
        if ch is None or not (tlo <= h < thi):
            continue
        apis = sorted(scan(h, args.depth, set()))
        if apis:
            print(f"{i:>4} {ch!r:>4} 0x{h:06x}  {', '.join(apis)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
