#!/usr/bin/env python3
"""Check RIPlib's field NAMES against the driver's own diagnostics.

Every other checker in this repo compares SHAPES.  ref-compare.py matches
field widths against the dispatch record, dll-conformance.py matches read
offsets and length gates, check-spec-examples.py matches payload lengths
and decoded values.  None of them can see a field that is the right width
and the wrong THING.

'|b' RIP_ExtendedTextWindow proved that costs real accuracy.  Its split --
2/2/2/2/2/2/1/4/3 -- matched the record exactly while args[4] and args[5]
were documented as foreground and background COLOURS and args[7] as a font
SIZE.  They are a cell width, a cell height and the flags word.  Every
automated check passed for months; the thing that gave it away was the
handler's own diagnostic, "Zero width value is not allowed", which is not
something a colour index ever says.

So this checks that: for each command, pull the diagnostic strings out of
its handler body, extract the CONCEPTS they name, and require that a
concept the driver complains about appears somewhere in RIPlib's field
names for that command.

It is deliberately advisory.  A mismatch is a prompt to go and read the
handler, not proof of a defect -- a driver can validate something RIPlib
legitimately does not model, and several do.  It exits 0 unless --strict
is given, because a noisy check wired into a build gets muted, and a muted
check is worse than none.

    python scripts/check-field-names.py <path>/RIPSCRIP.DLL [--verbose]
"""
import argparse
import bisect
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "ripscrip.c"
TABLE_RVA, ENTRIES, STRIDE = 0x080820, 129, 40
RUNS = ((0, 0, 84), (1, 85, 109), (2, 110, 121), (3, 122, 128))

# A diagnostic naming one of these implies a field carrying that concept.
# Kept small and concrete on purpose: a big synonym table would fire on
# everything and train the reader to ignore it.
CONCEPTS = {
    "width":     ("width", "wid", "w"),
    "height":    ("height", "hgt", "h"),
    "font":      ("font", "fontid", "fid"),
    "flags":     ("flags", "flag", "style"),
    "mode":      ("mode", "wmode", "writemode"),
    "color":     ("color", "colour", "col", "fore", "back", "fg", "bg",
                  "rgb", "palette", "pal"),
    "angle":     ("angle", "ang", "start", "end", "sa", "ea", "rotation",
                  "rot", "skew"),
    "radius":    ("radius", "rad", "rx", "ry", "r"),
    "port":      ("port", "portnum"),
    "slot":      ("slot", "num", "index", "idx", "id", "target", "entry"),
    "stretch":   ("stretch",),
    "direction": ("dir", "direction"),
    "size":      ("size", "sz"),
}
# Words that appear in diagnostics but describe the command, not a field.
IGNORE = ("parameter", "string", "memory", "buffer", "instance", "invalid",
          "illegal", "unable", "cannot", "can't", "protected", "temp")


def load(dll):
    d = dll.read_bytes()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    coff = pe + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    optsz = struct.unpack_from("<H", d, coff + 16)[0]
    base = struct.unpack_from("<I", d, coff + 20 + 28)[0]
    secs = []
    for i in range(nsec):
        o = coff + 20 + optsz + i * STRIDE
        vs, va, rs, rp = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, vs, rp, rs))
    return d, secs, base


def make_maps(d, secs, base):
    def r2o(r):
        for va, vs, rp, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + rp
        return None

    def o2r(o):
        for va, vs, rp, rs in secs:
            if rp <= o < rp + rs:
                return va + (o - rp)
        return None
    return r2o, o2r


