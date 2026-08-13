# Command suite — RIPlib

Adapted from the System 7 archeology methodology (an analogous byte-exact
reverse-engineering project), refactored to our domain: a portable C99 RIPscrip
renderer adjudicated against a shipped driver. Each command is a self-contained
workflow a cold-start session can follow. **Read the command file and follow it.**

| Command | File | When |
|---|---|---|
| `/iterate` | [iterate.md](iterate.md) | Default end-of-turn loop; "iterate next steps"; resume after context loss. **The spine.** |
| `/arbitrate` | [arbitrate.md](arbitrate.md) | Establish what a command IS, from the driver: record → handler → corpus. Persist as a D-record. |
| `/audit` | [audit.md](audit.md) | Take one *class* of defect across every command until it reports zero. **Where every win came from.** |
| `/verify` | [verify.md](verify.md) | Adversarial pass: try to REFUTE what we already claim, including about our own instruments. |
| `/debug` | [debug.md](debug.md) | Diagnose a failing gate, test, or scene; sensor disagreement first. |
| `/decide` | [decide.md](decide.md) | Consequential fork + ADR. |

`decide.md` is the portable edition carried across projects, not a RIPlib-native
file. Its CONFIG is pointed at this repo (`design/decisions.md`,
`design/knowledge.md`, `design/adr/`) and resolves, but its worked example in the
appendix is generic — a DuckDB storage choice with ADRs `0023`/`0024` that belong
to no project here. Read it as illustration; RIPlib's real ADRs are in
`design/adr/`.

## The arbiter, and the evidence ladder

RIPlib's measure of correctness is the shipped TeleGrafix driver:
`RIPSCRIP.DLL`, 592,896 bytes, MD5 `bade8b1f4e467ac7ad4edb2639738d4c`, from a
RIPtel 3.1 install. It is **not vendored** and will not be.

Rank evidence in this order. Every command in this suite assumes it.

1. **The handler.** What the driver *does*. Outranks everything, including its
   own record — that is how `|D`'s field order, `|F`'s stub and `|1G`'s identity
   were settled. Handlers frequently name themselves in their own diagnostics.
2. **The dispatch record** (129 entries at RVA `0x080820`). Machine-readable and
   complete, but it says only what is *accepted*, and it types only the numeric
   argument array — a trailing string never appears in it.
3. **Shipped content** (the corpus). Evidence about the world, not about the
   driver. It cannot overrule the driver on what a field *means*; it does
   overrule a decision to reject input the driver would reject — see `|k` and
   `|=` in `docs/spec/14-divergence-register.md` §14.3.3.
4. **bbs-land's reference, the 1.54 spec, the 2.0 draft.** Secondary, and often
   scoped to an earlier generation of the protocol. Useful, never decisive.
5. **RIPlib's own comments.** The weakest source in the building. Four were found
   materially wrong in a single day (D-27). Verify before citing.

## Durable knowledge (commands compound into these)

| Path | Role |
|------|------|
| `docs/spec/12-dll-provenance.md` | `D-NN` adjudication records — the audit trail. |
| `docs/spec/14-divergence-register.md` | Every divergence from bbs-land *and* every deliberate divergence from the driver, with reasoning. |
| `docs/spec/13-dll-command-table.md` | The dispatch record, verbatim. |
| `design/syntax-audit.md` | The field-list comparison and what it cost. |
| `docs/spec/{01..11}` | The protocol specification proper. |
| `design/decisions.md` | Candidates, committed, parked, graveyard — `/decide`'s ledger. |
| `design/knowledge.md` | `HR-NNN` heuristic rules, prior art, open questions. |
| `design/adr/` | One ADR per committed or killed decision. |
| `design/bbs-land-alignment.md` | The name/behaviour reconciliation with the sibling project. |

## Green — the instruments

Each sees what the others structurally cannot. **Tests prove it behaves;
conformance proves it matches the record; the corpus proves it survives real
content; the claim validator proves the documentation is still true.**

| Command | Expect | When |
|---|---|---|
| `ctest -C Debug` (in a build dir) | `100% tests passed`, 5 suites | **Every change.** |
| `python scripts/dll-conformance.py <dll> -v` | `OK: no conformance defects` | Any parser change. Five classes: read offsets, string tails, length gates, radix, coverage. |
| `python scripts/dll-validate-claims.py <dll>` | `0 refuted, 0 unverified` | Any change to code *or* docs. Re-derives every load-bearing claim. |
| `python scripts/check-command-docs.py` | parser and appendix agree | Command names or the appendix table changed. |
| corpus replay (`test_corpus`) | `35/35 scenes replayed cleanly` | Any parser change. Reports pixels, colours, **requests, regions**, and asserts host silence. |
| UBSan + ASan | clean | Anything that indexes or parses. **See the caveat below.** |
| `test_fuzz_seeded <n>` | `no guard violations, no crash` | Any change to the byte path. Takes an iteration count. |
| `arm-none-eabi-gcc … -fstack-usage` | `execute_rip_command` ≤ 656 | Anything adding locals to a command handler. |

**A local sanitizer build is not equivalent to CI's.** UBSan's
`nonnull-attribute` check — a NULL passed where a parameter is declared
never-null, such as `memcpy`'s source at zero length — fires only because
*glibc* annotates those functions. Windows CRT headers do not, so the check
cannot trigger there under any compiler, clang included. A real defect of that
exact shape passed every local suite and was caught only by the Linux job.
ASan is no help either: a zero-length copy touches no memory.

## Disciplines that catch more than the tools

- **Turn a defect into a class check.** Every class in `dll-conformance.py`
  began as one bug found by accident; every one then found more of the same.
  Fixing the instance and stopping is leaving the rest on the floor.
- **Measure the measurement.** Instruments were wrong five separate times —
  a truncated field list, dropped continuation rows, an elided reference list,
  a level boundary off by one, a body extractor stopping at the first `break`.
  Each would have overstated or understated a conclusion. When a late pass
  starts finding faults in the *instruments*, that is the method working.
- **Prove the regression test fails against the pre-fix code.** A test written
  after a fix is unfalsified. Stash or `git show HEAD:file`, rebuild, watch it
  fail. If it passes, the *test* is wrong.
- **Non-rendering behaviour is invisible unless deliberately counted.** Three
  defects hid because a renderer's harness measures what it renders. Interaction,
  host requests, and consumer-read state each had to be counted explicitly.
- **Check the corpus before tightening a gate.** The record says what the driver
  accepts, not what content exists. `|k` was tightened to match the record and
  restored when one shipped scene turned out to use the short form.
- **Commit before experimenting.** `git checkout <file>` to undo a scratch edit
  discards every uncommitted change in that file. It ate a real fix twice.
- **`src/*.c` is latin-1.** Writing a non-latin-1 character through a script
  that opens for truncation empties the file *and then* raises. It destroyed
  `ripscrip.c` twice. Validate before writing; write bytes, not text.

## Entry (new session / after context loss)

`README.md` → `docs/spec/14-divergence-register.md` §14.1 (the measure) →
`docs/spec/12-dll-provenance.md` (most recent D-records) → the instruments above.
Everything needed to resume is on disk; the conversation is not the memory.
