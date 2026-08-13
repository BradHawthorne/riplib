#!/usr/bin/env python3
"""Verify docs/spec/13-dll-command-table.md against the driver, row by row.

Segment 13 calls itself "A RECORD OF THE BINARY, not an interpretation of
it".  That claim is only worth something if something checks it, and until
now nothing did -- the file carried a "Regenerate with ..." line naming a
script that emits a different format, so the stated reproduction step did
not in fact reproduce the file.

This reads every tabulated row back and compares slot, letter, handler
pointer, argc and argument types against the dispatch record itself, then
checks the surrounding prose claims (entry count, RVA, MD5, per-section
totals) that a reader would otherwise have to take on trust.

Exits non-zero on any disagreement, so it can gate a build.

    python scripts/check-dll-table.py <path>/RIPSCRIP.DLL
"""
import argparse
import hashlib
import pathlib
import re
import struct
import sys

DOC = pathlib.Path(__file__).resolve().parent.parent / "docs" / "spec" / "13-dll-command-table.md"
TABLE_RVA = 0x080820
ENTRIES = 129
STRIDE = 40


# ---------------------------------------------------------------- the binary

def load_records(dll_path):
    """The dispatch record, read straight out of the PE."""
    d = dll_path.read_bytes()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    coff = pe + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    optsz = struct.unpack_from("<H", d, coff + 16)[0]
    secs = []
    for i in range(nsec):
        o = coff + 20 + optsz + i * STRIDE
        vs, va, rs, rp = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, vs, rp, rs))

    def rva_to_off(r):
        for va, vs, rp, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + rp
        raise SystemExit("RVA 0x%06x is outside every section" % r)

    base = rva_to_off(TABLE_RVA)
    out = []
    for i in range(ENTRIES):
        raw = d[base + i * STRIDE: base + (i + 1) * STRIDE]
        handler = struct.unpack_from("<I", raw, 1)[0]
        letter = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        types = []
        for b in raw[20:38]:
            if b == 0:
                break
            types.append("XY" if b == 0xFF else "color" if b == 0xFE
                         else "mega%d" % b if b in (1, 2, 4) else "0x%02x" % b)
        out.append({"slot": i, "handler": handler & 0xFFFFFF,
                    "letter": letter, "argc": argc, "types": types})
    return out, hashlib.md5(d).hexdigest()


# ------------------------------------------------------------------ the doc

ROW = re.compile(
    r"^\s*(\d+)\s+\|(0x00|[0-9]?(?:ESC|.))\s+0x([0-9a-f]{6})\s+(-?\d+|var)\s+(\S+)\s*(.*)$"
)
# The level headings carry a parenthesised aside of their own -- "LEVEL 1
# (prefix '1')   (26 commands)" -- so the count must be taken from the LAST
# parenthesis on the line, not the first one after the level number.
SECTION = re.compile(r"^13\.(\d)\s+LEVEL\s+(\d)\b.*\((\d+)\s+commands\)\s*$")


def parse_doc():
    """Tabulated rows and the section totals that head them."""
    rows, sections, cur = [], [], None
    for n, line in enumerate(DOC.read_text(encoding="utf-8").split("\n"), 1):
        s = SECTION.match(line.strip())
        if s:
            cur = {"level": int(s.group(2)), "claimed": int(s.group(3)),
                   "line": n, "rows": 0}
            sections.append(cur)
            continue
        m = ROW.match(line)
        if not m or "SLOT" in line:
            continue
        slot, cmd, handler, argc, _name, types = m.groups()
        # The level prefix is part of the command spelling, not the letter.
        if cmd == "0x00":
            letter = 0x00
        else:
            bare = cmd[1:] if (len(cmd) > 1 and cmd[0] in "123") else cmd
            letter = 27 if bare == "ESC" else ord(bare)
        rows.append({
            "line": n, "slot": int(slot), "letter": letter, "cmd": cmd,
            "handler": int(handler, 16),
            "argc": argc,
            "types": [t.strip() for t in types.split(",") if t.strip() and t.strip() != "-"],
        })
        if cur:
            cur["rows"] += 1
    return rows, sections