def handler_diagnostics(d, secs, base):
    """{(level, letter): set(diagnostic strings)} from each handler body."""
    r2o, o2r = make_maps(d, secs, base)
    tbl = r2o(TABLE_RVA)
    slots, starts = {}, []
    for i in range(ENTRIES):
        h = struct.unpack_from("<I", d, tbl + i * STRIDE + 1)[0] & 0xFFFFFF
        letter = d[tbl + i * STRIDE + 15]
        slots.setdefault(h, (i, letter))
        starts.append(h)
    starts = sorted(set(starts))

    def owner(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i < 0:
            return None
        s = starts[i]
        nxt = starts[i + 1] if i + 1 < len(starts) else s + 0x600
        return s if rva < nxt and rva - s <= 0x600 else None

    out = {}
    for m in re.finditer(rb"\x68(....)", d):          # push imm32
        va = struct.unpack("<I", m.group(1))[0]
        if not (base < va < base + 0x100000):
            continue
        so = r2o(va - base)
        if so is None or so >= len(d):
            continue
        s = d[so:so + 72].split(b"\x00")[0]
        if len(s) < 10 or not all(32 <= c < 127 for c in s):
            continue
        rva = o2r(m.start())
        if rva is None:
            continue
        o = owner(rva)
        if o is None or o not in slots:
            continue
        i, letter = slots[o]
        if letter == 0 or not (letter == 0x1B or 0x20 <= letter < 0x7F):
            continue
        lvl = next(l for l, lo, hi in RUNS if lo <= i <= hi)
        out.setdefault((lvl, chr(letter)), set()).add(s.decode())
    return out


# The width must be a NUMBER or XY/CM.  Accepting any word matched ordinary
# prose -- "protected slot: driver refuses" was read as a field called
# 'slot', which then satisfied a SLOT concept and suppressed a real
# question.  A permissive extractor does not merely add noise here; it
# manufactures matches and turns the check into a rubber stamp.
FIELD = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*:\s*([0-9]+|XY|CM)\b")


def riplib_fields():
    """{(level, letter): [field names]} from the signature comments."""
    lines = SRC.read_text(encoding="latin-1").splitlines()
    marks = {}
    for i, l in enumerate(lines, 1):
        for k, n in (("l3", "if (s->is_level3)"), ("l2", "if (s->is_level2)"),
                     ("l1", "if (s->is_level1)"), ("l0", "/* Level 0 commands */")):
            if k not in marks and n in l:
                marks[k] = i
    if len(marks) != 4:
        raise SystemExit("cannot locate switch blocks in %s" % SRC.name)

    def lvl(n):
        if marks["l3"] < n < marks["l2"]:
            return 3
        if marks["l1"] < n < marks["l0"]:
            return 1
        if n > marks["l0"]:
            return 0
        return None

    out = {}
    for i, line in enumerate(lines, 1):
        m = re.match(r"\s+case '(.)':", line) or re.match(r"\s+case 0x1[Bb]:", line)
        if not m:
            continue
        L = lvl(i)
        if L is None:
            continue
        ch = m.group(1) if m.re.groups else "\x1b"
        # Read only the SIGNATURE, not the whole comment.  Scanning ten
        # lines pulls prose into the field list -- '|1U' picked up
        # "readable" and "instance" from a sentence, '|1W' picked up
        # "CORRECTED" -- and a field list padded with prose matches
        # anything, which turns this check into a rubber stamp.  Stop at
        # the first line that has no name:width pair.
        names = []
        for j in range(i - 1, min(i + 10, len(lines))):
            s = lines[j]
            found = [a for a, _ in FIELD.findall(s)]
            if not found and j > i - 1:
                break
            names += found
            if "*/" in s and j > i - 1:
                break
        if names:
            out.setdefault((L, ch), names)
    return out


def concepts_in(text):
    t = text.lower()
    hits = set()
    for concept in CONCEPTS:
        if re.search(r"\b%s\b" % concept, t) and concept not in IGNORE:
            hits.add(concept)
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll", type=pathlib.Path)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero on a mismatch (default: advisory)")
    a = ap.parse_args()

    d, secs, base = load(a.dll)
    diags = handler_diagnostics(d, secs, base)
    fields = riplib_fields()

    checked = 0
    flags = []
    for key, msgs in sorted(diags.items()):
        if key not in fields:
            continue
        checked += 1
        names = [n.lower() for n in fields[key]]
        for msg in sorted(msgs):
            for concept in concepts_in(msg):
                if any(any(alias == n or alias in n for alias in CONCEPTS[concept])
                       for n in names):
                    continue
                flags.append((key, concept, msg, fields[key]))

    tag = lambda k: "|%s%s" % ("" if k[0] == 0 else k[0],
                               "ESC" if k[1] == "\x1b" else k[1])
    print("commands with both diagnostics and a field list: %d" % checked)
    print("concepts named by the driver with no matching field: %d" % len(flags))
    if flags:
        print()
        seen = set()
        for key, concept, msg, names in flags:
            if (key, concept) in seen:
                continue
            seen.add((key, concept))
            print("  %-6s driver says %-9s  fields: %s"
                  % (tag(key), concept.upper(), " ".join(names)[:52]))
            if a.verbose:
                print("           \"%s\"" % msg)
    print()
    print("Advisory.  A mismatch means GO READ THE HANDLER, not that a defect")
    print("exists: the driver validates things RIPlib does not model, and")
    print("several of those are deliberate.  This is the only check here that")
    print("looks at what a field MEANS rather than how wide it is -- '|b' had")
    print("the right shape and the wrong names for months.")
    return 1 if (flags and a.strict) else 0


if __name__ == "__main__":
    sys.exit(main())
