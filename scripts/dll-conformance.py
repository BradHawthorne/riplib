#!/usr/bin/env python3
"""dll-conformance.py -- check RIPlib's parser against the driver's record.

The dispatch record is machine-readable and complete, which makes several
whole CLASSES of defect checkable rather than findable one at a time.  Every
class below was first hit as a single bug and only afterwards turned into a
check; each one then found more of the same:

  read offsets   '|3G' read a URL at offset 0 against a fixed prefix of 8,
                 '|1M' read two 1-digit flags as one 2-digit hotkey, '|2P'
                 took the high half of a mega4.  -> D-14, D-15, D-17, D-19
  length gates   '|1g' gated 12 against a record of 14, '|1i' 12 against 24,
                 the Switch* family 1 against 3.  Checking all of them at
                 once then found fifteen more.  -> D-20
  radix          '|d' decoded base-64 payloads with the case-insensitive
                 base-36 helper and corrupted 61 of TUNNEL.RIP's 65 palette
                 entries.  Silent: nothing crashes, the colours are wrong.
                 -> D-12, D-23
  coverage       "zero disagreements" meant nothing until the set it was
                 measured over was itself measured: an entire command level
                 sat outside it.  -> D-17, D-18

Two rules the checks encode, both learned by getting them wrong:

  * The record types only the NUMERIC argument array.  A trailing string is
    passed out-of-band, so it never appears in the record -- and the record's
    fixed width is therefore exactly the offset that string begins at.

  * An overloaded command stores its extra signatures as CONTINUATION rows
    whose letter byte is 0x00, identified only by sharing the named entry's
    handler pointer.  Filtering rows on a printable letter drops them, which
    makes '|h' look like one signature instead of six.

Deliberate tolerances are listed by name rather than silently passed: where
shipped content contradicts the record, content wins, and TOLERATED says so.

Exit status is 1 if any check reports a defect, so this can gate a build.

Usage:
    python scripts/dll-conformance.py <path>/Ripscrip.dll [--verbose]
"""
from dll_record import dispatch_level
import argparse
import collections
import contextlib
import hashlib
import html
import importlib.util
import io
import os
import re
import struct
import sys
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src", "ripscrip.c")
SRC2 = os.path.join(ROOT, "src", "ripscrip2.c")
HDR2 = os.path.join(ROOT, "include", "ripscrip2.h")

# Tolerances justified against shipped scenes rather than against the record.
# See 14-divergence-register.md 14.3.3.
TOLERATED_GATES = {
    "|k": "133 uses: 132 are 2 chars, N2_BUSI.RIP sends 1",
    "|=": "116 uses: 107 are 8, 2 are 7, 7 are 4 -- reads progressively",
}
TOLERATED_READS = {
    ("|k", 0, 1): "the same single-character |k tolerance",
}


# ── dispatch record ──────────────────────────────────────────────────────
def load(path):
    d = open(path, "rb").read()
    md5 = hashlib.md5(d).hexdigest()
    if md5 != "bade8b1f4e467ac7ad4edb2639738d4c":
        print("WARNING: unexpected image (md5 %s)" % md5, file=sys.stderr)
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    coff = pe + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    optsz = struct.unpack_from("<H", d, coff + 16)[0]
    secs = []
    so = coff + 20 + optsz
    for i in range(nsec):
        o = so + i * 40
        vs, va, rs, rp = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, vs, rp, rs))
    return d, secs



def _blocks(src_text):
    """(L3, L1, L0) line ranges, derived from structural markers.

    Hardcoding these is a trap: any edit above a block shifts every case label
    inside it, and the stale range then brackets the wrong code silently.  A
    variable-expansion helper added ~40 lines above Level 3 and pushed '|3e'
    out of its own window.
    """
    lines = src_text.split("\n")
    mark = {}
    for i, l in enumerate(lines, 1):
        for key, pat in (("l9", r"/\* Level 9 commands \*/"), ("l3", r"if \(s->is_level3\)"),
                         ("l2", r"if \(s->is_level2\)"),
                         ("l1", r"if \(s->is_level1\)"),
                         ("l0", r"/\* Level 0 commands \*/")):
            if key not in mark and re.search(pat, l):
                mark[key] = i
    if not {"l3", "l2", "l1", "l0"} <= set(mark):
        raise SystemExit("cannot locate switch blocks: found %s" % sorted(mark))
    return (((mark["l9"], mark["l3"]),) if "l9" in mark else ()) + ((mark["l3"], mark["l2"]),
            (mark["l1"], mark["l0"]),
            (mark["l0"], 10 ** 9))


