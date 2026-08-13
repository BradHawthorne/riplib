#!/usr/bin/env python3
"""dll-validate-claims.py -- try to REFUTE the findings, not confirm them.

Every adjudication in docs/spec/12-dll-provenance.md rests on a claim about
the driver, the shipped corpus, or RIPlib's own source.  Those claims were
true when written.  Documentation does not notice when code changes underneath
it, and this project has now been bitten by that three separate times:

    '|1I'  a field list that still described the defect after the fix landed
    '|y'   "is not implemented yet", written before it was implemented
    '|3e'  a section that called the code an accept-both compromise for a
           day after the compromise had been removed
    '|@'   a note asserting that 'X' "is not in the DLL command table",
           sitting above a case that was not '@' at all -- both halves wrong

Each was found by accident, which is not a method.  This is the method: state
each claim as a predicate, re-derive its evidence from the image and the
corpus, and report the ones that no longer hold.  A claim that cannot be
re-derived is reported as UNVERIFIED rather than passed silently.

Usage:
    python scripts/dll-validate-claims.py <path>/Ripscrip.dll [--corpus DIR]
"""
import argparse
import hashlib
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src", "ripscrip.c")
SRC2 = os.path.join(ROOT, "src", "ripscrip2.c")

IB = 0x10000000


# ── image ────────────────────────────────────────────────────────────────
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


def mk_rva2off(secs):
    def f(r):
        for va, vs, rp, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + rp
        return None
    return f


def level_of(slot):
    return 0 if slot <= 84 else 1 if slot <= 109 else 2 if slot <= 121 else 3


def read_table(d, rva2off):
    """slot -> (letter or None, rva, argc, widths, radix)."""
    base = rva2off(0x080820)
    out = []
    for i in range(129):
        raw = d[base + i * 40: base + (i + 1) * 40]
        L = raw[15]
        argc = struct.unpack_from("<i", raw, 16)[0]
        rva = struct.unpack_from("<I", raw, 1)[0] - IB
        radix = struct.unpack_from("<H", raw, 0x26)[0] & 3
        w = []
        for b in raw[20:38]:
            if b == 0:
                break
            w.append(2 if b in (0xFF, 0xFE) else b)
        out.append((chr(L) if 0x20 <= L < 0x7F else None, rva, argc, w, radix))
    return out


def cstr(d, rva2off, va, maxlen=96):
    off = rva2off(va - IB)
    if off is None:
        return None
    o = bytearray()
    while off < len(d) and len(o) < maxlen:
        c = d[off]
        if c == 0:
            break
        if c < 0x20 or c > 0x7E:
            return None
        o.append(c)
        off += 1
    return o.decode("ascii", "replace")


def handler_strings(d, secs, rva2off, table, rva):
    """Diagnostics a handler pushes, bounded at the next handler entry.

    Bounding matters: reading a fixed byte count past the entry runs into
    whatever function follows, which is how a neighbour's strings were once
    attributed to '|3e'.
    """
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    except ImportError:
        return None
    bounds = sorted({r for (_, r, _, _, _) in table})
    nxt = next((a for a in bounds if a > rva), None)
    extent = min(nxt - rva, 4096) if nxt else 1200
    off = rva2off(rva)
    if off is None:
        return []
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    out, insns = [], []
    for ins in md.disasm(d[off:off + extent], rva + IB):
        insns.append(ins)
        if ins.mnemonic == "ret" and len(insns) > 4:
            if {insns[-2].mnemonic, insns[-3].mnemonic,
                    insns[-4].mnemonic} & {"pop", "leave", "add"}:
                break
        if ins.mnemonic == "push" and ins.op_str.startswith("0x"):
            try:
                s = cstr(d, rva2off, int(ins.op_str, 16))
            except ValueError:
                continue
            if s and len(s) >= 4:
                out.append(s)
    return out


# ── corpus ───────────────────────────────────────────────────────────────
def corpus_payloads(corpus, cmd):
    """Payload strings for a command, over every scene."""
    if not corpus or not os.path.isdir(corpus):
        return None
    pat = re.compile(rb"\|" + re.escape(cmd.encode()) + rb"([^|\r\n]*)")
    out = []
    for root, _, files in os.walk(corpus):
        for fn in files:
            if os.path.splitext(fn)[1].upper() not in (".RIP", ".RIPTEL"):
                continue
            try:
                blob = open(os.path.join(root, fn), "rb").read()
            except OSError:
                continue
            for m in pat.finditer(blob):
                out.append(m.group(1).decode("latin-1"))
    return out


# ── source ───────────────────────────────────────────────────────────────
def source():
    t = open(SRC, encoding="latin-1").read()
    t2 = open(SRC2, encoding="latin-1").read() if os.path.exists(SRC2) else ""
    return t, t2


def case_body(text, letter, lo, hi):
    """Body of `case '<letter>':` whose line number falls in [lo, hi)."""
    lines = text.split("\n")
    for i, l in enumerate(lines, 1):
        if not re.match(r"\s+case '%s':" % re.escape(letter), l):
            continue
        if not (lo < i < hi):
            continue
        body = []
        for j in range(i - 1, min(i + 120, len(lines))):
            if j > i - 1 and re.match(r"\s+case '.':", lines[j]):
                break
            body.append(lines[j])
        return "\n".join(body)
    return None


