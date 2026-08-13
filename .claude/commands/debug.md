# /debug — diagnose a failing gate, test, or scene

Find the true root before changing anything. RIPlib's instruments each see a
different slice, and they disagree in informative ways — a green local suite and
a red CI job is not a contradiction, it is a *reading*.

## When
- A CI job fails, or a green passes locally and fails there.
- A test, a corpus scene, or a conformance/claim check goes red.
- A scene renders wrong, or stops asking the host for something it used to.

**Do NOT** use for a defect with an obvious root and a one-line fix — fix it,
prove the test fails without it, commit. Use `/debug` when the signal is
ambiguous or the sensors disagree.

## Sensors — what each one can and cannot see

| sensor | sees | structurally blind to |
|---|---|---|
| `ctest` / unit suites | authored behaviour | anything nobody wrote a test for |
| `test_corpus` | real content: pixels, colours, **requests, regions**, host silence, FSM state, guard bands | correctness of a *value* — a wrong filename still counts as one request |
| `dll-conformance.py` | offsets, string tails, gates, radix, coverage vs the record | semantics; a correctly-shaped wrong reading |
| `dll-validate-claims.py` | whether documented claims still hold | claims nobody wrote down |
| ASan | out-of-bounds, use-after-free, leaks | a zero-length `memcpy` — it touches no memory |
| UBSan | signed overflow, shifts, **`nonnull-attribute`** | that last one **on Windows**, under any compiler — see below |
| `test_fuzz_seeded` | crashes and guard violations across mutated input | anything outside its seed set's reach |
| `-fstack-usage` (ARM) | frame growth against the 656-byte budget | everything else |

**The sanitizer-parity trap.** UBSan's `nonnull-attribute` check fires only
because *glibc* annotates `memcpy` and friends. Windows CRT headers do not, so
the check is unreachable there whatever the toolchain — verified by running CI's
exact flags under clang locally, where the suites pass **with and without** the
guard. If CI's sanitizer job is red and local is green, believe CI and read the
job log; do not conclude the log is stale.

## Steps

1. **Read the actual failure.** The first error, not the fiftieth cascade. For
   CI, fetch the job log rather than inferring from which job went red — the
   repository is public, though log endpoints need auth.

2. **Reproduce at the smallest scale that still fails.** A single scene, a
   single command, a single payload. Most of this project's defects reduce to
   one payload: `!|1A010000`, `!|1R00000000dragon.txt`.

3. **Read at least two sensors, and take disagreement seriously.** Unit green +
   CI red is a *class* statement about the local toolchain. Conformance clean +
   corpus wrong means the shape is right and the semantics are not. Corpus
   counts unchanged + names wrong means the metric cannot see values.

4. **Suspect recent work first, and your own most of all.** Every sanitizer and
   CI failure this project has had was self-inflicted and recent. The `|1U`
   `memcpy` was reachable only *because* an earlier fix removed the gate that
   had been hiding it — a latent defect behind a live one is invisible to every
   test that exercises the live one.

5. **Form one falsifiable root hypothesis and attack it with a different
   sensor** than the one that raised the alarm.

6. **Fix minimally at the root.** Then:
   - a regression test that **fails against the pre-fix code** — prove it;
   - re-run the corpus and compare metrics against the pre-fix build, not
     against memory. Build the previous commit in a worktree if the change
     could move rendering, requests or regions;
   - if the shape generalises, hand the class to `/audit`.

7. **Compound.** Record the finding and — more valuable — *why the existing
   instruments missed it*. That sentence is what produces the next check.

## Disciplines
- **Commit before experimenting.** `git checkout <file>` to undo a scratch edit
  discards every uncommitted change in that file; it ate a real fix twice in one
  session, and the second time only the conformance tool noticed.
- **`src/*.c` is latin-1.** A script that writes a non-latin-1 character opens
  for truncation, empties the file, *then* raises. It destroyed `ripscrip.c`
  twice. Validate the whole blob before writing, and write bytes.
- **A no-op comparison returns a plausible number.** Comparing a build against
  itself via `git stash` when the change is already committed yields "identical"
  and looks like a result. Check the comparison actually differs.
- **Trust order:** fresh command output this turn > the binary > shipped content
  > our tests > our documentation > memory of what a document said.

## Interop
Feeds `/audit` (a defect with a shape), `/arbitrate` (a root that turns on what
the driver does), `/decide` (a root that is really an unmade decision). Routed to
by `/iterate` when a green will not come back.