def dispatch_rows(d, secs):
    """Retain every row, including ESC, duplicate keys and continuations."""
    def rva2off(r):
        for va, vs, rp, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + rp

    base = rva2off(0x080820)
    if base is None or base + 129 * 40 > len(d):
        raise SystemExit("incomplete dispatch table: expected 129 records")
    rows = []
    for i in range(129):
        raw = d[base + i * 40:base + (i + 1) * 40]
        types = list(raw[20:38])
        if 0 in types:
            types = types[:types.index(0)]
        rows.append(dict(slot=i, level=dispatch_level(raw), letter=raw[15],
                         handler=struct.unpack_from("<I", raw, 1)[0],
                         argc=struct.unpack_from("<i", raw, 16)[0],
                         radix=struct.unpack_from("<H", raw, 38)[0] & 3,
                         types=types))
    return rows


def read_table(d, secs):
    by_handler = {}
    named = {}
    meta = {}
    for row in dispatch_rows(d, secs):
        i, letter, argc = row["slot"], row["letter"], row["argc"]
        rva, radix = row["handler"], row["radix"]
        widths = [2 if b in (0xFF, 0xFE) else b for b in row["types"]]
        if argc > 0 and widths:
            by_handler.setdefault(rva, []).append(widths)
        if letter != 0:
            key = (row["level"], chr(letter))
            named.setdefault(key, rva)
            meta.setdefault(key, (i, argc, radix))
    sigs = {k: by_handler.get(rva, []) for k, rva in named.items()}
    return sigs, meta


def spell(key):
    level, ch = key
    return "|%s%s" % (level or "", ch if ch.isprintable() else "<0x%02X>" % ord(ch))


def check_dispatch_accounting(rows, meta, verbose):
    """Every nonzero letter must survive indexing; every zero row has an owner."""
    named = [r for r in rows if r["letter"]]
    keys = {(r["level"], chr(r["letter"])) for r in named}
    bad = 0
    for key in sorted(keys ^ set(meta)):
        print("  ! %s missing or extraneous in indexed dispatch table" % spell(key))
        bad += 1
    for row in rows:
        if row["letter"] == 0 and not any(
                r["handler"] == row["handler"] and r["level"] == row["level"]
                for r in named):
            print("  ! continuation slot %d has no named handler" % row["slot"])
            bad += 1
    print("    %d rows: %d named rows, %d continuations, %d distinct keys, "
          "%d duplicate named row(s)" %
          (len(rows), len(named), len(rows) - len(named), len(keys), len(named) - len(keys)))
    return bad


# ── RIPlib source ────────────────────────────────────────────────────────
QUOTED = r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])*'"
C_LEXEME = re.compile(QUOTED + r"|/\*.*?\*/|//[^\n]*", re.S)
BODY_TOKEN = re.compile(
    r"\bcase\s+('(?:\\.|[^'\\])*'|0x[0-9a-fA-F]+|[0-9]+)\s*:"
    r"|\bdefault\s*:|\bcase\b|"
    + QUOTED + r"|[{}]")