# ------------------------------------------------------------------- checks

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll", type=pathlib.Path)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    recs, md5 = load_records(a.dll)
    rows, sections = parse_doc()
    text = DOC.read_text(encoding="utf-8")
    defects = []

    # --- prose claims a reader would otherwise take on trust ---------------
    if md5 not in text:
        defects.append("MD5: doc does not carry this binary's hash %s" % md5)
    # Case-fold only the hex digits; upper-casing the whole string would turn
    # the "0x" prefix into "0X" and never match.
    if not re.search(r"0x0*%x\b" % TABLE_RVA, text, re.IGNORECASE):
        defects.append("RVA: doc does not state the table RVA 0x%06X" % TABLE_RVA)
    if not re.search(r"\b%d\b\s+entries" % ENTRIES, text):
        defects.append("entry count: doc does not state '%d entries'" % ENTRIES)

    # --- every tabulated row against the record ---------------------------
    seen = set()
    for r in rows:
        if not (0 <= r["slot"] < ENTRIES):
            defects.append("line %d: slot %d is outside 0..%d"
                           % (r["line"], r["slot"], ENTRIES - 1))
            continue
        seen.add(r["slot"])
        rec = recs[r["slot"]]
        where = "line %d slot %-3d (%s)" % (r["line"], r["slot"], r["cmd"])
        if r["letter"] != rec["letter"]:
            defects.append("%s: letter doc=0x%02x binary=0x%02x"
                           % (where, r["letter"], rec["letter"]))
        if r["handler"] != rec["handler"]:
            defects.append("%s: handler doc=0x%06x binary=0x%06x"
                           % (where, r["handler"], rec["handler"]))
        doc_argc = -1 if r["argc"] == "var" else int(r["argc"])
        bin_argc = rec["argc"]
        # 'var' stands for any negative argc; the record's magnitude is the
        # count of types it still declares.
        if (doc_argc < 0) != (bin_argc < 0) or (doc_argc >= 0 and doc_argc != bin_argc):
            defects.append("%s: argc doc=%s binary=%d" % (where, r["argc"], bin_argc))
        if r["types"] != rec["types"]:
            defects.append("%s: types doc=[%s] binary=[%s]"
                           % (where, ", ".join(r["types"]), ", ".join(rec["types"])))

    missing = sorted(set(range(ENTRIES)) - seen)
    if missing:
        defects.append("coverage: %d slot(s) never tabulated: %s"
                       % (len(missing), ", ".join(map(str, missing))))

    # --- section totals against the rows actually printed under them ------
    for s in sections:
        if s["claimed"] != s["rows"]:
            defects.append("line %d: LEVEL %d header claims %d commands, "
                           "%d rows follow" % (s["line"], s["level"],
                                               s["claimed"], s["rows"]))
    total = sum(s["rows"] for s in sections)
    if total != ENTRIES:
        defects.append("section rows sum to %d, table has %d entries"
                       % (total, ENTRIES))

    # The prose quotes a split; it must match the sections it introduces.
    split = re.search(r"resulting split is (\d+)/(\d+)/(\d+)/(\d+)", text)
    if split:
        quoted = [int(g) for g in split.groups()]
        actual = [s["rows"] for s in sorted(sections, key=lambda x: x["level"])]
        if quoted != actual:
            defects.append("prose quotes split %s but the sections hold %s"
                           % ("/".join(map(str, quoted)), "/".join(map(str, actual))))
    else:
        defects.append("prose no longer states a 'resulting split is a/b/c/d'")

    # Levels are contiguous slot runs.  Checking only that each section's
    # header count matches the rows printed under it cannot catch a row
    # filed in the wrong section -- that is how slot 48 ('|N', level 0) sat
    # under LEVEL 1 as '|1N' through two separate corrections of the split.
    # So verify the runs themselves.
    for s in sections:
        s["slots"] = []
    for r in rows:
        owner = None
        for s in sections:
            if s["line"] < r["line"] and (owner is None or s["line"] > owner["line"]):
                owner = s
        if owner is not None:
            owner["slots"].append(r["slot"])
    prev_end = -1
    for s in sorted(sections, key=lambda x: x["level"]):
        if not s["slots"]:
            continue
        lo, hi = min(s["slots"]), max(s["slots"])
        if sorted(s["slots"]) != list(range(lo, hi + 1)):
            gaps = sorted(set(range(lo, hi + 1)) - set(s["slots"]))
            defects.append("LEVEL %d is not a contiguous slot run: %d..%d "
                           "missing %s" % (s["level"], lo, hi,
                                           ", ".join(map(str, gaps))))
        if lo != prev_end + 1:
            defects.append("LEVEL %d starts at slot %d, but the previous "
                           "level ended at %d" % (s["level"], lo, prev_end))
        prev_end = hi

    # A level-N section must spell its commands with the level-N prefix.
    for s in sections:
        for r in rows:
            if r["line"] <= s["line"] or r["slot"] not in s["slots"]:
                continue
            spell = r["cmd"]
            if spell == "0x00":
                continue
            want = "" if s["level"] == 0 else str(s["level"])
            got = spell[0] if spell[0] in "123" else ""
            if got != want:
                defects.append("line %d: '%s' is filed under LEVEL %d but is "
                               "spelled for level %s"
                               % (r["line"], spell, s["level"], got or "0"))

    # --- report -----------------------------------------------------------
    if a.verbose:
        print("doc rows %d   binary entries %d   sections %d"
              % (len(rows), ENTRIES, len(sections)))
        for s in sections:
            print("  LEVEL %d: header %d, rows %d" % (s["level"], s["claimed"], s["rows"]))

    if defects:
        print("check-dll-table: %d defect(s)\n" % len(defects))
        for x in defects:
            print("  ! " + x)
        return 1
    print("checked %d rows against %d dispatch entries." % (len(rows), ENTRIES))
    print("OK: segment 13 matches the binary.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
