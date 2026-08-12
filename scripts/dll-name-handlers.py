#!/usr/bin/env python3
"""Name the RIPSCRIP.DLL command handlers from their own error-reporting strings.

Every handler that can report an error pushes its own name as a bare
"FuncName()" string before calling the error reporter, e.g. the '|W' handler
at RVA 0x02102C pushes "RIP_WriteMode()". Cross-referencing those pushes back
to the dispatch table yields command letter -> handler -> NAME, with a citable
address for each.

Method:
  1. Collect every string matching ^Ident()$ (and "module.cpp - Ident()").
  2. Find `push imm32` sites in .text referencing each string.
  3. Bound each dispatch handler by the next handler start in address order.
  4. Attribute a name to a handler when a push of that name falls in its range.

A handler with no error path carries no name string; those are reported as
unnamed rather than guessed.

Usage:
    python scripts/dll-name-handlers.py <path>/Ripscrip.dll [-d dispatch.json]
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
    ap.add_argument("-o", "--out", default="handler-names.json")
    args = ap.parse_args()

    d, secs = load(args.dll)
    T = next(s for s in secs if s["n"] == ".text")
    tlo, thi = T["raw"], T["raw"] + T["rs"]

    def off2rva(o):
        for s in secs:
            if s["raw"] <= o < s["raw"] + s["rs"]:
                return s["rva"] + (o - s["raw"])
        return None

    def rva2off(r):
        for s in secs:
            if s["rva"] <= r < s["rva"] + max(s["vs"], s["rs"]):
                return r - s["rva"] + s["raw"]
        return None

    # 1. function-name strings
    pat = re.compile(rb"(?:[A-Za-z0-9_]{2,24}\.(?:cpp|c) - )?([A-Za-z_][A-Za-z0-9_]{2,44})\(\)\x00")
    names = {}
    for m in pat.finditer(d):
        s = m.start()
        rva = off2rva(s)
        if rva is None:
            continue
        names[IB + rva] = m.group(1).decode()
    print(f"function-name strings: {len(names)}")

    # 2. push xrefs
    xref = {}
    for va, nm in names.items():
        needle = b"\x68" + struct.pack("<I", va)
        st = tlo
        while True:
            i = d.find(needle, st, thi)
            if i < 0:
                break
            xref.setdefault(off2rva(i), []).append(nm)
            st = i + 1
    print(f"name-push sites in .text: {len(xref)}")

    # 3. dispatch table + handler bounds
    base = rva2off(TABLE_RVA)
    entries = []
    for i in range(ENTRY_COUNT):
        raw = d[base + i * ENTRY_SIZE: base + (i + 1) * ENTRY_SIZE]
        handler = struct.unpack_from("<I", raw, 1)[0] - IB
        letter = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        entries.append(dict(slot=i, handler=handler, argc=argc,
                            letter=chr(letter) if 0x20 <= letter < 0x7F else f"0x{letter:02x}"))
    starts = sorted({e["handler"] for e in entries})
    nxt = {s: (starts[k + 1] if k + 1 < len(starts) else s + 0x600)
           for k, s in enumerate(starts)}

    # 4. attribute
    out, named = [], 0
    for e in entries:
        lo, hi = e["handler"], nxt[e["handler"]]
        cands = []
        for site, nms in xref.items():
            if site is not None and lo <= site < hi:
                cands.extend(nms)
        uniq = sorted(set(cands))
        # Only name a handler when attribution is unambiguous. A loose upper
        # bound can sweep in neighbouring helpers, so multiple distinct
        # candidates means "unknown", not "pick the first".
        name = uniq[0] if len(uniq) == 1 else None
        if name:
            named += 1
        out.append(dict(slot=e["slot"], letter=e["letter"],
                        handler=f"0x{e['handler']:06x}", argc=e["argc"],
                        name=name, all_candidates=uniq))

    print(f"handlers named: {named}/{len(entries)}")
    print(f"\n{'SLOT':>4} {'CMD':>5} {'HANDLER':>9} {'ARGC':>5}  NAME")
    for r in out:
        if r["name"]:
            print(f"{r['slot']:>4} {r['letter']:>5} {r['handler']:>9} {r['argc']:>5}  {r['name']}")
    amb = [r for r in out if not r["name"] and r["all_candidates"]]
    print(f"\nambiguous (bound swept in neighbours): {len(amb)}")

    Path(args.out).write_text(json.dumps(out, indent=1))
    print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