def handler_bodies(lines):
    """Read all direct cases in the three command switches, without a cap.

    C brace depth, not indentation or an early break, determines the end.
    Preserve line numbers while removing comments; quoted braces and nested
    switch labels cannot terminate a command.  Fail closed on an unfamiliar
    dispatch layout instead of silently dropping coverage (D-29).
    """
    source = "\n".join(lines)
    blocks = _blocks(source)
    clean = C_LEXEME.sub(
        lambda m: re.sub(r"[^\n]", " ", m.group())
        if m.group().startswith(("/*", "//")) else m.group(), source)
    switches = list(re.finditer(
        r"^\s*switch\s*\(s->cmd_char\)\s*\{", clean, re.M))
    if len(switches) != len(blocks):
        raise SystemExit("expected %d command switches, found %d" % (len(blocks),len(switches)))
    seen_levels = set()
    for switch in switches:
        line = clean.count("\n", 0, switch.end()) + 1
        levels = [lvl for lvl, (start, end) in zip((9, 3, 1, 0) if len(blocks)==4 else (3, 1, 0), blocks)
                  if start < line < end]
        if len(levels) != 1 or levels[0] in seen_levels:
            raise SystemExit("cannot classify command switch at line %d" % line)
        lvl = levels[0]
        seen_levels.add(lvl)
        depth, current = 1, None
        seen_commands = set()
        for token in BODY_TOKEN.finditer(clean, switch.end()):
            value = token.group()
            if value == "{":
                depth += 1
            elif value == "}":
                depth -= 1
            boundary = depth == 0 or (depth == 1 and
                        (token.group(1) is not None or value.startswith("default")))
            if boundary and current is not None:
                ch, start = current
                yield lvl, ch, clean.count("\n", 0, start) + 1, clean[start:token.start()]
                current = None
            if depth == 0:
                break
            if depth == 1 and value == "case":
                raise SystemExit("unsupported command case at line %d" %
                                 (clean.count("\n", 0, token.start()) + 1))
            if depth == 1 and token.group(1) is not None:
                literal = token.group(1)
                if literal.startswith("'") and len(literal) != 3:
                    raise SystemExit("unsupported command character %s" % literal)
                ch = literal[1] if literal.startswith("'") else chr(int(literal, 0))
                if ch in seen_commands:
                    raise SystemExit("duplicate command case %r at level %d" % (ch, lvl))
                seen_commands.add(ch)
                current = ch, token.start()
        else:
            raise SystemExit("unterminated command switch at line %d" % line)


def boundaries(widths):
    off, out = 0, {}
    for w in widths:
        out[off] = w
        off += w
    return out


WIDTH = {"mega_digit": 1, "mega1": 1, "mega2": 2, "mega3": 3, "mega4": 4,
         "mega2_64": 2, "mega4_64": 4}
READ = re.compile(
    r"\b(mega_digit|mega1|mega2_64|mega4_64|mega2|mega3|mega4)"
    r"\s*\(\s*p\s*(?:\+\s*(\d+)\s*)?\)"
    r"|\b(mega_digit)\s*\(\s*p\s*\[\s*(\d+)\s*\]\s*\)")
GATE = re.compile(r"\bif\s*\(\s*len\s*(>=|>)\s*(\d+)")
# A trailing string is a `p + N` pointer, not a decode, so the offset check
# above cannot see it.  It needs its own check -- and did not have one until
# '|1A' and '|1b' were found reading their filenames four characters early,
# by which point '|1b' had been asking the host for "0000back.bmp" in all 36
# of its corpus appearances.
STR_BASE = re.compile(
    r"(?:fname|filename|name|nm|path|text|url|u|fn)\w*\s*=\s*p\s*\+\s*(\d+)")
STR_LEN = re.compile(r"=\s*len\s*-\s*(\d+)\s*;")
MULTI = re.compile(r"else\s+if\s*\(\s*len\s*(==|>=)\s*\d+")
B64 = re.compile(r"_64\s*\(")
B36 = re.compile(r"\bmega(?:_digit|1|2|3|4)\s*\(")


def check_offsets(sigs, lines, verbose):
    """Every field read must land on a boundary the record defines."""
    bad_total = examined = reads_total = 0
    for lvl, ch, _, txt in handler_bodies(lines):
        key = (lvl, ch)
        if key not in sigs or not sigs[key]:
            continue
        reads = set()
        for m in READ.finditer(txt):
            if m.group(1):
                reads.add((int(m.group(2) or 0), WIDTH[m.group(1)]))
            else:
                reads.add((int(m.group(4)), WIDTH[m.group(3)]))
        if not reads:
            continue
        examined += 1
        reads_total += len(reads)
        tag = "|%s%s" % (lvl or "", ch)
        allb = [boundaries(w) for w in sigs[key]]
        bad = [(o, w) for o, w in sorted(reads)
               if not any(o in b and b[o] == w for b in allb)
               and (tag, o, w) not in TOLERATED_READS]
        if bad:
            bad_total += 1
            print("  ! %-5s reads %s not in any record signature"
                  % (tag, " ".join("%d:%d" % x for x in bad)))
    if verbose:
        print("    %d commands, %d reads" % (examined, reads_total))
    return bad_total


