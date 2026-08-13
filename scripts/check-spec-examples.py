#!/usr/bin/env python3
"""Check the spec chapters' command blocks against each other and the driver.

The chapters document each command as a block:

     Command:      |c
     Arguments:    color:2
     Format:       !|c<color>|
     Example:      !|c0F|          color=15 (EGA white)

Those four lines can disagree with each other and with the dispatch
record, and have: a stale 'Arguments:' line for '|1I' shipped
undetected because nothing compared it to anything.  Prose is not
self-checking.

Checks per block:

  1. WIDTH    the Example: payload is exactly as long as Arguments: says
  2. DECODE   every MegaNum in Example: decodes to the value its own
              trailing comment claims ("x=100, y=50")
  3. SPLIT    the Arguments: field split matches the dispatch record's.
              Two adjacent driver fields written as one wider field is
              reported: it is the same defect this project files against
              third-party references, and it is not exempt here.
  4. TOTAL    a 'Format:' line that states a character count states the
              right one

And once over every chapter, not only the blocks:

  5. LITERALS every '!|...|' anywhere in docs/spec is length-checked
              against the record for its command.  Segment 1's worked
              examples sit in free prose and carried the same "'4Q' is
              150" mistake as segment 2's '|L' block; the block checks
              found one and only a hand pass found the other.
              Only OVER-length is reported -- a shorter literal may
              legitimately omit optional trailing fields, and the
              record alone cannot say which those are.  Commands with
              a trailing string are excluded, since their literals are
              longer than the record by design.

A block that cannot be checked mechanically is reported as SKIPPED with
its reason, never silently passed -- counts are blind to values, and a
run that "passes" 105 blocks while skipping 90 is not evidence.

Known limits, stated so the pass is not read as more than it is: the
decode check needs the example to carry its own decoded values, either
as 'name=value' or as a tuple with exactly one number per field; 76 of
the 105 blocks have no Example: line at all; and nothing here checks
prose claims about SEMANTICS, only about widths, values and splits.

    python scripts/check-spec-examples.py [<path>/RIPSCRIP.DLL]
"""
import argparse
import pathlib
import re
import struct
import sys

SPEC = pathlib.Path(__file__).resolve().parent.parent / "docs" / "spec"
CHAPTERS = ["02-level0-drawing.md", "03-level1-interactive.md",
            "04-extended-commands.md", "05-level2-ports.md",
            "06a-v32-extensions.md"]
TABLE_RVA, ENTRIES, STRIDE = 0x080820, 129, 40

# Levels are contiguous slot runs, per 13-dll-command-table.md.  A handler
# ADDRESS band was tried and rejected: it misplaces slot 48 ('|N',
# RIP_SetBorder), whose code sits among the level-1 handlers.
RUNS = ((0, 0, 84), (1, 85, 109), (2, 110, 121), (3, 122, 128))

# Fields whose width follows a mode set at run time rather than a literal.
SYMBOLIC = {"XY": 2, "CM": 2}


def mega(s):
    v = 0
    for ch in s:
        if "0" <= ch <= "9":
            d = ord(ch) - 48
        elif "A" <= ch <= "Z":
            d = ord(ch) - 65 + 10
        elif "a" <= ch <= "z":
            d = ord(ch) - 97 + 10
        else:
            return None
        v = v * 36 + d
    return v


# ------------------------------------------------------------------- driver

def load_driver(dll_path):
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
    base = next((TABLE_RVA - va + rp for va, vs, rp, rs in secs
                 if va <= TABLE_RVA < va + max(vs, rs)), None)
    if base is None:
        raise SystemExit("dispatch table RVA outside every section")
    out = {}
    for i in range(ENTRIES):
        raw = d[base + i * STRIDE: base + (i + 1) * STRIDE]
        letter = raw[15]
        handler = struct.unpack_from("<I", raw, 1)[0] & 0xFFFFFF
        argc = struct.unpack_from("<i", raw, 16)[0]
        if not (0x20 <= letter < 0x7F):
            continue
        widths = []
        for b in raw[20:38]:
            if b == 0:
                break
            widths.append(2 if b in (0xFF, 0xFE) else b)
        lvl = next((l for l, lo, hi in RUNS if lo <= i <= hi), None)
        if lvl is not None:
            out.setdefault((lvl, chr(letter)), (widths, argc))
    return out


# --------------------------------------------------------------- doc blocks

LABEL = re.compile(r"^\s{2,}([A-Z][a-z]+):\s+(\S.*?)\s*$")
FIELD = re.compile(r"([A-Za-z_][A-Za-z_0-9\[\]]*)\s*:\s*([0-9]+|XY|CM)\b")


