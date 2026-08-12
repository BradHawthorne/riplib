#!/usr/bin/env python3
"""Extract the RIPSCRIP.DLL v3.0.7 command dispatch table.

The driver dispatches RIPscrip commands through a table of fixed-size records.
Its location and layout were recovered by the original reconstruction (see
docs/historical/ripscrip-v3-RE-notes.md):

    RVA 0x080820, 129 entries, 40 bytes per entry
      [+0]      index
      [+1..4]   handler pointer
      [+15]     command letter
      [+16..19] argument count (signed; negative = variable length)
      [+20..]   argument type codes

This script re-derives that table from the binary so every opcode->handler
binding in the specification carries a checkable citation, rather than being
asserted. Layout assumptions are validated against the image before use and a
failure is reported rather than papered over.

Usage:
    python scripts/dll-dispatch-table.py <path>/Ripscrip.dll [-o OUT.json]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

TABLE_RVA = 0x080820
ENTRY_SIZE = 40
ENTRY_COUNT = 129
IMAGE_BASE = 0x10000000

ARGTYPE = {
    0xFF: "XY",       # coordinate pair, width per SET_COORDINATE_SIZE
    0xFE: "color",    # width per SET_COLOR_MODE
    0x01: "mega1",
    0x02: "mega2",
    0x04: "mega4",
}

LEVEL_PREFIX = {0: "", 1: "1", 2: "2", 3: "3"}


def load_pe(path):
    d = Path(path).read_bytes()
    e = struct.unpack_from("<I", d, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    optsz = struct.unpack_from("<H", d, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", d, opt + 0x1C)[0]
    secs = []
    so = opt + optsz
    for i in range(nsec):
        o = so + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
        secs.append(dict(name=name, rva=vaddr, vsize=vsize, raw=rawptr, rawsize=rawsize))
    return d, secs, image_base


def rva_to_off(secs, rva):
    for s in secs:
        if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["rawsize"]):
            return rva - s["rva"] + s["raw"]
    return None


def sec_of(secs, rva):
    for s in secs:
        if s["rva"] <= rva < s["rva"] + max(s["vsize"], s["rawsize"]):
            return s["name"]
    return "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll")
    ap.add_argument("-o", "--out", default="dispatch-table.json")
    ap.add_argument("--rva", type=lambda x: int(x, 0), default=TABLE_RVA)
    ap.add_argument("--count", type=int, default=ENTRY_COUNT)
    args = ap.parse_args()

    d, secs, ib = load_pe(args.dll)
    text = next(s for s in secs if s["name"] == ".text")
    tlo, thi = text["rva"], text["rva"] + text["vsize"]

    base = rva_to_off(secs, args.rva)
    if base is None:
        print(f"ERROR: RVA 0x{args.rva:06x} is not mapped", file=sys.stderr)
        return 2
    print(f"dispatch table: RVA 0x{args.rva:06x} -> VA 0x{ib + args.rva:08x} "
          f"({sec_of(secs, args.rva)}) file offset 0x{base:06x}")

    rows, valid = [], 0
    for i in range(args.count):
        o = base + i * ENTRY_SIZE
        if o + ENTRY_SIZE > len(d):
            break
        raw = d[o:o + ENTRY_SIZE]
        index = raw[0]
        handler = struct.unpack_from("<I", raw, 1)[0]
        letter = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        handler_rva = handler - ib
        ok = tlo <= handler_rva < thi
        if ok:
            valid += 1
        argtypes = []
        for b in raw[20:]:
            if b == 0:
                break
            argtypes.append(ARGTYPE.get(b, f"0x{b:02x}"))
        rows.append(dict(
            slot=i, index=index,
            handler=f"0x{handler:08x}", handler_rva=f"0x{handler_rva:06x}",
            handler_in_text=ok,
            letter=chr(letter) if 0x20 <= letter < 0x7F else None,
            letter_byte=f"0x{letter:02x}",
            argc=argc, variable_length=argc < 0,
            argtypes=argtypes,
        ))

    print(f"entries read: {len(rows)}   handler pointers inside .text: {valid}/{len(rows)}")
    if valid < len(rows) * 0.8:
        print("WARNING: most handler pointers fall outside .text — the recorded layout "
              "may not hold for this image. Treat output as unvalidated.", file=sys.stderr)

    # validation anchor recorded by the original analysis:
    # RIP_BOUNDED_TEXT, command '"' (0x22), handler RVA 0x01A0DA
    anchor = [r for r in rows if r["letter"] == '"']
    if anchor:
        a = anchor[0]
        match = a["handler_rva"].lower() == "0x01a0da"
        print(f"anchor RIP_BOUNDED_TEXT ('\"'): handler {a['handler_rva']} "
              f"{'MATCHES' if match else 'DOES NOT MATCH'} the recorded 0x01A0DA")
    else:
        print("anchor RIP_BOUNDED_TEXT ('\"'): letter not found in table")

    printable = [r for r in rows if r["letter"] and r["handler_in_text"]]
    print(f"\n{'SLOT':>4} {'LTR':>4} {'HANDLER':>10} {'ARGC':>5}  ARGTYPES")
    for r in printable:
        print(f"{r['slot']:>4} {r['letter']!r:>4} {r['handler_rva']:>10} {r['argc']:>5}  "
              f"{','.join(r['argtypes'])}")

    Path(args.out).write_text(json.dumps(rows, indent=1))
    print(f"\nwrote {args.out}  ({len(rows)} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