def check_gates(sigs, lines, verbose):
    """A gate must admit exactly the record's fixed total."""
    bad = ok = multi = tol = nogate = 0
    for lvl, ch, _, txt in handler_bodies(lines):
        key = (lvl, ch)
        if key not in sigs or not sigs[key]:
            continue
        tag = "|%s%s" % (lvl or "", ch)
        g = GATE.search(txt)
        if not g:
            nogate += 1
            continue
        want = min(sum(w) for w in sigs[key])
        admits = int(g.group(2)) + (1 if g.group(1) == ">" else 0)
        # A command with a trailing string may legitimately require ONE more
        # than the fixed prefix, where an empty string makes it meaningless --
        # '|1W' cannot cache under no name, '|1R' cannot request no file.
        # Where the string is optional (a mouse region with no host command,
        # a button with no label) the gate is the prefix exactly.  Both are
        # accepted; anything else is not.
        has_tail = bool(STR_BASE.search(txt) or STR_LEN.search(txt))
        if admits == want or (has_tail and admits == want + 1):
            ok += 1
        elif tag in TOLERATED_GATES:
            tol += 1
        elif MULTI.search(txt):
            multi += 1
        else:
            bad += 1
            print("  ! %-5s gate admits %d, record needs %d"
                  % (tag, admits, want))
    if verbose:
        print("    %d match, %d multi-length, %d tolerated, %d without a "
              "numeric gate" % (ok, multi, tol, nogate))
    return bad


def check_string_tails(sigs, lines, verbose):
    """A trailing string starts at the record's fixed width, exactly.

    The record types only the numeric argument array; a string is passed
    out-of-band and never appears in it, so the record's total IS the string's
    offset.  Reading it early prefixes the value with reserved digits, which
    is silent -- a filename that no host can match, a URL pointing elsewhere.
    """
    bad = checked = 0
    for lvl, ch, _, txt in handler_bodies(lines):
        key = (lvl, ch)
        if key not in sigs or not sigs[key]:
            continue
        offs = {int(x) for x in STR_BASE.findall(txt)}
        offs |= {int(x) for x in STR_LEN.findall(txt)}
        if not offs:
            continue
        want = min(sum(w) for w in sigs[key])
        tag = "|%s%s" % (lvl or "", ch)
        for o in sorted(offs):
            checked += 1
            if o != want:
                bad += 1
                print("  ! %-5s string read at %d, record's fixed width is %d"
                      % (tag, o, want))
    if verbose:
        print("    %d string offsets checked" % checked)
    return bad


def check_radix(sigs, meta, lines, verbose):
    """Commands with a fixed radix must use the matching decoder."""
    bad = checked = 0
    for lvl, ch, _, txt in handler_bodies(lines):
        key = (lvl, ch)
        if key not in meta:
            continue
        radix = meta[key][2]
        if radix not in (1, 2):
            continue
        has64, has36 = bool(B64.search(txt)), bool(B36.search(txt))
        if not (has64 or has36):
            continue
        checked += 1
        tag = "|%s%s" % (lvl or "", ch)
        if radix == 2 and (not has64 or has36):
            bad += 1
            print("  ! %-5s record says base 64; code uses base 36" % tag)
        elif radix == 1 and has64:
            bad += 1
            print("  ! %-5s record says base 36; code uses base 64" % tag)
    if verbose:
        print("    %d fixed-radix commands checked" % checked)
    return bad


def level2_defines(text):
    """Printable and numeric opcodes, including ESC; never drop a define."""
    out = {}
    for name, char, number in re.findall(
            r"#define\s+(RIP2_CMD_\w+)\s+(?:'(.)'|(0x[0-9a-fA-F]+|[0-9]+))", text):
        out[name] = char if char else chr(int(number, 0))
    return out