def blocks(path):
    """Command blocks, with Arguments: continued across indented lines."""
    out, cur, pending = [], None, None
    for n, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
        m = LABEL.match(line)
        if m:
            key, val = m.group(1).lower(), m.group(2)
            if key == "command":
                if cur:
                    out.append(cur)
                cur = {"cmd": val, "line": n, "file": path.name}
                pending = None
            elif cur is not None and key in ("arguments", "format", "example",
                                             "widths"):
                cur.setdefault(key, val)
                pending = key if key == "arguments" else None
            else:
                pending = None
            continue
        # An indented continuation line extends the Arguments: list.
        if (pending == "arguments" and cur and line.strip()
                and line.startswith("     ") and FIELD.search(line)
                and not LABEL.match(line)):
            cur["arguments"] += " " + line.strip()
            continue
        if not line.strip():
            pending = None
    if cur:
        out.append(cur)
    return out


def parse_cmd(spelling):
    """(level, letter) from a '|1I'-style spelling, or None if not literal."""
    s = spelling.strip().strip("`").split()[0]
    if not s.startswith("|") or "<" in s:
        return None
    body = s[1:]
    if len(body) == 2 and body[0] in "123":
        return int(body[0]), body[1]
    if len(body) == 1:
        return 0, body
    return None


def widths_of(fields):
    return [SYMBOLIC.get(w, None) or int(w) for _, w in fields]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dll", nargs="?", type=pathlib.Path)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    driver = load_driver(a.dll) if a.dll else {}
    defects, skips = [], []
    no_tail = set()          # commands with no trailing string
    checked = {"width": 0, "decode": 0, "split": 0, "total": 0}
    nblocks = 0

    for name in CHAPTERS:
        p = SPEC / name
        if not p.exists():
            defects.append("%s: chapter missing" % name)
            continue
        for b in blocks(p):
            nblocks += 1
            where = "%s:%d %s" % (b["file"], b["line"], b["cmd"].split()[0])
            args = b.get("arguments", "")
            if "<" in b["cmd"] or "letter" in b["cmd"]:
                skips.append((where, "template block, not a real command"))
                continue
            if not args:
                skips.append((where, "no Arguments: line"))
                continue
            if "..." in args or "…" in args:
                skips.append((where, "elided field list"))
                continue
            # A repeat group ("[x0:2 ... y3:2] x nsegs") is variable-length;
            # counting the group once would understate it and expanding it
            # would need the count, which is only known at run time.
            if "×" in args or re.search(r"\]\s*x\s", args):
                skips.append((where, "variable repeat group"))
                continue

            fields = FIELD.findall(args)
            # Some blocks give the widths on their own line instead of
            # inline -- "Widths: 1,1,4,2,2,2,2,2,2,2,6".
            if not fields and b.get("widths"):
                ws = re.findall(r"\d+", b["widths"])
                if ws:
                    fields = [("f%d" % i, w) for i, w in enumerate(ws)]
            optional = set(re.findall(r"\[([A-Za-z_][A-Za-z_0-9]*)\s*:", args))
            # A bare word with no width is a variable-length string tail.
            # It may carry a parenthesised gloss ("path (free-form text)"),
            # so anchoring at end of line misses it.
            tail = re.search(r"(?:^|\s)(filename|text|url|name|path|string|data|"
                             r"varname|params?)\b(?!\s*:)", args, re.I)
            widths = widths_of(fields)
            fixed = sum(w for (fn, _), w in zip(fields, widths) if fn not in optional)
            full = sum(widths)

            # --- 4. a stated character total ------------------------------
            fmt = b.get("format", "")
            mtot = re.search(r"<\s*(\d+)\s*chars?\b", fmt)
            if mtot:
                if int(mtot.group(1)) != full:
                    defects.append("%s: Format: says %s chars of params, "
                                   "Arguments: sums to %d"
                                   % (where, mtot.group(1), full))
                else:
                    checked["total"] += 1

            # --- 1/2. the Example -----------------------------------------
            ex = b.get("example", "")
            m = re.match(r"^!\|(\S+?)\|", ex)
            if not m:
                skips.append((where, "no parsable Example:"))
            elif tail:
                skips.append((where, "Example: carries a variable-length string"))
            else:
                body = m.group(1)
                prefix = b["cmd"].strip().strip("`").split()[0][1:]
                if not body.startswith(prefix):
                    defects.append("%s: Example: '%s' does not start with the "
                                   "command spelling" % (where, body))
                else:
                    payload = body[len(prefix):]
                    if not (fixed <= len(payload) <= full):
                        want = str(full) if fixed == full else "%d..%d" % (fixed, full)
                        defects.append("%s: Example: payload '%s' is %d char(s), "
                                       "Arguments: requires %s"
                                       % (where, payload, len(payload), want))
                    else:
                        checked["width"] += 1
                        gloss = ex[m.end():]
                        claims = dict(re.findall(
                            r"\b([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(-?\d+)", gloss))
                        # Many glosses use coordinate tuples instead of
                        # name=value -- "(0,100) to (200,150)".  Fall back to
                        # matching the integers positionally, but only when
                        # there are exactly as many as there are fields, so a
                        # stray number in prose cannot create a false hit.
                        if not claims:
                            nums = re.findall(r"-?\d+", gloss)
                            if len(nums) == len(fields) and fields:
                                claims = {fn: v for (fn, _), v in zip(fields, nums)}
                        if not claims:
                            skips.append((where, "Example: states no decoded values"))
                        else:
                            off, bad = 0, False
                            for (fn, _), w in zip(fields, widths):
                                if off + w > len(payload):
                                    break
                                raw = payload[off:off + w]
                                off += w
                                if fn in claims:
                                    got = mega(raw)
                                    if got is not None and got != int(claims[fn]):
                                        defects.append(
                                            "%s: Example: field '%s' is '%s' = %d, "
                                            "comment says %s"
                                            % (where, fn, raw, got, claims[fn]))
                                        bad = True
                            if not bad:
                                checked["decode"] += 1

            # --- 3. the field split against the record --------------------
            key = parse_cmd(b["cmd"])
            if key is not None and not tail:
                no_tail.add(key)
            if not driver:
                continue
            if key is None:
                skips.append((where, "command spelling not literal"))
            elif key not in driver:
                skips.append((where, "no dispatch entry (RIPlib-original?)"))
            else:
                dwidths, argc = driver[key]
                if argc < 0:
                    skips.append((where, "variable-length record (argc %d)" % argc))
                elif not dwidths:
                    skips.append((where, "record declares no argument types"))
                elif widths == dwidths:
                    checked["split"] += 1
                elif sum(widths) == sum(dwidths):
                    defects.append("%s: same total (%d) but a different field "
                                   "split -- doc %s, record %s"
                                   % (where, sum(widths),
                                      "/".join(map(str, widths)),
                                      "/".join(map(str, dwidths))))
                else:
                    defects.append("%s: doc sums to %d %s, record sums to %d %s"
                                   % (where, sum(widths), "/".join(map(str, widths)),
                                      sum(dwidths), "/".join(map(str, dwidths))))

    # --- 5. LITERALS ------------------------------------------------------
    # Command blocks are not the only place a wire example appears.  Segment
    # 1's worked examples sit in free prose and carried the same '4Q' = 170
    # mistake as segment 2's '|L' block -- one was found by the block check
    # and the other only by hand.  So sweep every literal '!|...|' in every
    # chapter and length-check it against the record for its command.
    # A literal can only be length-checked when the command has no trailing
    # string -- '!|1I0A0F000000MYICON|' is correct and much longer than the
    # record.  The blocks above already say which commands those are, so
    # reuse that rather than guessing from the literal.
    PLACEHOLDER = re.compile(r"^(cmd\d*|params?|letter|opcode|args?)$", re.I)
    nlit = 0
    if driver:
        for p in sorted(SPEC.glob("*.md")):
            for n, line in enumerate(p.read_text(encoding="utf-8").split("\n"), 1):
                for lit in re.findall(r"!\|([A-Za-z0-9][^|<>\s]*)\|", line):
                    if PLACEHOLDER.match(lit):
                        continue
                    key = parse_cmd("|" + lit[:2]) or parse_cmd("|" + lit[:1])
                    if key is None or key not in driver or key not in no_tail:
                        continue
                    dwidths, argc = driver[key]
                    if argc < 0 or not dwidths:
                        continue
                    prefix = 2 if (len(lit) > 1 and lit[0] in "123") else 1
                    payload = lit[prefix:]
                    # Only fixed-width commands can be length-checked: a
                    # string tail or an optional field makes any length legal.
                    if not payload or not payload.isalnum():
                        continue
                    # Only OVER-length is unambiguous.  A shorter literal may
                    # legitimately omit optional trailing fields -- '|Y'
                    # without its flags, '|2P' without flags and reserved --
                    # and the record alone cannot say which fields those are.
                    # Over-length is what the '|=' and '|L' examples were.
                    want = sum(dwidths)
                    if len(payload) <= want:
                        nlit += 1
                    else:
                        defects.append("%s:%d: literal '!|%s|' carries %d "
                                       "char(s); the record for that command "
                                       "allows at most %d"
                                       % (p.name, n, lit, len(payload), want))
    print("scanned %d command block(s) across %d chapter(s)" % (nblocks, len(CHAPTERS)))
    if driver:
        print("literal wire examples length-checked: %d" % nlit)
    print("verified: width %d   decode %d   split %d   stated-total %d"
          % (checked["width"], checked["decode"], checked["split"], checked["total"]))
    print("skipped (not mechanically checkable): %d" % len(skips))
    if a.verbose:
        for w, why in skips:
            print("    - %-46s %s" % (w, why))
    if defects:
        print("\ncheck-spec-examples: %d defect(s)\n" % len(defects))
        for x in defects:
            print("  ! " + x)
        return 1
    print("OK: no spec block defects.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
