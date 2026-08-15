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
import argparse
import collections
import hashlib
import os
import re
import struct
import sys

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
        for key, pat in (("l3", r"if \(s->is_level3\)"),
                         ("l2", r"if \(s->is_level2\)"),
                         ("l1", r"if \(s->is_level1\)"),
                         ("l0", r"/\* Level 0 commands \*/")):
            if key not in mark and re.search(pat, l):
                mark[key] = i
    if len(mark) != 4:
        raise SystemExit("cannot locate switch blocks: found %s" % sorted(mark))
    return ((mark["l3"], mark["l2"]),
            (mark["l1"], mark["l0"]),
            (mark["l0"], 10 ** 9))

# Switch-block line ranges in ripscrip.c.  Levels 1, 2 and 3 all sit at the
# same indentation, so a handler's level is decided by which block it is in,
# not by how far it is indented.
BLOCK_L3, BLOCK_L1, BLOCK_L0 = _blocks(open(SRC, encoding="latin-1").read())


def level_of(slot):
    """Level 0 runs to slot 84.  Handler ADDRESS regions confirm it: the
    sustained move into the Level 1 region happens at slot 85, not 84."""
    return 0 if slot <= 84 else 1 if slot <= 109 else 2 if slot <= 121 else 3


def read_table(d, secs):
    def rva2off(r):
        for va, vs, rp, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + rp

    base = rva2off(0x080820)
    by_handler = {}
    named = {}
    meta = {}
    for i in range(129):
        raw = d[base + i * 40: base + (i + 1) * 40]
        letter = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        rva = struct.unpack_from("<I", raw, 1)[0]
        radix = struct.unpack_from("<H", raw, 0x26)[0] & 3
        widths = []
        for b in raw[20:38]:
            if b == 0:
                break
            widths.append(2 if b in (0xFF, 0xFE) else b)
        if argc > 0 and widths:
            by_handler.setdefault(rva, []).append(widths)
        if 0x20 <= letter < 0x7F:
            key = (level_of(i), chr(letter))
            named.setdefault(key, rva)
            meta.setdefault(key, (i, argc, radix))
    sigs = {k: by_handler.get(rva, []) for k, rva in named.items()}
    return sigs, meta


# ── RIPlib source ────────────────────────────────────────────────────────
def level_at(ln):
    if BLOCK_L3[0] < ln < BLOCK_L3[1]:
        return 3
    if BLOCK_L1[0] < ln < BLOCK_L1[1]:
        return 1
    if ln > BLOCK_L0[0]:
        return 0
    return None


def handler_bodies(lines, stop_at_break=True):
    """(level, letter, first-line-index, body text) for each case label."""
    for i, line in enumerate(lines, 1):
        m = re.match(r"\s+case '(.)':", line)
        if not m:
            continue
        lvl = level_at(i)
        if lvl is None:
            continue
        body = []
        for j in range(i - 1, min(i + 120, len(lines))):
            ln = lines[j]
            if j > i - 1 and re.match(r"\s+case '.':", ln):
                break
            body.append(ln)
            # Stop at the case's OWN terminating break, not at an early exit
            # inside a guard.  Matching any bare `break;` truncated the body
            # at the first guard, hiding everything after it -- a two-line
            # protection guard added to '|v' silently removed its `len >= 8`
            # gate from the gate check, and the only reason the other
            # eighteen guards did not do the same is that they happen to be
            # written on one line.  A checker that depends on the formatting
            # of the code it checks will lose coverage without saying so, so
            # require the break to be at the case body's own indent level.
            if stop_at_break and re.match(r"\s{8}break;\s*$", ln):
                break
        txt = re.sub(r"/\*.*?\*/", "", "\n".join(body), flags=re.S)
        txt = re.sub(r"/\*.*", "", txt, flags=re.S)
        yield lvl, m.group(1), i, txt


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
    for lvl, ch, _, txt in handler_bodies(lines, stop_at_break=False):
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


def check_coverage(sigs, meta, lines, verbose):
    """Account for every dispatch entry, so a clean result has a known scope."""
    impl = set()
    for lvl, ch, _, _ in handler_bodies(lines):
        impl.add((lvl, ch))
    l2 = set()
    if os.path.exists(HDR2) and os.path.exists(SRC2):
        names = dict(re.findall(r"#define\s+(RIP2_CMD_\w+)\s+'(.)'",
                                open(HDR2, encoding="latin-1").read()))
        body2 = open(SRC2, encoding="latin-1").read()
        for m in re.finditer(r"case\s+(RIP2_CMD_\w+)\s*:", body2):
            if m.group(1) in names:
                l2.add(names[m.group(1)])
    buckets = collections.Counter()
    missing = []
    for key, (slot, argc, _) in sorted(meta.items()):
        tag = "|%s%s" % (key[0] or "", key[1])
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--verbose", "-v", action="store_true")
    a = ap.parse_args()

    d, secs = load(a.dll)
    sigs, meta = read_table(d, secs)
    lines = open(SRC, encoding="latin-1").read().split("\n")

    defects = 0
    for name, fn in (("read offsets", lambda: check_offsets(sigs, lines, a.verbose)),
                     ("string tails", lambda: check_string_tails(sigs, lines, a.verbose)),
                     ("length gates", lambda: check_gates(sigs, lines, a.verbose)),
                     ("radix selection", lambda: check_radix(sigs, meta, lines, a.verbose)),
                     ("coverage", lambda: check_coverage(sigs, meta, lines, a.verbose))):
        print("%s:" % name)
        defects += fn()

    if TOLERATED_GATES or TOLERATED_READS:
        print("\ntolerances (corpus-backed, see 14-divergence-register.md):")
        for k, v in TOLERATED_GATES.items():
            print("    %-5s %s" % (k, v))

    print("\n%s" % ("OK: no conformance defects." if not defects
                    else "FAIL: %d defect(s)." % defects))
    return 1 if defects else 0


if __name__ == "__main__":
    sys.exit(main())