def check_coverage(sigs, meta, lines, verbose):
    """Account for every dispatch entry, so a clean result has a known scope."""
    impl = set()
    for lvl, ch, _, _ in handler_bodies(lines):
        impl.add((lvl, ch))
    print("    %d complete source handlers (levels 0/1/3/9; no line cap)" % len(impl))
    print("    level 2 bodies and called helpers are outside these source checks")
    l2 = set()
    if os.path.exists(HDR2) and os.path.exists(SRC2):
        names = level2_defines(Path(HDR2).read_text(encoding="latin-1"))
        body2 = open(SRC2, encoding="latin-1").read()
        for m in re.finditer(r"case\s+(RIP2_CMD_\w+)\s*:", body2):
            if m.group(1) in names:
                l2.add(names[m.group(1)])
    buckets = collections.Counter()
    missing = []
    for key, (slot, argc, _) in sorted(meta.items()):
        tag = spell(key)
        if key[0] == 2 and key[1] in l2:
            buckets["level 2 (ripscrip2.c)"] += 1
        elif key in impl:
            buckets["implemented in ripscrip.c"] += 1
        else:
            buckets["NOT implemented"] += 1
            missing.append(tag)
    if verbose or missing:
        for k, n in sorted(buckets.items(), key=lambda kv: -kv[1]):
            print("    %-28s %3d" % (k, n))
    if missing:
        print("    not implemented: %s" % " ".join(missing))
    return 0        # informational: an unimplemented command is not a defect


def source_inventory(lines):
    """Map all direct command cases to source paths and one-based lines."""
    inventory = {(lvl, ch): ("src/ripscrip.c", line)
                 for lvl, ch, line, _ in handler_bodies(lines)}
    names = level2_defines(Path(HDR2).read_text(encoding="latin-1"))
    source = Path(SRC2).read_text(encoding="latin-1")
    clean = C_LEXEME.sub(lambda m: re.sub(r"[^\n]", " ", m.group())
                        if m.group().startswith(("/*", "//")) else m.group(), source)
    switch = re.search(r"\bswitch\s*\(cmd\)\s*\{", clean)
    if not switch:
        raise SystemExit("cannot locate Level 2 command switch")
    depth = 1
    tokens = re.compile(r"\bcase\s+(RIP2_CMD_\w+)\s*:|\bcase\b|" + QUOTED + r"|[{}]")
    for token in tokens.finditer(clean, switch.end()):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
        if depth == 0:
            break
        if depth == 1 and token.group() == "case":
            raise SystemExit("unrecognised Level 2 command label")
        if depth == 1 and token.group(1):
            name = token.group(1)
            if name not in names or (2, names[name]) in inventory:
                raise SystemExit("unknown or duplicate Level 2 command: " + name)
            inventory[(2, names[name])] = ("src/ripscrip2.c", clean.count("\n", 0, token.start()) + 1)
    else:
        raise SystemExit("unterminated Level 2 command switch")
    return inventory


