# /audit — take one CLASS of defect to zero, across every command

Not one command at a time. **One kind of mistake, across all of them, until the
check reports zero.** This is where every substantive win in this project came
from, and the reason is measurable: each class in `scripts/dll-conformance.py`
began as a single defect found by accident, and every one of them then found
more of the same.

| the class | first found as | then found |
|---|---|---|
| read offsets | `\|3G` reading a URL at 0 against a prefix of 8 | `\|1M`, `\|2P` |
| string tails | `\|1R` requesting `00000000dragon.txt` | `\|1A`, `\|1b` (36 uses), `\|1W` |
| length gates | `\|1g` gating 12 against a record of 14 | **fifteen more** |
| value ranges | `\|1G`'s mode bound, read by hand | `\|a` masking where the driver rejects, `\|Y` unchecked |
| radix | `\|d` decoding base-64 with the base-36 helper | (none — but the check is what proves that) |

## When
The default forward motion. Invoked by `/iterate` with a class to drive, or when
`/arbitrate` establishes a contract that other commands might also violate.

**Do NOT** use to fix a single known defect — that is ordinary work; fix it,
test it, commit it. Use `/audit` the moment you notice the defect has a *shape*.

## Steps

0. **Name the class as a predicate.** Not "check the offsets" — "every field read
   must land on a boundary the record defines, at the width the record gives".
   A class you cannot state as a predicate cannot be checked mechanically, and
   will be walked by hand and mis-walked. Hand-checking is exactly what let
   `|2P`'s invented flag bits survive in the very handler being inspected.

1. **Establish the ground truth** the predicate rests on, via `/arbitrate` if it
   is not already a D-record. Encode the *rule*, not the instance: "the record's
   fixed width is where a trailing string begins" covers nine commands.

2. **Write the check**, into `scripts/dll-conformance.py` where it belongs with
   its siblings. It must:
   - cover **every** command the predicate can apply to, and say how many it
     examined — a clean result is a claim about a set, so report the set;
   - **exit non-zero** on a defect, so it can gate;
   - name deliberate tolerances **by name** rather than passing them silently
     (`TOLERATED_GATES`), each with the corpus evidence that justifies it.

3. **Prove the check can fail.** Re-inject a real historical defect and watch it
   caught. A check that cannot fail is worth nothing, and this is cheap:
   `|1i`'s 12-character gate, `|h` decoded base-36, a one-character shift in
   `|1G`'s offsets — each should produce exactly one finding.

4. **Triage the findings — do not bulk-fix.** For each, in order:
   - re-derive the record and, where they might disagree, the handler;
   - **measure the corpus** before tightening anything. Payload widths, column
     values, use counts. `|k` was tightened to match the record and restored
     when one shipped scene turned out to send the short form; `|=` sends three
     different widths and all three are real content;
   - decide: fix, or tolerate-with-evidence. Both are legitimate. Only silence
     is not.

5. **Fix, with a regression test that fails against the pre-fix code.** Prove it:
   `git stash push src/ripscrip.c`, rebuild, watch it fail, `git stash pop`.
   Then check the corpus metrics did not move in ways you did not intend.

6. **Record.** A `D-NN` in `docs/spec/12-dll-provenance.md` covering the class:
   what it is, what it found, what it cost, and — the part future sessions need —
   **why the class was invisible until now**. Update the register if a tolerance
   or a deliberate divergence changed.

7. **Re-run everything.** Not just the new check: the full suite, conformance,
   the claim validator, the corpus, and — if the byte path moved at all —
   sanitizers and the fuzzer.

## Disciplines
- **The corpus decides acceptance, the driver decides meaning.** Tightening to
  match the record is right by default and wrong where shipped scenes say
  otherwise. The corpus is what tells the two apart.
- **Count what does not render.** Three defects hid because the harness measured
  pixels: buttons that never registered, mouse flags read from a reserved
  column, and every scene-file request asking for the wrong name. Regions,
  requests and host-silence are counted now *because* they were invisible.
- **A late pass finding faults in the instrument is the method working.**
  Verify the instrument, fix it, re-run — then continue the class.
- **Report what was dropped.** If a check bounds its coverage, say so. Silent
  truncation reads as "covered everything" when it did not.

## Interop
Fed by `/iterate` and `/arbitrate`. A finding whose ground truth is unsettled →
`/arbitrate`. A green that breaks → `/debug`. A tolerance that is really a
scope question → `/decide`. The class check, once written, belongs to `/verify`.
