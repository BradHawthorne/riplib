#!/usr/bin/env python3
"""Recover per-command FIELD SEMANTICS from RIPSCRIP.DLL error strings.

Each command handler validates its arguments and, on failure, pushes a
human-readable diagnostic naming the field that was wrong -- e.g. the '|d'
handler pushes "Color palette index out of range", "Bits value out of range"
and "RGB Color value is out of range!", which together identify all three of
its arguments without any guesswork.

This script walks every handler in the dispatch table and collects the
diagnostics it references, giving a field-level reference for the whole
command set rather than one command at a time.

Method, per handler:
  1. Bound it by the next handler start in address order (handlers are
     interleaved with helpers, so this is an upper bound -- strings from a
     following helper can bleed in; see the caveat printed with the output).
  2. Collect `push imm32` operands that land in .rdata/.data.
  3. Keep those that decode as plausible diagnostics.

Usage:
    python scripts/dll-handler-semantics.py <path>/Ripscrip.dll [-o OUT.json]
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

IB = 0x10000000
TABLE_RVA = 0x080820
ENTRY_SIZE = 40
ENTRY_COUNT = 129
# A handler's own diagnostics sit close to its entry; beyond this the bound is
# almost certainly reaching into unrelated code.
MAX_HANDLER_SPAN = 0x900


def load(path):
    d = Path(path).read_bytes()
    e = struct.unpack_from("<I", d, 0x3C)[0]
    coff = e + 4
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
    ap.add_argument("-o", "--out", default="handler-semantics.json")
    args = ap.parse_args()

    d, secs = load(args.dll)
    T = next(s for s in secs if s["n"] == ".text")

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
            t = d[o:end].decode("ascii")
        except UnicodeDecodeError:
            return None
        return t if all(0x20 <= ord(c) < 0x7F for c in t) else None

    base = rva2off(TABLE_RVA)
    entries = []
    for i in range(ENTRY_COUNT):
        raw = d[base + i * ENTRY_SIZE: base + (i + 1) * ENTRY_SIZE]
        h = struct.unpack_from("<I", raw, 1)[0] - IB
        letter = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        entries.append(dict(slot=i, handler=h, argc=argc,
                            letter=chr(letter) if 0x20 <= letter < 0x7F
                            else f"0x{letter:02x}"))

    starts = sorted({e["handler"] for e in entries})
    nxt = {s: min(starts[k + 1] if k + 1 < len(starts) else s + MAX_HANDLER_SPAN,
                  s + MAX_HANDLER_SPAN)
           for k, s in enumerate(starts)}

    # A diagnostic is a sentence-ish string; a bare identifier is usually the
    # function's own name, which dll-name-handlers.py already reports.
    def is_diagnostic(t):
        if len(t) < 8 or "()" in t:
            return False
        return " " in t and re.search(r"[a-z]", t) is not None

    out = []
    for e in entries:
        lo, hi = e["handler"], nxt[e["handler"]]
        lo_off, hi_off = rva2off(lo), rva2off(hi)
        msgs, names = [], []
        if lo_off is not None and hi_off is not None:
            i = lo_off
            while i < min(hi_off, T["raw"] + T["rs"]) - 5:
                if d[i] == 0x68:
                    va = struct.unpack_from("<I", d, i + 1)[0]
                    if IB + 0x76000 <= va <= IB + 0x96000:
                        t = read_cstr(va)
                        if t:
                            if t.endswith("()"):
                                if t not in names:
                                    names.append(t)
                            elif is_diagnostic(t) and t not in msgs:
                                msgs.append(t)
                i += 1
        out.append(dict(slot=e["slot"], letter=e["letter"],
                        handler=f"0x{e['handler']:06x}", argc=e["argc"],
                        name=names[0][:-2] if names else None,
                        diagnostics=msgs))

    with_diag = [r for r in out if r["diagnostics"]]
    print(f"handlers with recovered diagnostics: {len(with_diag)}/{len(out)}")
    print("\nCAVEAT: handler bounds are upper bounds (next-start, capped at "
          f"0x{MAX_HANDLER_SPAN:X}); a following helper's strings can bleed in.\n")
    for r in out:
        if r["diagnostics"]:
            nm = f" {r['name']}" if r["name"] else ""
            print(f"  {r['letter']!r:>6} @{r['handler']} argc={r['argc']}{nm}")
            for m in r["diagnostics"][:6]:
                print(f"         - {m}")
    Path(args.out).write_text(json.dumps(out, indent=1))
    print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