def crosswalk_markdown(rows, inventory, image, corpus, checks, reference=None, revision=None):
    """A row-complete inventory; presence never implies behavioural parity."""
    named = collections.defaultdict(list)
    for row in rows:
        if row["letter"]:
            named[(row["level"], chr(row["letter"]))].append(row)
    missing, extra = set(named) - set(inventory), set(inventory) - set(named)
    reference_entries, reference_by_key = [], {}
    reference_url = ""
    if reference:
        spec = importlib.util.spec_from_file_location("ref_compare", Path(HERE) / "ref-compare.py")
        compare = importlib.util.module_from_spec(spec)
        # This repository tracks historical .pyc files; don't rewrite them.
        old_bytecode = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True
            spec.loader.exec_module(compare)
        finally:
            sys.dont_write_bytecode = old_bytecode
        reference_entries = compare.reference_rows(reference)
        for entry in reference_entries:
            if entry["key"] is not None:
                if entry["key"] in reference_by_key:
                    raise SystemExit("duplicate reference command: %r" % (entry["key"],))
                reference_by_key[entry["key"]] = entry
        if revision:
            if not re.fullmatch(r"[0-9a-f]{40}", revision):
                raise SystemExit("reference revision must be a full 40-digit Git commit")
            reference_url = ("https://github.com/bbs-land/remote-imaging-protocol/blob/" + revision +
                             "/version/3.0/ripscrip/9.0-command-reference.md")

    def reference_status(key):
        entry = reference_by_key.get(key)
        if not entry:
            return "Not listed by opcode"
        args = entry["arguments"]
        widths = compare.widths_from(args)
        if "..." in args or "…" in args:
            return "Elided / variable list; not compared"
        if key not in named:
            return "No DLL opcode to compare"
        candidates = [r for r in rows if r["level"] == key[0] and
                      any(r["handler"] == n["handler"] for n in named[key])]
        if not widths or not any(r["argc"] > 0 and r["types"] for r in candidates):
            return "No comparable fixed numeric list"
        for row in candidates:
            dw = ["n" if t in (254, 255) else str(t) for t in row["types"]]
            if row["argc"] > 0 and len(dw) == len(widths) and all(
                    compare.compatible(a, b) for a, b in zip(widths, dw)):
                return "Numeric shape agrees"
        return "**Numeric shape differs**"

    def reference_cell(key):
        entry = reference_by_key.get(key)
        if not entry:
            return "Not listed by opcode"
        label = cell(entry["symbol"])
        if reference_url:
            label = "[%s](%s#L%d)" % (label, reference_url, entry["line"])
        return label + "; " + code(entry["arguments"]) + "; " + reference_status(key)
    uses = collections.Counter()
    files = []
    if corpus:
        files = sorted(p for p in Path(corpus).rglob("*") if p.suffix.lower() == ".rip")
        if not files:
            raise SystemExit("no RIP corpus scenes found: " + corpus)
        # Match corpus-scan.py's lexical census, not a parser or execution trace.
        for path in files:
            for line in path.read_text(encoding="latin-1").splitlines():
                if line.startswith("!|"):
                    for chunk in line[2:].split("|"):
                        if chunk:
                            key = (int(chunk[0]), chunk[1]) if chunk[0] in "123" and len(chunk) > 1 else (0, chunk[0])
                            uses[key] += 1

    def cell(value):
        return html.escape(str(value)).replace("|", "&#124;")

    def code(value):
        return "<code>%s</code>" % cell(value)

    def location(key):
        if key not in inventory:
            return "**Missing handler**"
        path, line = inventory[key]
        source_line = Path(ROOT, path).read_text(encoding="latin-1").splitlines()[line - 1]
        name = re.search(r"\b(?:RIP2_CMD_|RIP_)[A-Za-z0-9_]+", source_line)
        return "[Present%s](../%s#L%d)" % (": " + name.group() if name else "", path, line)

    out = ["# RIPlib / RIPtel / bbs-land command crosswalk" if reference else
           "# RIPlib versus RIPtel command crosswalk", "",
           "Generated by `scripts/dll-conformance.py <RIPSCRIP.DLL> -v --corpus <RIPtel-dir> "
           "--crosswalk docs/riptel-crosswalk.md`" +
           (" with `--reference <9.0-command-reference.md> --reference-revision %s`." % revision if reference else "."), "",
           "Scope: the RIPSCRIP.DLL shipped with the local RIPtel 3.1 installation, "
           "not the entire RIPtel terminal application. The DLL self-reports 3.00.04. "
           "Its MD5 is `%s`; %d bytes." % (hashlib.md5(image).hexdigest(), len(image)), "",
           "Level assignment reads each record’s literal prefix at bytes 5–14; "
           "no slot-range inference is used. See [binary provenance](spec/13-dll-command-table.md).", "",
           "**Present means a source handler exists. It does not mean equivalent rendering, "
           "parameter handling, host behavior, or test coverage.** No live RIPtel-versus-RIPlib "
           "pixel or callback comparison was performed.", "",
           "[Audit conclusions and upstream conflict triage](crosswalk-audit.md).", "",
           "| Measure | Count |", "|---|---:|",
           "| DLL dispatch rows, none omitted | %d |" % len(rows),
           "| Distinct nonzero command keys, including ESC | %d |" % len(named),
           "| Continuation signature rows | %d |" % sum(r["letter"] == 0 for r in rows),
           "| Additional named rows with a duplicate key | %d |" % (sum(map(len, named.values())) - len(named)),
           "| DLL command keys with a RIPlib handler | %d |" % len(set(named) & set(inventory)),
           "| DLL command keys missing a RIPlib handler | %d |" % len(missing),
           "| RIPlib command keys absent from this DLL | %d |" % len(extra), "",
           "## Findings and limits", "",
           "- Missing source handlers: %s." % (", ".join(code(spell(k)) for k in sorted(missing)) or "none"),
           "- Command identities and behavioral fixes are evaluated separately from this "
           "inventory. See the [audit report](crosswalk-audit.md) and "
           "[D-31 through D-37](spec/12-dll-provenance.md) for driver geometry fixtures, "
           "host-service contracts, fill and move semantics, and their validation limits.",
           "- The former duplicate `|3D` finding is retracted: slot 122 is `|3D` "
           "(delay), slot 125 is `|9D` (host expression). Literal prefix recovery "
           "corrected all five service keys. [D-34](spec/12-dll-provenance.md).",
           "- Host-mediated operations and deliberate extensions/tolerances are documented "
           "in the [divergence register](spec/14-divergence-register.md). "
           "Handler coverage does not close those separate behavioral questions.",
           "- The static offset/gate/radix checks cover subsets of levels 0/1/3/9. "
           "Level 2 bodies, helper implementations, computed offsets, and pixel parity "
           "are outside those checks. The tables below inventory those handlers without "
           "claiming they passed these checks.", "",
           "## Current mechanical checks", "", "```text", checks.rstrip(), "```", ""]
    if reference:
        out += ["## bbs-land reference audit", "",
                "Reference commit: `%s`. File SHA-256: `%s`." %
                (revision or "not supplied", hashlib.sha256(reference.read_bytes()).hexdigest()), "",
                "All %d inventory rows retained: %d keyed opcodes and %d names without "
                "assigned opcodes. The latter cannot be matched by guessing. Level 9 and "
                "the three ESC spellings remain visible." %
                (len(reference_entries), len(reference_by_key), len(reference_entries) - len(reference_by_key)), "",
                "| Reference versus DLL | Keys |", "|---|---:|"]
        for status, count in sorted(collections.Counter(reference_status(k) for k in reference_by_key).items()):
            out.append("| %s | %d |" % (status, count))
        out += ["", "The comparison checks numeric field shape, allowing `XY`/`CM` "
                "to match literal width 2 at default settings. Bare trailing text is "
                "outside the numeric record; agreement does not validate its placement "
                "or meaning. Variable/elided lists are explicitly unverified. Multiple "
                "DLL signatures are considered. This is documentation agreement, not "
                "an executable bbs-land implementation test.", ""]
    if corpus:
        out += ["Corpus census: %d scenes, %d lexical command instances, %d distinct "
                "keys. Counts split raw `!|` lines like `corpus-scan.py`; they do not "
                "evaluate preprocessing, escapes, or execution. Duplicate DLL rows repeat "
                "the same opcode count and must not be summed." % (len(files), sum(uses.values()), len(uses)), ""]
    else:
        out += ["Corpus counts were not measured.", ""]
    out += ["## Every DLL dispatch row", "",
            "Types are raw record widths; `XY` and `color` are configurable-width fields. "
            "Zero numeric arguments can still carry text. Continuations are associated by "
            "handler address within a level, not by adjacency.", "",
            "| Slot | Command / owner | Handler RVA | argc | Types | RIPlib | Corpus uses |" + (" bbs-land |" if reference else ""),
            "|---:|---|---|---:|---|---|---:|" + ("---|" if reference else "")]
    for row in rows:
        owners = ([(row["level"], chr(row["letter"]))] if row["letter"] else
                  [k for k, group in named.items() if k[0] == row["level"] and
                   any(r["handler"] == row["handler"] for r in group)])
        label = ", ".join(code(spell(k)) for k in owners)
        if not row["letter"]:
            label += " (continuation)"
        elif len(named[owners[0]]) > 1:
            label += " (duplicate key; see finding)"
        types = ", ".join({255: "XY", 254: "color"}.get(t, str(t)) for t in row["types"]) or "—"
        out.append("| %d | %s | %s | %d | %s | %s | %s |" % (
            row["slot"], label, code("0x%06x" % (row["handler"] - 0x10000000)),
            row["argc"], types, ", ".join(location(k) for k in owners) or "Unresolved owner",
            ", ".join(str(uses[k]) for k in owners) if corpus else "unmeasured") +
            (" " + "; ".join(reference_cell(k) for k in owners) + " |" if reference else ""))
    out += ["", "## RIPlib handlers absent from this DLL", "",
            "These are additions relative to this image; absence here does not establish "
            "their status in other historical RIPscrip releases.", "",
            "| Command | RIPlib source | Corpus uses |" + (" bbs-land |" if reference else ""),
            "|---|---|---:|" + ("---|" if reference else "")]
    for key in sorted(extra):
        out.append("| %s | %s | %s |" % (code(spell(key)), location(key), str(uses[key]) if corpus else "unmeasured") +
                   (" " + reference_cell(key) + " |" if reference else ""))
    if reference:
        out += ["", "## Reference opcodes absent from this DLL", "",
                "| Command | Reference | RIPlib |", "|---|---|---|"]
        for key in sorted(set(reference_by_key) - set(named)):
            out.append("| %s | %s | %s |" % (code(spell(key)), reference_cell(key), location(key)))
        out += ["", "## Reference names without assigned opcodes", "",
                "No opcode match or implementation claim is made for these rows.", "",
                "| Name | Reference line |", "|---|---:|"]
        for entry in reference_entries:
            if entry["key"] is None:
                out.append("| %s | %d |" % (cell(entry["symbol"]), entry["line"]))
    out += ["", "## RIPlib source fingerprints", "", "SHA-256 of the audited source files, with CRLF normalized to LF:", "",
            "| File | SHA-256 |", "|---|---|"]
    for path in sorted(list((Path(ROOT) / "src").glob("*.c")) +
                       list((Path(ROOT) / "src").glob("*.h")) +
                       list((Path(ROOT) / "include").glob("*.h"))):
        out.append("| %s | %s |" % (Path(path).relative_to(ROOT).as_posix(),
                                   hashlib.sha256(Path(path).read_bytes().replace(b"\r\n", b"\n")).hexdigest()))
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--crosswalk", type=Path, help="write complete Markdown command crosswalk")
    ap.add_argument("--corpus", help="optional RIPtel directory for lexical corpus counts")
    ap.add_argument("--reference", type=Path, help="bbs-land 3.0 command reference for the crosswalk")
    ap.add_argument("--reference-revision", help="full bbs-land Git commit used for evidence links")
    a = ap.parse_args()

    d, secs = load(a.dll)
    sigs, meta = read_table(d, secs)
    rows = dispatch_rows(d, secs)
    lines = open(SRC, encoding="latin-1").read().split("\n")

    defects = 0
    reports = []
    for name, fn in (("dispatch accounting", lambda: check_dispatch_accounting(rows, meta, a.verbose)),
                     ("read offsets", lambda: check_offsets(sigs, lines, a.verbose)),
                     ("string tails", lambda: check_string_tails(sigs, lines, a.verbose)),
                     ("length gates", lambda: check_gates(sigs, lines, a.verbose)),
                     ("radix selection", lambda: check_radix(sigs, meta, lines, a.verbose)),
                     ("coverage", lambda: check_coverage(sigs, meta, lines, a.verbose))):
        with contextlib.redirect_stdout(io.StringIO()) as captured:
            print("%s:" % name)
            defects += fn()
        report = captured.getvalue()
        print(report, end="")
        reports.append(report)

    if TOLERATED_GATES or TOLERATED_READS:
        print("\ntolerances (corpus-backed, see 14-divergence-register.md):")
        for k, v in TOLERATED_GATES.items():
            print("    %-5s %s" % (k, v))

    status = "OK: no conformance defects." if not defects else "FAIL: %d defect(s)." % defects
    print("\n" + status)
    if a.crosswalk:
        a.crosswalk.write_text(crosswalk_markdown(rows, source_inventory(lines), d,
                                                a.corpus, "".join(reports) + status,
                                                a.reference, a.reference_revision), encoding="utf-8")
        print("wrote " + str(a.crosswalk))
    return 1 if defects else 0


if __name__ == "__main__":
    sys.exit(main())
