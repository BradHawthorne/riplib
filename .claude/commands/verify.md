# /verify — try to REFUTE what we already claim

`/audit` walks a class forward. `/verify` walks backward through what is already
written down and attempts to break it. These are different activities, and the
second finds different defects — including in the first.

The premise, learned expensively: **prose about code does not notice when the
code changes, and a stale conclusion outranks the behaviour it misdescribes**,
because it states a conclusion where code only shows behaviour. Four
documentation defects of that shape were found in a single day (D-27), every one
by accident, and one of them was quoted back to the operator as a live open item
hours after the code had stopped matching it.

## When
- Before declaring anything ready for use.
- After a run of changes, especially ones that moved a shared path.
- When a document and the code disagree, or you catch yourself *citing* a
  comment rather than reading the code.
- Periodically, on no trigger at all. This is the pass that catches drift.

## Steps

1. **Run the standing checks first.** They encode past refutations, so a failure
   here is a regression, not a discovery:
   ```
   python scripts/dll-validate-claims.py <dll>     # 0 refuted, 0 unverified
   python scripts/dll-conformance.py   <dll> -v    # no conformance defects
   python scripts/check-command-docs.py            # parser vs appendix
   ctest -C Debug                                  # 5 suites
   ```

2. **Harvest new claims into predicates.** Every D-record, register entry and
   load-bearing comment written since the last pass asserts something. Turn each
   into a check in `scripts/dll-validate-claims.py`, re-derived from the image,
   the corpus, or the source — never from another document.

   **Prefer negatives.** A positive claim decays into a false negative when the
   code moves; a negative is what catches a defect being *reintroduced*, which
   is the failure this project actually had. Three are encoded: `|3e` no longer
   falls back to `mega4`; `|2P` no longer sets flags from wire bits 2–3; the
   protection word has no dispatched writer.

   A claim that cannot be re-derived is reported **UNVERIFIED**, never passed.

3. **Attack the instruments, not only the code.** They have been wrong five
   separate times, each time shaping a conclusion:
   - a field-list extractor reading only the first line of a comment;
   - continuation rows dropped by filtering on a printable letter, making `|h`
     look like one signature instead of six;
   - an elided reference list (`c1:2 c2:2 ... c16:2`) counted literally,
     inventing a 32-versus-6 divergence where all three sources agree;
   - a level boundary off by one, reporting an implemented command as missing;
   - a body extractor stopping at the first `break;`, which truncates every
     Level 2 handler at its length gate.

   For each instrument ask: what would it *fail* to see? Then check that case by
   another route.

4. **Attack the tests.** Name a wrong implementation that still passes. This
   project has shipped assertions that could not fail: an `MF_RADIO` test whose
   fixture registered zero regions, so "both regions inactive" was trivially
   true of the empty set. If no assertion can exclude the failure, say so in the
   test — a declared gap beats a green line that means nothing.

5. **Attack from outside the tree.** Clone the pushed state to a clean directory
   and run the checks there. Local greens are run against the working copy, and
   a working copy can contain a fix that was never committed — the claim
   validator itself was described in the docs for one commit before the script
   was actually in the repo.

6. **Record refutations as findings.** A claim the evidence refutes is a
   *finding*, not an embarrassment. Correct the record and say what it said
   before; `design/syntax-audit.md`'s `|3e` section states its own correction
   history for exactly this reason.

## Disciplines
- **Re-derive, never re-read.** Checking a document against another document
  measures agreement, not correctness. Both can be stale together.
- **The claim validator must be able to fail.** Re-inject a real historical
  defect on each axis and confirm the right claims are refuted, exit non-zero.
- **A clean result is a claim about a set.** Report what was examined —
  commands, reads, claims — or "zero defects" is unfalsifiable.
- **Suspect the newest text most.** The four stale-doc defects were all recent
  writing describing recent code.

## Interop
Feeds `/iterate` (a refutation is the next work item) and `/audit` (a refutation
with a shape is a new class). A refuted claim about the driver → `/arbitrate`.
A refuted green → `/debug`.