L3, L1, L0 = (2299, 2468), (2517, 3485), (3485, 10 ** 9)
BLOCK = {0: L0, 1: L1, 3: L3}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--corpus", default="C:/RIPtel")
    a = ap.parse_args()

    d, secs = load(a.dll)
    rva2off = mk_rva2off(secs)
    table = read_table(d, rva2off)
    src, src2 = source()

    by_cmd = {}
    for slot, (L, rva, argc, w, radix) in enumerate(table):
        if L is None:
            continue
        key = "|%s%s" % (level_of(slot) or "", L)
        by_cmd.setdefault(key, (slot, L, rva, argc, w, radix))

    results = []

    def check(name, ok, detail):
        results.append((name, ok, detail))

    # ---- D-14 ..: identities asserted from a handler's own diagnostics ----
    for cmd, want in (("|1G", "RIP_Scroll"),
                      ("|1g", "RIP_CopyBlit"),
                      ("|1M", "RIP_Mouse"),
                      ("|1U", "RIP_Button"),
                      ("|;", "RIP_PolyMarker"),
                      ("|@", "RIP_TextXY"),
                      ("|2P", "RIP_PortDefine")):
        if cmd not in by_cmd:
            check("%s names itself %s" % (cmd, want), None, "no dispatch entry")
            continue
        strs = handler_strings(d, secs, rva2off, table, by_cmd[cmd][2])
        if strs is None:
            check("%s names itself %s" % (cmd, want), None, "capstone missing")
        else:
            hit = any(want.rstrip("()") in s for s in strs)
            check("%s names itself %s" % (cmd, want), hit,
                  "handler strings: %s" % (", ".join(sorted(set(strs))[:3])
                                           or "none"))

    # ---- fixed-radix commands -------------------------------------------
    b64 = sorted(k for k, v in by_cmd.items() if v[5] == 2)
    b36 = sorted(k for k, v in by_cmd.items() if v[5] == 1)
    check("base-64 set is |D |d |h |y", b64 == ["|D", "|d", "|h", "|y"],
          "found %s" % " ".join(b64))
    check("base-36 set is |J |N", b36 == ["|J", "|N"], "found %s" % " ".join(b36))

    # ---- string-tail offsets --------------------------------------------
    TAILS = {"|1A": 6, "|1b": 18, "|1R": 8, "|1W": 1, "|3G": 8, "|3R": 14,
             "|1D": 5, "|1F": 6, "|1t": 1}
    for cmd, want in TAILS.items():
        if cmd not in by_cmd:
            check("%s fixed prefix is %d" % (cmd, want), None, "no entry")
            continue
        got = sum(by_cmd[cmd][4])
        check("%s fixed prefix is %d" % (cmd, want), got == want,
              "record sums to %d" % got)

    # ---- corpus claims ---------------------------------------------------
    CORPUS = {
        "|k": ("133 uses, 132 of width 2 and one of width 1",
               lambda p: len(p) == 133 and
               sum(1 for x in p if len(x) == 2) == 132 and
               sum(1 for x in p if len(x) == 1) == 1),
        "|1b": ("36 uses, every payload at least 18 chars",
                lambda p: len(p) == 36 and all(len(x) >= 18 for x in p)),
        "|1R": ("25 uses, every payload starting with 8 zeros",
                lambda p: len(p) == 25 and all(x[:8] == "0" * 8 for x in p)),
        "|1e": ("14 uses, every payload exactly 24 chars",
                lambda p: len(p) == 14 and all(len(x) == 24 for x in p)),
        "|2s": ("every payload exactly 3 chars",
                lambda p: bool(p) and all(len(x) == 3 for x in p)),
    }
    for cmd, (desc, pred) in CORPUS.items():
        pay = corpus_payloads(a.corpus, cmd[1:])
        if pay is None:
            check("%s corpus: %s" % (cmd, desc), None, "no corpus at %s" % a.corpus)
        else:
            check("%s corpus: %s" % (cmd, desc), pred(pay),
                  "%d payload(s), widths %s"
                  % (len(pay), sorted({len(x) for x in pay})))

    # ---- source claims ---------------------------------------------------
    SRC_CLAIMS = [
        ("|@ is RIP_TEXT_XY in RIPlib", 0, "@", r"RIP_TEXT_XY"),
        ("|X is RIP_PIXEL in RIPlib", 0, "X", r"RIP_PIXEL"),
        ("|3e reads mega2, not mega4", 3, "e", r"mega2\(p\)"),
        ("|a rejects colour > 63", 0, "a", r"ega64 <= 63"),
        ("|Y rejects font > 10", 0, "Y", r"fid > 10"),
    ]
    for name, lvl, letter, pat in SRC_CLAIMS:
        lo, hi = BLOCK[lvl]
        body = case_body(src, letter, lo, hi)
        if body is None:
            check(name, None, "case not found in level-%d block" % lvl)
        else:
            check(name, bool(re.search(pat, body)), "pattern %r" % pat)

    # a negative: |3e must NOT still prefer mega4
    body = case_body(src, "e", *BLOCK[3])
    if body is not None:
        check("|3e no longer falls back to mega4",
              not re.search(r"mega4\(p\)", body), "checked for mega4(p)")

    # ---- report ----------------------------------------------------------
    ok = sum(1 for _, r, _ in results if r is True)
    bad = [x for x in results if x[1] is False]
    unk = [x for x in results if x[1] is None]
    for name, r, detail in results:
        mark = "PASS" if r is True else ("FAIL" if r is False else "????")
        if r is not True:
            print("%-4s %-46s %s" % (mark, name, detail))
    print("\n%d refuted, %d unverified, %d held (of %d claims)"
          % (len(bad), len(unk), ok, len(results)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
