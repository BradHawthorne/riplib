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
from dll_record import dispatch_level
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
_spec = importlib.util.spec_from_file_location("conformance", os.path.join(HERE, "dll-conformance.py"))
CONFORMANCE = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(CONFORMANCE)
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
    """Complete body, including numeric labels and nested control flow."""
    for level, ch, line, body in CONFORMANCE.handler_bodies(text.splitlines()):
        if ch == letter and lo < line < hi:
            return text.splitlines()[line-1] + "\n" + body
    return None


L3, L1, L0 = _blocks(open(SRC, encoding="latin-1").read())
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
        key = "|%s%s" % (dispatch_level(d[rva2off(0x080820)+slot*40:rva2off(0x080820)+(slot+1)*40]) or "", L)
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
    TAILS = {"|1A": 6, "|1b": 18, "|1R": 8, "|1W": 1, "|9G": 8, "|9R": 14,
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

    # ---- handler-derived bounds (D-14, D-21) -----------------------------
    # Each is "the handler guards this field at this value and says so".
    # Re-derived by finding the diagnostic and the compare that guards it.
    BOUNDS = [
        ("|1G", "Invalid mode parameter", 6),
        ("|1g", "Illegal mode parameter", 5),
        ("|;", "Invalid marker number", 36),
        ("|;", "Invalid marker rotation", 360),
        ("|Y", "Illegal font number", 0xA),
        ("|a", "Invalid Color Parameter", 0x3F),
    ]
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        have_cs = True
    except ImportError:
        have_cs = False
    for cmd, diag, want in BOUNDS:
        name = "%s guards %r at %d" % (cmd, diag, want)
        if not have_cs or cmd not in by_cmd:
            check(name, None, "no capstone or no entry")
            continue
        rva = by_cmd[cmd][2]
        bounds = sorted({r for (_, r, _, _, _) in table})
        nxt = next((x for x in bounds if x > rva), None)
        extent = min(nxt - rva, 4096) if nxt else 1200
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        insns = list(md.disasm(d[rva2off(rva):rva2off(rva) + extent], rva + IB))
        hit = None
        for i, ins in enumerate(insns):
            if ins.mnemonic != "push" or not ins.op_str.startswith("0x"):
                continue
            try:
                s = cstr(d, rva2off, int(ins.op_str, 16))
            except ValueError:
                continue
            if not s or diag not in s:
                continue
            for k in range(i - 1, max(-1, i - 12), -1):
                m = insns[k]
                if m.mnemonic == "cmp" and "," in m.op_str:
                    rhs = m.op_str.rsplit(",", 1)[1].strip()
                    if rhs.startswith("0x") or rhs.isdigit():
                        hit = int(rhs, 0)
                        break
            break
        check(name, hit == want,
              "guard found at %s" % ("0x%x (%d)" % (hit, hit) if hit is not None
                                     else "none"))

    # ---- protection is host-side only (D-22) ------------------------------
    if have_cs:
        writers = 0
        readers = 0
        for (L, rva, argc, w, radix) in table:
            if L is None:
                continue
            off = rva2off(rva)
            if off is None:
                continue
            bounds = sorted({r for (_, r, _, _, _) in table})
            nxt = next((x for x in bounds if x > rva), None)
            extent = min(nxt - rva, 4096) if nxt else 1200
            md = Cs(CS_ARCH_X86, CS_MODE_32)
            for ins in md.disasm(d[off:off + extent], rva + IB):
                if "0x104" not in ins.op_str:
                    continue
                if ins.mnemonic == "mov" and \
                        ins.op_str.split(",")[0].strip().endswith("]"):
                    writers += 1
                elif ins.mnemonic in ("cmp", "test"):
                    readers += 1
        check("protection word has readers but no dispatched writer",
              writers == 0 and readers > 0,
              "%d writer(s), %d reader(s)" % (writers, readers))
    else:
        check("protection word has readers but no dispatched writer", None,
              "capstone missing")

    # ---- structural claims about RIPlib's own parser ----------------------
    STRUCT = [
        ("the << >> scanner runs for every byte, not only at IDLE",
         src, r"static void rip_dispatch_byte\(rip_state_t \*s, void \*ctx"),
        ("an unrecognised << >> run is emitted verbatim",
         src, r"preproc_emit_verbatim\(s, ctx\);"),
        ("|1U registers a region regardless of host_len",
         src, r"if \(s->num_mouse_regions < RIP_MAX_MOUSE_REGIONS\) \{"),
        ("|1U guards the host-text memcpy against NULL",
         src, r"if \(host_len > 0\)\s*\n\s*memcpy\(r->text, host_text"),
        ("|2P reads its flags as a full mega4",
         src2, r"mega4l\(raw \+ 9\)"),
        ("|2P no longer sets FULLSCREEN/PROTECTED from wire bits 2-3",
         src2, None),
    ]
    for name, text, pat in STRUCT:
        if pat is None:
            # negative: those assignments must be GONE from the |2P handler
            bad_pat = re.compile(r"port_flags & 0x0[48]\)")
            check(name, not bad_pat.search(text), "checked for port_flags & 0x04/0x08")
        else:
            check(name, bool(re.search(pat, text)), "pattern %r" % pat)

    # ---- D-31..33: handler-derived geometry and host/fill contracts -------
    def instructions(start, end):
        if not have_cs:
            return None
        off = rva2off(start)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        return list(md.disasm(d[off:off + end - start], IB + start))

    for slot, mode in ((8, 1), (10, 0), (11, 2), (83, 3), (84, 0)):
        start = table[slot][1]
        end = min(r for _, r, _, _, _ in table if r > start)
        ins = instructions(start, end)
        calls = [] if ins is None else [i for i, op in enumerate(ins)
                 if op.mnemonic == "call" and op.op_str == "0x1000fa70"]
        # Last argument pushed is the helper's first argument, the closure mode.
        got = []
        for i in calls:
            push = next((op for op in reversed(ins[:i]) if op.mnemonic == "push"), None)
            value = push.op_str if push else None
            if value == "edi":
                writes = [op for op in ins[:i] if op.mnemonic in ("mov", "xor", "lea", "pop", "add", "sub")
                          and op.op_str.split(',')[0] == "edi"]
                value = "0" if writes and writes[-1].mnemonic == "xor" and writes[-1].op_str == "edi, edi" else None
            got.append(value)
        check("slot %d calls ellipse generator with closure %d" % (slot, mode),
              None if ins is None else got == [str(mode)], str(got))

    for name, start, end, callee in (
            ("refresh assigns a command string", 0x46BD9, 0x46C64, "0x1003e43c"),
            ("SwitchDirectory names its host service", 0x46F66, 0x46FC0, None),
            ("EnterBlockMode names its host service", 0x24B4E, 0x24C60, None)):
        ins = instructions(start, end)
        if callee:
            found = ins is not None and any(i.mnemonic == "call" and i.op_str == callee for i in ins)
        else:
            expected = "RIP_SwitchDirectory" if start == 0x46F66 else "RIP_EnterBlockMode"
            strings = [cstr(d, rva2off, int(i.op_str, 16)) for i in ins or []
                       if i.mnemonic == "push" and i.op_str.startswith("0x")]
            found = any(expected in s for s in strings if s)
        check(name, None if ins is None else found, "bounded disassembly")

    off = rva2off(0x7AFD8)
    check("EMPTY brush rows are zero; SOLID rows are 255",
          struct.unpack_from('<16H', d, off) == (0,)*8 + (255,)*8, "brush table 0x7AFD8")
    for cmd, start, end in (("|1g", 0xB7A4, 0xB9CF), ("|1G", 0xD7E0, 0xD9DD)):
        ins = instructions(start, end)
        check(cmd + " move requests SRCCOPY, not mode-selected ROP",
              None if ins is None else any(i.mnemonic == "push" and i.op_str == "0xcc0020" for i in ins),
              "SRCCOPY immediate before blit")
    for ch, pat in ((",", r"rip_draw_affine_oval\(s,p,1,false\)"),
                    (".", r"rip_draw_affine_oval\(s,p,0,false\)"),
                    (":", r"rip_draw_affine_oval\(s,p,2,"),
                    ("`", r"rip_draw_affine_oval\(s,p,3,"),
                    ("{", r"rip_draw_affine_oval\(s,p,0,true\)")):
        body = case_body(src, ch, *BLOCK[0])
        check("|" + ch + " uses affine geometry, without host side effects",
              None if body is None else bool(re.search(pat, re.sub(r"\s+", "", body))) and
              not re.search(r"draw_copy_rect|num_mouse_regions|rip_icon_lookup", body), pat)

    # ---- report ----------------------------------------------------------
    # D-34: prefix bytes are independent of handler address and slot number.
    rawrows = CONFORMANCE.dispatch_rows(d, secs)
    levels = [r['level'] for r in rawrows]
    check("all 129 literal dispatch prefixes agree with recovered grouping",
          levels == [0]*85+[1]*25+[2]*12+[3]*2+[9]*5,
          "record bytes 5..14; slots 124..128 are level 9")
    check("driver has no duplicate named opcode",
          len({(r['level'],r['letter']) for r in rawrows if r['letter']}) == 118,
          "118 named rows must have distinct (prefix,letter) keys")
    ins = instructions(0x252C0, 0x252F2)
    check("9U validates type without decoding a payload",
          None if ins is None else
          {i.op_str for i in ins if i.mnemonic == 'call'} == {'0x10045038','0x10038e47'} and
          any(i.mnemonic == 'cmp' and i.op_str == 'esi, 1' for i in ins),
          "bounded handler calls only cursor reset and error reporting")
    ins = instructions(0xCB38, 0xCEF8)
    check("LoadIcon ROP reads argument 3",
          None if ins is None else any(i.mnemonic == 'mov' and i.op_str == 'esi, dword ptr [ecx + 0xc]' for i in ins),
          "ESI feeds the CD5A ROP selector")
    body = case_body(src, 'I', *BLOCK[1])
    check("LoadIcon uses p[5] for mode, never p[4]",
          None if body is None else bool(re.search(r'uint8_t mode\s*=\s*\(uint8_t\)mega_digit\(p\[5\]\)',body)) and
          not re.search(r'uint8_t mode\s*=\s*\(uint8_t\)mega_digit\(p\[4\]\)',body),
          "source semantic-field predicate")
    ins = instructions(0x49340, 0x493B7)
    check("LoadIcon stretch invokes logical-to-device dimension scaling",
          None if ins is None else any(i.mnemonic == 'call' and i.op_str == '0x10031084' for i in ins),
          "show_bmp_file dimension helper")
    drawing = (Path(SRC).parent / 'drawing.c').read_text(encoding='utf-8')
    match = re.search(r'rip_fill_patterns\[10\]\[8\]\s*=\s*\{(.*?)\n\};',drawing,re.S)
    got = [int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',match[1])] if match else []
    want = list(struct.unpack_from('<80H',d,rva2off(0x7AFD8)+32))
    check("all 80 patterned-brush rows match driver data",got == want,
          "BGI patterns 2..11, excluding EMPTY/SOLID and user brush")
    ins = instructions(0xD3DA, 0xD64D)
    calls = {i.op_str for i in ins or [] if i.mnemonic == 'call'}
    check("query definitions check port and text-window existence/protection",
          None if ins is None else {'0x100338bc','0x10033821','0x1000e028','0x10027642'} <= calls,
          "four distinct target-slot checks in the bounded query handler")
    ins = instructions(0x13E61, 0x13ED0)
    strings = [cstr(d,rva2off,int(i.op_str,16)) for i in ins or []
               if i.mnemonic == 'push' and i.op_str.startswith('0x')]
    check("resident query helper recognizes OFF",None if ins is None else '$OFF$' in strings,
          "resident-query assignment helper")

    ok = sum(1 for _, r, _ in results if r is True)
    bad = [x for x in results if x[1] is False]
    unk = [x for x in results if x[1] is None]
    for name, r, detail in results:
        mark = "PASS" if r is True else ("FAIL" if r is False else "????")
        if r is not True:
            print("%-4s %-46s %s" % (mark, name, detail))
    print("\n%d refuted, %d unverified, %d held (of %d claims)"
          % (len(bad), len(unk), ok, len(results)))
    return 1 if bad or unk else 0


if __name__ == "__main__":
    sys.exit(main())
