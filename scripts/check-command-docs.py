#!/usr/bin/env python3
"""check-command-docs.py -- keep the command table in the spec agreeing with
the command names in the parser.

This exists because the two drifted, repeatedly and in both directions.
docs/spec/12 recorded several opcode assignments as REFUTED and the code was
never changed to match; separately, docs/spec/10's appendix was corrected
ahead of the code and sat right for months while the parser stayed wrong.
Either way a reader trusting one of them was misled.

The check is deliberately shallow: for every Level-0 case in the parser that
names an RIP_* command, the appendix must list the same name for that
letter.  It cannot verify semantics -- only that the two documents have not
diverged on what a letter is called.

Exit status is non-zero on any mismatch, so CI can run it.

Usage:
    python scripts/check-command-docs.py
"""
import pathlib
import re
import sys

SRC = pathlib.Path("src/ripscrip.c")
APPENDIX = pathlib.Path("docs/spec/10-appendices.md")

# Appendix uses short names; the parser prefixes RIP_ and sometimes differs in
# a documented, deliberate way.  Anything here is an accepted alias, not a
# silent exemption -- each entry says why.
ALIASES = {
    # appendix column is width-limited; parser keeps the fuller name
    "SET_COORD_SIZE": "SET_COORDINATE_SIZE",
    "EXT_FONT_STYLE": "EXTENDED_FONT_STYLE",
    "ONE_DRAWING_PALETTE": "ONE_DRAWING_PALETTE",
}


def main():
    if not SRC.exists() or not APPENDIX.exists():
        print("run from the repository root", file=sys.stderr)
        return 2

    src = SRC.read_text(encoding="latin-1")
    doc = APPENDIX.read_text(encoding="utf-8")

    # parser: `    case 'X': /* RIP_NAME ...`  (Level 0 only -- four spaces)
    code = {}
    for m in re.finditer(r"^    case '(.)': /\* RIP_([A-Z0-9_]+)", src, re.M):
        code.setdefault(m.group(1), m.group(2))

    # appendix rows: `     X    NAME    args   format`
    docs = {}
    for m in re.finditer(r"^     (\S)    ([A-Z0-9_]+)\s", doc, re.M):
        docs.setdefault(m.group(1), m.group(2))

    mismatches, checked = [], 0
    for ch, name in sorted(code.items()):
        if ch not in docs:
            continue                      # not every letter is tabulated
        checked += 1
        want = docs[ch]
        if name == want:
            continue
        if ALIASES.get(want) == name or ALIASES.get(name) == want:
            continue
        mismatches.append((ch, name, want))

    print("checked %d Level-0 commands against %s" % (checked, APPENDIX))
    if not mismatches:
        print("OK: parser and appendix agree on every tabulated command name.")
        return 0

    print("\nMISMATCH between src/ripscrip.c and the appendix:\n")
    print("  %-4s %-28s %s" % ("cmd", "parser says", "appendix says"))
    for ch, name, want in mismatches:
        print("  |%-3s %-28s %s" % (ch, "RIP_" + name, want))
    print("\nOne of the two is wrong.  Fix it rather than adding an alias,")
    print("unless the difference is cosmetic -- see ALIASES in this script.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
