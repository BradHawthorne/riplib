#!/usr/bin/env python3
"""Compare RIPlib and a third-party reference, each against the driver.

This reproduces the counts in 14-divergence-register.md section 14.2 --
the thirteen commands where bbs-land/remote-imaging-protocol and
RIPSCRIP.DLL disagree.  Until 2026-08-13 those numbers came from a
script that lived in a scratch directory, so the register's claim that
"every count in this file is reproducible from the scripts in 14.1" was
not true of 14.2, and the tool itself rotted unnoticed: it carried
hardcoded switch-block line numbers that went stale as src/ripscrip.c
grew, bracketed the wrong code, and reported three RIPlib divergences
that did not exist.  Boundaries here are derived from structural
markers for that reason.

Neither the driver nor the reference is vendored -- both are
third-party -- so both are arguments:

    python scripts/ref-compare.py <path>/RIPSCRIP.DLL [<path>/reference.md]

With no reference, only RIPlib is compared, which is still the useful
half day to day.  Exits non-zero if RIPlib disagrees with the driver
anywhere, so it can gate a build; a reference disagreeing is reported
but never fails the run, since the reference is evidence and not the
measure.
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "ripscrip.c"
TABLE_RVA, ENTRIES, STRIDE = 0x080820, 129, 40

# Levels are contiguous slot runs, per 13-dll-command-table.md.  A handler
# address band was tried and rejected -- it misplaces slot 48 ('|N').
RUNS = ((0, 0, 84), (1, 85, 109), (2, 110, 121), (3, 122, 128))

FIELD = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*:\s*([A-Za-z0-9]+)")


# ------------------------------------------------------------------- driver

def load_driver(dll):
    d = dll.read_bytes()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    coff = pe + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    optsz = struct.unpack_from("<H", d, coff + 16)[0]
    secs = []
    for i in range(nsec):
        o = coff + 20 + optsz + i * STRIDE
        vs, va, rs, rp = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, vs, rp, rs))
    base = next((TABLE_RVA - va + rp for va, vs, rp, rs in secs
                 if va <= TABLE_RVA < va + max(vs, rs)), None)
    if base is None:
        raise SystemExit("dispatch table RVA outside every section")
    out = {}
    for i in range(ENTRIES):
        raw = d[base + i * STRIDE: base + (i + 1) * STRIDE]
        letter = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        # ESC (0x1B) is a real command letter, not padding.  Filtering on
        # printability dropped it from the driver side, which -- together
        # with the `case 'X':` extractor on the RIPlib side -- meant
        # '|1<ESC>' was invisible to this comparison from both directions
        # at once.  It is the Level 1 command with the most corpus traffic.
        if letter != 0x1B and not (0x20 <= letter < 0x7F):
            continue
        types = []
        for b in raw[20:38]:
            if b == 0:
                break
            types.append("n" if b in (0xFF, 0xFE) else str(b))
        lvl = next(l for l, lo, hi in RUNS if lo <= i <= hi)
        out.setdefault((lvl, chr(letter)), (argc, types))
    return out


# ------------------------------------------------------------------- riplib

def switch_bounds(lines):
    """Where each level's switch block starts.

    Derived, never hardcoded.  A stale literal line number brackets the
    WRONG code silently, which is how three phantom divergences were once
    reported -- '|R' showed '|1R's record because the ranges had drifted
    past it.
    """
    marks = {}
    pats = (("l3", "if (s->is_level3)"), ("l2", "if (s->is_level2)"),
            ("l1", "if (s->is_level1)"), ("l0", "/* Level 0 commands */"))
    for i, line in enumerate(lines, 1):
        for key, needle in pats:
            if key not in marks and needle in line:
                marks[key] = i
    missing = [k for k, _ in pats if k not in marks]
    if missing:
        raise SystemExit("cannot locate switch blocks in %s: missing %s"
                         % (SRC.name, ", ".join(missing)))
    return marks


def signature_block(lines, idx):
    """The comment text that belongs to a case's signature.

    Reads the WHOLE leading comment, not just the text on the 'case' line
    -- several handlers wrap their signature onto a continuation line, and
    reading one line silently truncated them into phantom divergences.
    Stops at a blank comment line, a prose line, or a sentence break, so
    the discussion that follows a signature is not mined for fields.
    """
    buf = []
    for j in range(idx, min(idx + 12, len(lines))):
        ln = lines[j]
        body = ln.split("/*", 1)[1] if j == idx and "/*" in ln else ln
        body = body.lstrip().lstrip("*").strip()
        if j > idx:
            if body == "" or ln.strip() in ("*", "*/"):
                break
            if not FIELD.search(body):
                break
        buf.append(body)
        if "*/" in ln:
            break
        if re.search(r"\.\s", body):
            break
    return re.split(r"\.\s", " ".join(buf))[0]


def widths_from(text):
    out = []
    for _, f in FIELD.findall(text):
        f = f.upper()
        out.append("n" if f in ("XY", "CM") else f if f.isdigit() else "?")
    return out


def load_riplib():
    lines = SRC.read_text(encoding="latin-1").split("\n")
    m = switch_bounds(lines)

    def level_at(n):
        if m["l3"] < n < m["l2"]:
            return 3
        if m["l1"] < n < m["l0"]:
            return 1
        if n > m["l0"]:
            return 0
        return None

    # `case 0x1B:` is the ESC command and must be matched too.  Matching only
    # `case 'X':` skipped it -- and '|1<ESC>' is the Level 1 command with the
    # MOST corpus traffic (80 uses), so the one command this comparison could
    # not see was the one shipped content exercises hardest.  It carried a
    # five-character prefix against the record's four for months.
    out = {}
    for i, line in enumerate(lines, 1):
        mt = re.match(r"\s+case '(.)':(.*)", line)
        if not mt:
            me = re.match(r"\s+case 0x1[Bb]:(.*)", line)
            if me:
                mt = type("M", (), {"group": lambda self, n: "\x1b" if n == 1
                                    else me.group(1)})()
            else:
                continue
        lvl = level_at(i)
        if lvl is None:
            continue
        w = widths_from(signature_block(lines, i - 1))
        if w:
            out.setdefault((lvl, mt.group(1)), w)
    return out


# ---------------------------------------------------------------- reference

def load_reference(path):
    out = {}
    for line in path.read_text(encoding="utf-8").split("\n"):
        if not line.startswith("| ["):
            continue
        c = [x.strip() for x in line.split("|")[1:-1]]
        if len(c) < 4:
            continue
        sym = re.match(r"\[([A-Z_0-9]+)\]", c[0])
        cmd = re.sub(r"\(\\?\*\)|`", "", c[2]).strip()
        if not sym or c[1] not in "0123" or len(cmd) != 1:
            continue
        args = c[3].replace("`", "")
        # An elided list ("c1:2 c2:2 ... c16:2") yields only the pairs
        # literally written, which once reported '|Q' as a 32-vs-6
        # divergence where the reference in fact agrees.  Flag, don't count.
        elided = "..." in args or "…" in args
        out[(int(c[1]), cmd)] = (widths_from(args), elided)
    return out


# ---------------------------------------------------------------- compare

def compatible(a, b):
    """Same field, allowing the notation difference (literal 2 vs width-n)."""
    return a == b or {a, b} == {"n", "2"}


def classify(who, driver):
    """exact / notation / string-tail / different.

    The string-tail class exists because the record types only the NUMERIC
    argument array; a trailing string is passed out of band, so it never
    appears.  A field list that matches the record and then documents one
    further variable field is CORRECT, and the record's fixed total is the
    offset that string starts at.
    """
    exact = notation = tail = 0
    reals, tails, elided = [], [], []
    for key, val in sorted(who.items()):
        w, is_elided = val if isinstance(val, tuple) else (val, False)
        if is_elided:
            elided.append(key)
            continue
        if key not in driver or not w:
            continue
        argc, dw = driver[key]
        if argc < 0 or not dw:
            continue
        if w == dw:
            exact += 1
        elif len(w) == len(dw) and all(compatible(a, b) for a, b in zip(w, dw)):
            notation += 1
        elif (len(w) == len(dw) + 1 and w[-1] == "?"
              and all(compatible(a, b) for a, b in zip(w[:-1], dw))):
            tail += 1
            tails.append((key, dw, w))
        else:
            reals.append((key, dw, w))
    return exact, notation, tail, reals, tails, elided


def spell(key):
    lvl, letter = key
    return "|" + ("" if lvl == 0 else str(lvl)) + letter


def report(name, data, driver, verbose):
    e, n, t, reals, tails, elided = classify(data, driver)
    total = e + n + t + len(reals)
    print("%-9s vs driver:  exact %2d   notation %2d   string-tail %2d   "
          "DIFFERENT %2d   (of %d compared)" % (name, e, n, t, len(reals), total))
    for k, dw, ow in reals:
        print("  ! %-5s driver [%-26s]  %s [%s]"
              % (spell(k), " ".join(dw), name, " ".join(ow)))
    if verbose:
        for k, dw, ow in tails:
            print("  . %-5s driver [%-26s]  %s [%s]   (fixed prefix + string)"
                  % (spell(k), " ".join(dw), name, " ".join(ow)))
    if elided:
        print("  - %d command(s) skipped: elided field list (%s)"
              % (len(elided), ", ".join(spell(k) for k in sorted(elided))))
    print()
    return len(reals)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll", type=pathlib.Path)
    ap.add_argument("reference", nargs="?", type=pathlib.Path,
                    help="bbs-land 3.0 command reference (markdown)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    driver = load_driver(a.dll)
    ours = load_riplib()
    bad = report("RIPlib", ours, driver, a.verbose)

    if a.reference:
        if not a.reference.exists():
            raise SystemExit("reference not found: %s" % a.reference)
        report("bbs-land", load_reference(a.reference), driver, a.verbose)
    else:
        print("(no reference given -- pass the bbs-land command reference to")
        print(" reproduce the section 14.2 counts)")

    if bad:
        print("RIPlib disagrees with the driver in %d place(s)." % bad)
        return 1
    print("RIPlib agrees with the dispatch record everywhere it is comparable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
