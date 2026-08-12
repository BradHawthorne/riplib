# Alignment proposal — RIPlib ↔ bbs-land/remote-imaging-protocol

Status: **proposal, not committed.** Nothing here is decided until it lands as a `/decide` candidate row (see [Ledger integration](#9-ledger-integration)). Written 2026-08-11 against riplib `main` @ `3e05ecb` and bbs-land `main` @ clone of 2026-08-11.

Upstream: <https://github.com/bbs-land/remote-imaging-protocol> — a CC0 effort to standardize the RIPscrip record across 1.54 / 2.x / 3.x, with three vendor trees already dedicated to RIPlib.

---

## 1. What is actually on the table

This is not a cold start. bbs-land already carries RIPlib in three directories, reconciled against **our current tip `3e05ecb`**:

| Tree | What it is | Standing |
| --- | --- | --- |
| `version/3.0-riplib/` | Comparison record — where our account of RIPscrip 3.0 contradicts theirs | Questions of fact; exactly one side is right |
| `version/3.1-riplib/` | Our §A2G.1-7 extensions, documented as deltas | Adopt / decline / refine |
| `version/3.2-riplib/` | Our §A2G.8-13 extensions, documented as deltas | Adopt / decline / refine |

Their [`3.0-riplib/CONFLICTS.md`](https://github.com/bbs-land/remote-imaging-protocol/blob/main/version/3.0-riplib/CONFLICTS.md) enumerates **22 numbered items** — 12 baseline conflicts (`B1`-`B12`), 7 extension collisions (`X1`-`X7`), 3 free corrections (`N1`-`N3`). They have done the work of stating both readings, citing our own files and line numbers, and proposing a resolution order. That register is the spine of this proposal.

They also record, unprompted, where **we** are right: `§BUG.6` (pie/chord fill leak), `§DEAD.7` (never-applied patterned-flood brush), `§BUG.3`/`§BUG.4`/`§BUG.5`/`§BUG.9`. `§DEAD.7` corrected an inference of theirs drawn from the driver's silence. Those findings have already been folded into their canonical techspecs with attribution. This is a good-faith counterparty.

---

## 2. The evidence question — what our documentation actually rests on

Our spec is disassembly-derived, and this is stated in the document itself. `docs/spec/11-dll-deviations.md:11-12`:

> This information was derived from systematic disassembly of RIPSCRIP.DLL (592,896 bytes, 32-bit Windows PE, i386).

bbs-land already credits this. Their "Why the two records differ" table names our primary source as exactly that disassembly, and characterizes the split honestly:

| | bbs-land | RIPlib |
| --- | --- | --- |
| Primary source | RIPtel 3.1 install: `RIPSCRIP.HLP`/`RIPTEL.HLP` string tables + the 116-file demo corpus TeleGrafix shipped | Systematic disassembly of `RIPSCRIP.DLL` 3.0.7 |
| Answers well | What the wire actually carried — which opcodes shipping content sends, with what arguments, how often | What the shipping code actually did — which functions exist, what they compute, where they are buggy or dead |
| Answers poorly | Internal behavior of code nobody has disassembled | Which accepted opcodes real content used, and what TeleGrafix documented for authors |

Both are views of the same client from opposite ends. That framing is fair and we should adopt it rather than argue it. But being true to the code logic cuts three different ways, and the difference decides most of the register:

**(a) Where we hold positive binary evidence, we should win — but we have not published it.** Our spec asserts opcode names without citing the function string, export-table entry, or dispatch address they came from. From outside, an unsourced name is indistinguishable from an inference. Their `B4` asks for exactly this: _"establish the provenance of RIPlib's names. If they came from DLL strings or dispatch analysis, both sides hold real evidence."_ This is the single highest-value thing we can contribute, and it costs no code change.

**(b) Where our evidence is _negative_, it is the weak kind and we should expect to lose.** `§11.1` says RIP_FILLED_RECTANGLE is _"Not present as a named function in the DLL"_ and RIP_WORLD_FRAME has _"no implementation found in the DLL export table or function strings."_ That is absence-of-evidence in a half-megabyte binary whose surviving symbol coverage we have not characterized. Against it they put `|fZKQO` and `|J10` appearing in **90 of 116 shipping demo scenes**, with TeleGrafix's own inline comment in `ONLINE.RIP` reading _"Set base math to MegaNums"_ next to `J10`. Their adjudication rule — _"positive evidence from the binary beats an absent name in the help inventory, and wire observation from the corpus beats an absent function in a disassembly"_ — is symmetric and correct. We should accept it, including when it goes against us.

**(c) On the highest-impact item, our own disassembly contradicts our own header.** This is `B1`, and it deserves its own section.

### 2.1 B1 — our code logic refutes our code

The `|W` path carries no interpretation layer. Tracing it:

- `src/ripscrip.c:2214-2217` — the handler decodes the wire byte and passes it through unchanged: `wm = mega2(p)`, then `if (wm > 4) wm = 0`.
- `src/drawing.c:296` — `draw_set_write_mode` stores it directly: `g_write_mode = (mode <= DRAW_MODE_NOT) ? mode : DRAW_MODE_COPY`.
- `src/drawing.c:171-177` — the compositing switch keys on the `DRAW_MODE_*` symbols.

So `include/drawing.h:79-84` **is** RIPlib's assertion about wire semantics. Nothing mediates between the wire byte and the enum. It currently reads `0=COPY, 1=OR, 2=AND, 3=XOR, 4=NOT`.

Our own disassembly record says the opposite:

- `§BUG.7` (`11-dll-deviations.md:120-125`): _"The DLL internal constants were 0=COPY, 1=XOR, 2=OR."_
- `§DEAD.3` (`:193-199`): _"the write mode handler accepted mode values 0-4 on the wire but only implemented COPY (0), XOR (1), and OR (2) internally."_

Both place **XOR at 1**, which is bbs-land's reading, Borland BGI's convention, the 1.54 specification text, the 1.54 `RIP_PUT_IMAGE` mode table, the 2.00a4 five-value table, and SyncTERM `ripper.c:14062`.

The only thing standing against all of that is one unsourced sentence inside `§BUG.7` — _"The protocol wire values are 0=COPY, 1=OR, 3=XOR"_ — which cites nothing, **skips value 2 entirely**, and therefore does not even self-consistently describe the header it is defending (`drawing.h` puts AND at 2).

Git makes the sequence explicit. RIPlib v1.0.0 (`314aa04`) shipped this comment in `src/ripscrip.c`:

```c
/* mode at p+4: 0=COPY, 1=XOR, 2=OR, 3=AND, 4=NOT */
```

That matches the disassembly **and** the TeleGrafix record. Trace item **T-004** (2026-05-25) rewrote it to match `drawing.h`, recording the change as _"corrected … matches drawing.h DRAW_MODE_* and the RIPscrip wire encoding per docs/spec §2.3."_ In fact it overwrote our own disassembly-derived evidence with the unsourced reinterpretation.

**Recommendation: concede B1 and fix the header.** Being true to the code logic here means following our own disassembly, not our own enum.

Blast radius, verified:

- Four `#define` lines in `include/drawing.h`. The `drawing.c:171-177` switch is symbolic, so it follows automatically.
- Both guards stay valid — the value set `{0,1,2,3,4}` is unchanged, only the name↔value mapping moves.
- **No compat fixture uses `|W`** (`grep -lE '\|W' tests/fixtures/compat/*.rip` is empty), so no FNV-1a frame-hash churn.
- `§BUG.7` retires; `§DEAD.3` stands unchanged and becomes the citation.
- `docs/spec/02-level0-drawing.md` §2.3 needs its table updated.

This is the cheapest high-impact item in the register, and both projects' evidence already agrees.

---

## 3. Track A — the code-review findings

The `/code-review` pass on `3e05ecb` returned 12 findings. Their disposition below folds into the alignment work rather than sitting beside it, because the same commit is what bbs-land reconciled against.

| # | Finding | Disposition |
| --- | --- | --- |
| 1 | Card architecture still fully published at `README.md:259-280` (diagram, `Processor "B"`/`"V"` bullets, IPL paragraph) | **Fix** — finish the scrub |
| 2 | Live board block-diagram link at `README.md:282` re-identifies the card | **Fix** — remove the link |
| 3 | Pushed `v1.3.0` tag and the commit subject still carry the scrubbed strings | **Accept + adjust forward** — cannot be undone without a history rewrite |
| 4 | Removing the name from the forbidden list left no in-tree prohibition | **Fix** — restore as a rule, add a lint |
| 5 | Board-vs-firmware sentence deleted, not extracted | **Fix** — record in the handoff audit trail |
| 6 | Binding constraint amended inline with no decisions-log row | **Fix** — and do not repeat it in this proposal |
| 7 | `docs/historical/README.md:13` leaks `C:/RIPtel/protocol/` and the RIPtel name | **Fix** |
| 8 | `docs/riplib-snapshot-plan.md:5,126` still calls A2GSPU a card firmware | **Fix** |
| 9 | Rewritten Portability bullet keeps an unsanctioned A2GSPU mention; drops the variant qualifier | **Fix** |
| 10 | A2GSPU branding in the opening feature paragraph (`README.md:16`) | **Decide** — see §4.4, this is the `§A2G` question |
| 11 | `README.md:292` presents V/B role letters as RP2350 silicon variants | **Fix** — `cmake/arm-none-eabi.cmake:12-13` already states it correctly |
| 12 | Heading rename silently broke the section anchor | **Fix** — and adopt `run/check-links` discipline (§4.3) |

Three of these need more than a line.

**Finding 3 — the scrub cannot be made retroactive, so stop trying.** `git show v1.3.0:README.md` still returns the full pre-scrub text, and `design/decisions.md:54` lists the v1.3.0 Release-page publish as a sanctioned forward move. The realistic posture is: accept that the pre-`3e05ecb` history is public, scrub the tip so nothing *new* leaks, and write future commit subjects that do not name what they redact. A history rewrite on a repo an external project has already cloned and cited by SHA would break bbs-land's reconciliation anchors for no privacy gain.

**Finding 4 — replace the deleted prohibition with something enforceable.** The enumerated list was the only in-tree statement of the rule, there is no branding lint, and the `decisions.md:23` pointer to the "durable form" does not resolve (it glues a relative prefix onto an absolute Windows path, and points at the session-memory directory for the repo's *old* location). Proposal: restore the name to the list, and add `scripts/check-branding.sh` running a real regex over tracked files, wired into CI. Note the verification grep recorded for `3e05ecb` was a no-op twice over — `git grep` defaults to basic regex so `|` was literal, and `.` cannot span the space-plus-quote in `Processor "V"`. The lint must use `-E` or `-P`.

**Finding 6 — this proposal must not repeat the defect it flags.** A binding constraint was amended in place with no chronological log row. Everything proposed here therefore lands as a candidate row plus a log line first (§9).

---

## 4. Track B — adopting their documentation standards

Their `CONTRIBUTING.md` is a genuine standards document. Adopting it is mostly free and makes our material directly consumable by their trees.

### 4.1 The structural mismatch

Our `docs/spec/*.md` are **plain-text ASCII-banner documents with a `.md` extension** — `=====` rules, hard wraps at ~70 columns, `§`-numbered sections, no GFM tables, no cross-links, no anchors. Their standard is GitHub-Flavored Markdown: one H1 per page, nav lines top and bottom, GFM tables for command entries, `proseWrap: never`, hyphen bullets, no hard wraps.

This is why they mirror rather than link our content: our segments cannot be rendered, anchored, or link-checked. Converting is the change that makes every other alignment item cheaper.

| Their convention | Our current state | Action |
| --- | --- | --- |
| GFM, one H1, nav lines | ASCII banners | Convert `docs/spec/` to GFM |
| No hard wraps, Prettier `proseWrap: never` | Hard-wrapped ~70 cols | Reflow on conversion |
| Hyphen bullets | Mixed `•` and `-` | Normalize |
| `## RIP_XXX` + italic summary + GFM table for Level/Command/Arguments | Fixed-column text blocks | Restructure command entries |
| Anchors follow GitHub slug rules | None | Falls out of conversion |
| Lowercase filenames except `README.md` | `01-wire-format.md` etc. — already compliant | None |
| Citation form `repo:path/file.c` | Ad-hoc `src/foo.c:123` | Adopt their form in shared material |
| Chapter `9` pinned as the reference chapter | Segments `01`-`11`, appendices at `10` | See §4.2 |

### 4.2 Numbering — align, but do not renumber for its own sake

Their scheme is two-layer (`N.M-slug.md`) ordered by learning concern, with chapter `9` pinned as the reference chapter in every version so deltas line up across trees. Ours is a 12-segment linear spec.

A full renumber would invalidate every `§` citation in their three riplib trees — the exact anchors they built. **Recommendation: do not renumber.** Instead publish a mapping table (`docs/spec/README.md`) from our segments to their chapters, so a reader of either can cross-walk. Convert the *format* (Track B), keep the *numbering* stable, and let the map absorb the difference. Their `CONTRIBUTING.md` explicitly warns against rewriting section numbers that merely happen to read like version numbers; the same caution applies in reverse.

### 4.3 Tooling worth mirroring

- `run/check-links` — validates every file and anchor target across the doc trees. Our finding 12 (silently broken anchor) is precisely what this catches. Worth a riplib equivalent once `docs/spec/` is GFM.
- `run/lint` / `run/format` — Prettier with a checked-in `.prettierrc`. Cheap to adopt.
- Their `.gitattributes` discipline for `.rip`/`.ans` — **directly relevant to us**: those files are CP437 and their line endings are significant test data. Our `tests/fixtures/compat/*.rip` are exactly this class. We should confirm our `.gitattributes` exempts them from LF normalization; if it does not, our fixtures are one `core.autocrlf` setting away from silent frame-hash failures.

### 4.4 The `§A2G` question — Track A finding 10 and Track B are the same problem

Our extension sections are numbered `§A2G.1` through `§A2G.13`. `A2G` is derived from **A2GSPU** — a consumer name. `design/decisions.md:15-21` permits that name only in the README Origins / Reference-target paragraphs and `cmake/arm-none-eabi.cmake`. By that rule, the section prefix on our own protocol extensions is out of compliance, and it now propagates through bbs-land's two extension trees, their version tables, and every `§A2G.N` citation in `CONFLICTS.md`.

Two honest options:

1. **Rename** to a neutral prefix (`§RL.N`) with an alias table. Cost: invalidates roughly twenty external citations in a repo that has already published them, plus our own `docs/spec/06`/`06a`, README, and CHANGELOG.
2. **Declare `§A2G` an opaque revision tag** — not an abbreviation to be expanded — and amend the constraint with a proper log row to say so. Cost: the constraint gets one documented exception. Nothing external breaks.

**Recommendation: option 2.** The prefix is three letters that expand to nothing for any outside reader, the wire IDs (`RIPSCRIP031001`/`RIPSCRIP032001`) carry no branding, and the coordination cost of renaming exceeds the leak. Doing the amendment *properly* — candidate row, ADR, log line — also demonstrates the fix for finding 6.

`README.md:16` (finding 10) then resolves by dropping the parenthetical expansion, not the tag: "v3.1 (§A2G.1-7 extensions)" rather than "v3.1 (A2GSPU §A2G.1-7 extensions)".

---

## 5. Track C — the 22 conflict items

Recommended disposition for every item, with the code-logic reasoning where it is contested.

### 5.1 Baseline conflicts

**All dispatch-dependent items below were SETTLED on 2026-08-12** by extracting the DLL's command dispatch table (RVA 0x080820, 129 entries). See `docs/spec/12-dll-provenance.md` §12.8 for the adjudication and `docs/spec/13-dll-command-table.md` for the verbatim table. Verdicts are decided by recorded arity and argument types, which either admit a proposed name or exclude it.

| ID | Item | Recommendation | Basis |
| --- | --- | --- | --- |
| B1 | `\|W` write-mode numbering | **CONCEDE — SETTLED BY DISASSEMBLY** | Handler stores the wire byte unmodified; RVA 0x00E6B3 maps 1→`R2_XORPEN`, 2→`R2_MERGEPEN`, 3→`R2_MASKPEN`. Wire order is **0=COPY, 1=XOR, 2=OR, 3=AND, 4=NOT** — their table exactly. §BUG.7 withdrawn |
| B2 | `\|J` base math vs SAVE_ICON | **CONCEDE — settled** | Table records **1 arg (mega2)**; RIPlib's SAVE_ICON claims 2. Record's `base_math:2` fits |
| B3 | `\|f` world frame vs FONT_ATTRIB | **CONCEDE — settled** | Table records **2 args, both XY**; FONT_ATTRIB needs two MegaNums. `§A2G.3` must relocate |
| B4 | The punctuation block (8 opcodes) | **Split verdict — mostly concede** | `\|+`/`\|[`/`\|]` share one 7-arg signature (a family); `\|_` takes 6 args not 1 point; `\|<` is variable-length. **`\|&` supports RIPlib** (5 args XY,XY + 3 modes) |
| B5 | `\|K` filled rectangle vs KILL_MOUSE_EXT | **CONCEDE — settled** | Table records **4 XY args** — a rectangle |
| B6 | `\|D`/`\|d` palette vs pattern | **CONCEDE BOTH — settled by name** | Handlers name themselves `RIP_SetDrawingPalette` and `RIP_OneDrawingPalette`. `\|y` is `RIP_ExtendedFontStyle`, confirming their reading that font style lives there |
| B7 | `\|2R` define vs perform refresh | **CONCEDE — settled** | Table records **1 arg (mega4)**; RIPlib claims zero |
| B8 | Level 1/2 letter swaps (6 commands) | **Concede — settled by name** | `\|1i` names itself `RIP_ImageStyle` and no `S`/`s` exists in the Level 1 band. The ESC-introduced command names itself `rip_query` |
| B9 | Fill pattern `00` | **Concede for bars/rects; leave polygon open** | Localized |
| B10 | Built-in pattern bitmaps | **Verified — they are right; qualify §A2G.4** | Doc fix now, bitmap swap optional |
| B11 | `\!` attributed as an extension | **Accept** | Free |
| B12 | SOH/STX introducers missing | **Implement** | Additive parser change |

**B2 / B3 / B5 — why our position is weak.** All three rest on negative findings in `§11.1`: _"no function string containing 'scroller' found"_, _"Not present as a named function in the DLL"_, _"no implementation found in the DLL export table or function strings."_ Against that they hold positive wire evidence — the same 90 of 116 prologues, plus TeleGrafix's own authored comment. Under the symmetric rule stated in §2, absence in a disassembly loses to presence on the wire. We should either produce a positive dispatch-table entry or concede.

`B3` carries a real downstream cost worth stating plainly: **`§A2G.3` (font attributes) rides on `|f`.** If `|f` is `RIP_SET_WORLD_FRAME`, our font-attribute command needs a different letter, and `3.1-riplib/ripscrip/3.0-text-direction-and-font-attributes.md` moves with it. That is a cost of being right, not a reason to resist the finding.

**B4 — the provenance ask, and our own precedent for answering it honestly.** `§DEV.4` already carries an origin note conceding that _"whether any of these mirror undocumented RIPSCRIP.DLL behaviour or are wholly RIPlib-original is an open provenance question, U-026."_ We have therefore already accepted, in-tree, that name-provenance is a legitimate question. Their side of `B4` is unusually strong: six of the eight names come **verbatim from TeleGrafix's own comments in `NEWCMDS.RIP`**, a demo script written to introduce those very commands and shipped in the same product as the DLL. If our names came from DLL strings, publishing them settles it in an afternoon. If they were inferred from the 2.00a4 draft, the corpus wins.

**B9 — the spec text is explicit and we chose otherwise.** The 1.54 entry reads _"Fill pattern 00 will set the entire fill area to the background color."_ We skip the fill entirely for every primitive. Our pattern-0 fast path is already special-cased at `src/drawing.c:355`, so the change is localized to the fill entry points. Their nuance is worth keeping: SyncTERM's scanline polygon filler also skips style 0 while bars and floods paint color 0, so the polygon case is genuinely unsettled everywhere. Align bars and rectangles; leave polygon documented as divergent.

**B10 — verified 2026-08-11; their claim is correct in full.** `§DEAD.6` claims v3.1 implements patterns 9-11 _"with their correct 8×8 bitmaps per the Borland BGI specification."_ That part holds — BGI 9/10/11 map to three distinct bitmaps. But their wider claim also holds, and our own source is the proof:

- `src/drawing.c:51` declares `static const uint8_t fill_patterns[11][8]` — **eleven** bitmaps plus one user slot, not thirteen.
- `rip_bgi_fill_to_card` (`src/ripscrip.c:695-706`) collapses two wire values onto one bitmap: `case 5: return 2; /* BKSLASH→ diagonal \ */` and `case 6: return 2; /* LTBKSLASH→ diagonal \ (no lighter variant) */`. The comment states the defect outright.
- Four wire values resolve to approximations, matching their count exactly: `3` (LTSLASH→light diagonal), `5` and `6` (both→diagonal `\`), `8` (XHATCH→50% checker, commented _"closest dense X feel"_).

So the `05`/`06` collapse `§DEAD.6` set out to remove was **relocated into the wire-mapping layer, not eliminated**, and `§A2G.4`'s _"all 13 patterns natively"_ is overstated. `U-029` closes: answer is yes.

Two follow-ons. First, `§A2G.4` and the README feature list must be qualified to "11 built-in bitmaps + user pattern, with four BGI styles approximated" — this is a correctness fix to our own claims regardless of what we do about the bitmaps. Second, the deeper concession stands independently of the count: **"correct per Borland BGI" is not the same standard as "correct per RIPscrip."** The RIPscrip specification prints eight byte values per pattern and RIPterm implemented those faithfully — including the famously wrong Light Backslash bytes `A5 D2 69 B4 5A 2D 96 4B`, which SyncTERM still ships as documented. Byte-exact fidelity matters when a fill tiles against era artwork. Adopting the spec's printed bytes would fix the count and the collapse together; keeping ours is defensible but must become a *recorded* choice.

The same function carries a branding leak for Track A: `src/ripscrip.c:691` reads _"the closest visually-matching **card** pattern."_

There is a second distinction here we should concede regardless of the count: **"correct per Borland BGI" is not the same standard as "correct per RIPscrip."** The RIPscrip specification prints eight byte values per pattern and RIPterm implemented those faithfully — including the famously wrong Light Backslash bytes `A5 D2 69 B4 5A 2D 96 4B`, which SyncTERM still ships as documented. Byte-exact fidelity matters when a fill tiles against era artwork. Our independently chosen bitmaps are a defensible choice but should be a *recorded* one, not an accident.

**B12 — implement it; it is additive and it retires X5.** Syntax rule 12 allows `!` to be replaced by SOH (`0x01`) or STX (`0x02`), accepted anywhere in a line, deliberately host-only. The shipped 2.x corpus opens with the SOH form (`\x01|*`). We recognize `!` at four positions only, so that content will not start on RIPlib. The change cannot break existing streams — those control bytes are not legal in RIP text — and it makes `X5` unnecessary.

### 5.2 Extension collisions

| ID | Item | Recommendation |
| --- | --- | --- |
| X1 | v3.1 text variables | **Split** — concede `$YEAR$`; rename the other three |
| X2 | v3.2 time variables | **Concede** — adopt their names |
| X3 | `\|Y` direction `01` redefined | **Stop redefining `01`; keep `02`** |
| X4 | Font attribute bit assignment | **Relocate the feature — `\|q` is `RIP_FontAttrib`** |
| X5 | CSI-relaxed `!` trigger | **Drop, once B12 lands** |
| X6 | `\|28` gradient unattested | **CONCEDE — settled against RIPlib.** No digit-letter command exists in the Level 2 band and no gradient handler name appears in any string class. `RIP_GRADIENT_FILL` is **not in this driver**, so the v3.0-baseline attribution is wrong and `§A2G.13` extends a command the binary does not contain |
| X7 | `<<DEBUG>>` directive | **Make it opt-in, off by default** |

**X1 — real, but their stated failure mode is wrong, and the correction is ours to offer.** Their page says a host sending `$COMPAT(0)$` to a `RIPSCRIP031001` terminal _"instead gets the literal string `1` rendered."_ That is not what the code does. `src/rip_variables.c:249` scans forward to the matching `$`, so `$COMPAT(0)$` yields `vname = "COMPAT(0)"`, `vlen = 9`. The handler at `:455` tests `vlen == 6 && memcmp(vname, "COMPAT", 6)` and does not match; expansion falls through to `vval_len = -1`, _"unrecognized, emit literal."_ The whole token passes through as visible text.

So the parameterized forms — `$COMPAT(env)$`, `$COPY(...)$`, `$PROT(...)$` — fail **visibly**, not silently. That is a materially less dangerous failure than described, and worth sending upstream as a correction.

The conflict itself still stands and we should still fix it: we are squatting on three bare names the language reserves for parameterized actions, and a terminal that renders `$COMPAT(0)$` as literal text is still broken from the host's point of view. **`$YEAR$` is the genuinely silent one** — bare form on both sides, 4-digit for us, 2-digit in the record, with `$FYEAR$` already meaning the 4-digit value. Concede `$YEAR$` outright; rename our three squatters.

**X2 — the cheapest concession in the register, take it in full.** These are bare-name collisions with different values on both sides, so they *are* silently wrong in the way X1 only partly is. `src/rip_variables.c:539` (`HOUR`), `:567` (`DOW`), `:591` (`MONTH`) collide with documented meanings: `$HOUR$` is 01-12 in the record, not 00-23; `$DOW$` is spelled out (`Friday`), not a digit; `$MONTH$` is the full month name (`December`), not `MM`. Our own README example — `<<IF $DOW$=4>>Happy Friday!<<ENDIF>>` — evaluates false on every conforming 3.x terminal, and neither side errors.

The record already names every value we want: `$MHOUR$`, `$WDAY$` (0 = Sunday), `$MONTHNUM$`, `$DAY$` for our `$DOM$`. Adopting them clears the entire group at **zero cost to the feature** — it is a rename in `rip_variables.c` plus the README and `docs/spec/07-variables.md`.

**X3 — we redefined a documented value, and `§DEAD.4` is why.** Our `§DEAD.4` records that the DLL accepted direction=1, stored it, and rendered bottom-to-top text that was _"unreadable in English … the feature was documented but functionally broken."_ That is a fair reading of the implementation. But 1.54 states the behavior as intent, not defect: _"Vertical text is drawn with the base-line to the right, and is read from bottom to the top."_ Bottom-to-top is conventional for a rotated axis label; "unreadable" is a judgment, not a bug report.

Redefining `01` means content authored against either side reads upside-down on the other. **Recommendation: restore `01` to the documented bottom-to-top reading, keep `02` (CCW) as the clean addition it is, and add a third value for the corrected top-to-bottom rendering** rather than taking over `01`. We lose nothing — practical risk is low either way, since no corpus scene uses vertical `|Y` text — and we stop being the implementation that silently reinterprets a documented value.

**X4 — resolved differently than either side framed it.** Handler naming shows a font-attribute command **does** exist in the driver, on **`|q`** (`RIP_FontAttrib`, 1 argument), not on `|f` (which is `RIP_SetWorldFrame`). So `§A2G.3` is not wrong to exist — it is on the wrong letter, and should move to `|q` rather than be abandoned. The facing-bit layout still needs the handler body read before the `04`/`08` question can be settled either way.

Original framing, retained for context: **this one we may well win, but only with the bit layout published.** They cite `RIP_EXTENDED_FONT_STYLE`'s `<style>` field as the canonical place facings live: `01` bold, `02` italic, `04` **strike-out**, `08` **underline**. We use `04` underline, `08` shadow. Our `§DEAD.1` is a positive binary finding — the DLL _"parsed font_attrib bits (bold, italic, underline, shadow) … and stored them in the GFXSTYLE structure"_ — and under the symmetric rule, positive binary evidence beats an inference from an adjacent command. Publishing the actual GFXSTYLE bit offsets would likely settle this in our favour. Note it is compounded by `B3`: the feature rides on the contested `|f`.

**X7 — their two objections are sound; mitigate rather than defend.** First, `<<NAME>>` expands to text variable `NAME` anywhere in a command's argument text, so `<<DEBUG msg>>` is syntactically indistinguishable from a macro reference to a variable named `DEBUG`. Second, and more serious: everything a 2.x/3.x terminal sends is a *response* — auto-sense, mouse/button host command, `RIP_QUERY` result, file-query answer. Unsolicited terminal-to-host traffic has no precedent, and a BBS sitting at a prompt reads inbound bytes as keystrokes. `>DEBUG: entering menu render` plus CR is a menu selection.

Our README currently calls it _"safe to leave in production."_ That claim does not survive their second point. **Recommendation: default `<<DEBUG>>` to off, gate it behind an explicit opt-in, and correct the README.**

### 5.3 Free corrections — accept all three

- **N1** — `§DEV.4` marks `|!`, `|(`, `|)` and `|1R` as RIPlib extensions. All four are documented 1.54 commands (RIP_COMMENT, RIP_GROUP_BEGIN/END, RIP_READ_SCENE) and behavior matches in every case; only the standing is wrong. Four rows leave the deviation register at zero behavioral cost. `1V`, `1X` and the backtick composite-icon command remain genuine additions.
- **N2** — `§11.2` Erratum 2 gets the letters right (`x` filled poly-bezier, `z` unfilled) but `§A.1` lists only `z`. Table omission; fix the table.
- **N3** — `§A2G.1` presents AND and NOT as v3.1 additions. bbs-land argued they were documented modes since 2.00 Alpha 1, so the *language* always had them. **Disassembly goes further than either project claimed:** the translation at RVA 0x00E6B3 maps wire 3 → `R2_MASKPEN` and wire 4 → `R2_NOT`, so AND and NOT were **implemented and live** in the shipping driver. `§DEAD.3`'s "parsed but never rendered" is wrong, and `§A2G.1`'s claim to activate dead code is unfounded. It should be restated as a completeness fix to RIPlib's own renderer and dropped as a protocol extension entirely.

---

## 6. What this nets out to

| Category | Items | Character |
| --- | --- | --- |
| Concede on our own evidence | B1, N1, N2, N3, X2, part of X1 | We are already on record agreeing; the docs lag |
| Concede on the symmetric rule | B2, B3, B5, B9(bars) | Negative disassembly evidence loses to positive wire evidence |
| Publish provenance and let it decide | B4, B6, B7, B8, X4, X6 | Costs no code; may go either way |
| Implement | B12 (retires X5), B10 (pending verify) | Additive |
| Redesign slightly | X3, X7, rest of X1 | Small, and each removes a real hazard |

The honest summary is that a majority of the register resolves against RIPlib on evidence we ourselves collected and published. That is not a bad outcome — it means the disagreements are tractable, and the one class where we are strongest (positive binary findings) is the class we have not yet made citable.

---

## 7. Sequenced plan

**Phase 0 — ledger first.** Land the candidate row and log line (§9). Nothing else starts until the constraint amendment in §4.4 is recorded properly.

**Phase 1 — free wins and the scrub (no protocol change).**
- Track A findings 1, 2, 5, 7, 8, 9, 11, 12.
- Restore the branding prohibition + `scripts/check-branding.sh` + CI wiring (finding 4).
- N1, N2, N3, B11 — deviation-register corrections.
- Send bbs-land the X1 failure-mode correction from §5.2.

**Phase 2 — B1.** Four `#define`s, the §2.3 table, retire `§BUG.7`, CHANGELOG entry. Add a `|W` compat fixture, which currently does not exist.

**Phase 3 — the provenance publication.** Dump the DLL dispatch table and string references; annotate `docs/spec/` opcode claims with their binary citations. This is the deliverable that settles B4, B6, B7, B8, X4, X6 in whichever direction the evidence points. Their `CONFLICTS.md` notes items 2, 4 and part of 7 would likely fall out of a single such pass.

**Phase 4 — variable renames.** X2 in full, X1 minus the three squatters, `$YEAR$` conceded.

**Phase 5 — parser work.** B12 (SOH/STX), drop X5, B9 for bars and rectangles, X7 opt-in gating.

**Phase 6 — docs conversion.** Track B: `docs/spec/` to GFM, segment↔chapter map, link checker.

**Phase 7 — opcode reassignments.** B2, B3, B5 and whatever Phase 3 did not save. Largest blast radius; do it last, on evidence, in one coordinated release.

---

## 8. What to send upstream now

Before any of the above ships, three things are worth sending, because they cost nothing and establish good faith:

1. **The B1 concession, with the git receipt.** Our v1.0.0 comment matched their table; T-004 overwrote it. Their evidence and ours agree, and the header is the outlier.
2. **The X1 correction.** `$COMPAT(0)$` renders as literal text, not as `1` — verified at `rip_variables.c:249` and `:455`. Their conflict stands, their stated failure mode does not.
3. **A provenance commitment for B4.** Say plainly which of our opcode names came from DLL strings or dispatch analysis and which were inferred. They asked; the answer decides eight opcodes either way.

---

## 9. Ledger integration

This proposal is net-new first-party work on a library `design/decisions.md:47-58` declares to be in **steady state**. That note permits only three forward moves absent consumer demand. The demand condition is met here — an external standardization effort has raised 22 numbered conflicts against our published spec and is mirroring our extensions into three of its trees — but the exemption should be *recorded*, not assumed.

Proposed candidate row:

```
| C-018 | Align RIPlib with the bbs-land RIPscrip standardization record | alive | | | 2026-08-11 | 22-item conflict register (B1-B12/X1-X7/N1-N3) raised by bbs-land/remote-imaging-protocol against riplib @ 3e05ecb, plus 12 code-review findings on the same commit. Phased plan in design/bbs-land-alignment.md. Headline: B1 write-mode renumbering is refuted by our own §BUG.7/§DEAD.3 and by the pre-T-004 code comment — concede and fix drawing.h. | full |
```

Proposed log line:

```
2026-08-11 | bbs-land-alignment-proposal | trace | External standardization effort (bbs-land/remote-imaging-protocol, CC0) has reconciled three vendor trees against riplib @ 3e05ecb and raised a 22-item conflict register. Worked up a phased alignment proposal (design/bbs-land-alignment.md) covering the register, the 12 code-review findings on the same commit, and their documentation standards. No code changed; no constraint amended. Steady-state exemption claimed on consumer-demand grounds and recorded here rather than assumed. | n/a | alignment,bbs-land,C-018,steady-state-exemption
```

The §4.4 `§A2G` decision additionally needs an ADR, since it amends the platform-independence constraint — the omission that code-review finding 6 flagged.

---

## 10. Open questions this raises

| ID | Question | Decides |
| --- | --- | --- |
| U-028 | Which `docs/spec/` opcode names came from DLL strings/dispatch analysis, and which were inferred from the 2.00a4 draft? | B4, B5, and the credibility of every unsourced name |
| ~~U-029~~ | ~~Does `src/drawing.c` collapse wire patterns `05`/`06` onto one bitmap?~~ | **CLOSED 2026-08-11 — yes.** `src/ripscrip.c:700-701` maps both to bitmap 2; the table holds 11 bitmaps, not 13. See B10 |
| U-030 | What are the actual GFXSTYLE facing-bit offsets in the DLL? | X4 |
| U-031 | What is the provenance of `\|28` RIP_GRADIENT_FILL, attributed to DLL 3.0.7 but absent from every bbs-land source? | X6, and the baseline `§A2G.13` builds on |

`U-024` (`1M` reserved-field width) and `U-025` (`1D` DEFINE grammar) are already open in `design/knowledge.md` and both appear in their register — `B8` cites `U-025` by name. bbs-land's corpus may answer both directly, which is worth asking them.

---

## 11. Execution readiness

Assessed 2026-08-11. **Roughly two-thirds of the register is executable from this document as written; one phase is hard-blocked on an artifact not present in this environment; four items need a decision that is not mine to make.**

### 11.1 Executable now — no further input needed

Phase 0 (ledger row + log line, both written out verbatim in §9), and all of Phase 1 except the branding decision:

- Track A findings 1, 2, 5, 7, 8, 9, 11, 12 — every one has an identified file, line, and replacement approach. `consumer-handoff/a2gspu/` **is present on disk**, so the "extract, don't delete" destination for finding 5 exists.
- Finding 4 — restore the prohibition, add `scripts/check-branding.sh` with a working regex (`-E` or `-P`; the recorded verification grep was a no-op twice over), wire it into `.github/workflows/build.yml`.
- `N1`, `N2`, `N3` and the `\!` half of `B11` — deviation-register corrections, all four self-contained.
- `B10` documentation fix — qualify `§A2G.4` and the README feature list to the verified "11 bitmaps + user slot, four styles approximated."
- The three upstream sends in §8.

Phase 2 (`B1`) is also fully specified — four `#define`s, the §2.3 table, retire `§BUG.7`, CHANGELOG — but see §11.3, it changes rendering.

A toolchain is available (`D:/dev`: arm-gcc, cmake, ninja; MSVC per the 2026-05-30 log entry), so anything needing a build or a regenerated `.expect` frame hash can be verified rather than shipped blind.

### 11.2a The method IS recorded — and the substrate was deleted, not lost

Recovered 2026-08-11 from git: **`docs/historical/ripscrip-v3-RE-notes.md`**, 3,070 lines, removed in commit `5a76df8` _("Roll RE deviations/errata/bugs into v3.1 spec, deprecate RE doc")_. Retrieve with `git show 5a76df8^:docs/historical/ripscrip-v3-RE-notes.md`. It opens with the full methodology:

```
File:         RIPSCRIP.DLL  (592,896 bytes)
Format:       32-bit Windows PE (i386)
Build Date:   October 16, 1997
Build Path:   C:\src\rip3\dll32\
Method:       Export table enumeration (153 exports)
              String table extraction (180+ function names)
              Error message cross-referencing
              Disassembly of selected entry points
```

Cross-referenced against v2.A4 (26,713 lines) and the RIPtermJS reference implementation. **This is fully reproducible** — every step is a standard, cheap operation (`dumpbin /exports`, `strings`, and DIE, which is already on the archive drive at `E:/20251006_archive/archive_drive/die_win64_portable_3.10_x64/`). The byte size, build date and the embedded build-path string `C:\src\rip3\dll32\` together fingerprint the artifact, so a re-obtained DLL can be confirmed identical before any of the prior analysis is trusted.

**But the method's shape decides several register items.** It was export-table and string-table *led*, with disassembly of *selected* entry points only. Therefore:

- Where a name appears in the exports or strings, the evidence is **positive and publishable** — this is the citable provenance `B4` asks for.
- Where segment 11 says "not found", the true claim is **"absent from the export table and string table"**, not "absent from the code." A static, non-exported function carrying no error string is invisible to this method. That is a far weaker claim than segment 11's wording implies, and it is exactly the weakness bbs-land pressed on.

**The consolidation into segment 11 was lossy, and it hardened uncertainties into false negatives.** Three cases, each checkable in the recovered file:

| Item | What the RE notes actually say | What segment 11 says | Verdict |
| --- | --- | --- | --- |
| `B3` `\|f` | Line 471-477 documents `RIP_SET_WORLD_FRAME` with **positive DLL string evidence** — _"DLL strings: 'WORLD', 'WORLDW', 'WORLDH'"_ | _"no implementation found in the DLL export table or function strings"_ | **Direct contradiction. Our own primary source backs bbs-land.** `§A2G.3` is sitting on an occupied letter |
| `B5` `\|K` | Line 1032-1044 documents `RIP_FILLED_RECTANGLE` as a real v2.0 command, letter **unknown** — _"Command: [from v2.0 TOC entry 3.4.1.20]"_ | _"Not present as a named function in the DLL … making this redundant"_ | "Letter unknown" became a negative claim. **Concede** |
| `B2` `\|J` | Line 494-518 puts base math on **`'b'`**, sourced from the v2.A4 spec text with the collision erratum. **`SAVE_ICON` appears nowhere in the document** | `\|J` = `SAVE_ICON` | Our `\|J` assignment has **no DLL basis at all**. **Concede** |

**`B4` is now answerable, and the answer is theirs.** None of RIPlib's eight punctuation-block names — `ICON_STYLE`, `TEXT_XY_EXT`, `SCROLL`, `FILL_POLYGON_EXT`, `POLYLINE_EXT`, `DRAW_TO`, `BUTTON_EXT`, `GET_IMAGE_EXT` — appears anywhere in the RE notes (the `SCROLL` hits are the unrelated `ripScrollback*` callbacks and `RIP_SCROLLER`). Neither do bbs-land's. So those names came from neither DLL strings nor dispatch analysis. Their `B4` ask states the consequence precisely: _"if inferred, the corpus decides."_ TeleGrafix's own comments in `NEWCMDS.RIP` decide it. **Concede B4.**

This is the same root cause as `B1`: a lossy roll-up replaced sourced findings with unsourced summaries, and no one could check the difference once the substrate was deleted. It is also the same defect class as the code-review findings — content removed rather than preserved.

**Recommendation: restore `ripscrip-v3-RE-notes.md` to the repository** as the provenance substrate segment 11 summarizes, and cite into it. One caveat before it goes back to a public path: it contains at least one consumer reference (_"as the A2GSPU project does"_), so it needs the same scrub pass as the rest of Track A.

**One ecosystem finding worth forwarding.** The RE notes record that the original TeleGrafix developers publicly stated an intent to release the complete RIPscrip source — RIPaint, RIPterm, RIPtel and the driver DLL — under an open-source license via a TeleGrafix GitHub organization, announced June 2025 in the "RIPScrip Art Resurrection" Facebook community. bbs-land's `reference/rights.md` currently describes the rights position as _"effectively in limbo"_, so this is directly material to them, and a source release would settle every open item in the register definitively. Verify the attribution before citing: our note says _"Jeff and Mark Hayton"_ while bbs-land credits _"Jeff Reeder, with Jim Bergman and Mark Hayton"_, so the names may be conflated.

### 11.2b UNBLOCKED — the artifact was found, and both evidence bases are in hand

**Located 2026-08-11 at `E:/archive_drive/RIPtel/`** — a complete extracted RIPtel 3.1 install. Identity verified against the RE notes' fingerprint:

| Fingerprint | Recorded | Found | |
| --- | --- | --- | --- |
| Size | 592,896 bytes | 592,896 bytes | ✅ |
| Build date | October 16, 1997 | Oct 16 1997 | ✅ |
| Format | 32-bit PE (i386) | `MZ` header confirmed | ✅ |
| Build path | `C:\src\rip3\dll32\` | `rip3` ×8, `dll32` ×7 present | ✅ |
| MD5 | — | `bade8b1f4e467ac7ad4edb2639738d4c` | recorded for future verification |

**Critically, the directory holds _both_ projects' primary sources**: `Ripscrip.dll` (ours), plus `RIPSCRIP.HLP` (38 KB) and `RIPTEL.HLP` (347 KB) string tables and an `ICONS/` demo corpus of 321 files — 35 `.RIP` scenes plus `.FN`/`.DEF`/`.RET`/`.MSE`/`.OVR` scripts (theirs). Every item in the register can now be settled locally, from either side.

The method reproduces immediately: 6,814 printable strings, **137 unique `RIP_*` function names**, and embedded source-module assertions of the form `riprocmd.cpp - RIP_PortDelete()` / `r_ports.cpp - portDelete()`.

**First-pass findings from the string table alone — every one goes against RIPlib:**

| Item | Evidence in the binary | Verdict |
| --- | --- | --- |
| `B3` `\|f` | **`RIP_SetWorldFrame` is present.** Segment 11's basis for reassigning the letter — _"no implementation found in the DLL export table or function strings"_ — is **false** | **Settled for bbs-land.** `§A2G.3` must move off `\|f` |
| `B2` `\|J` | `RIP_SetBaseMath` present; **no `SaveIcon` string of any form exists** | **Settled.** RIPlib's `\|J` = SAVE_ICON has no binary basis |
| `B4` punctuation block | **None of RIPlib's eight names exist** — no `TEXT_XY_EXT`, `FILL_POLYGON_EXT`, `POLYLINE_EXT`, `BUTTON_EXT`, `GET_IMAGE_EXT`, `DRAW_TO`, `ICON_STYLE`. In fact **no `*_EXT` name appears anywhere in the DLL.** Meanwhile `RIP_PolyPolygon` and `RIP_PolyMarker` — bbs-land's `\|<` and `\|;` — **are both present** | **Settled for bbs-land** |
| `B5` `\|K` | `KILL_MOUSE_EXT` does not exist in the binary | **Settled.** Concede |
| `B6` `\|D`/`\|d` | **Both `RIP_SetDrawingPalette` and `RIP_OneDrawingPalette` are present**, exactly as bbs-land reads them. `RIP_ExtendedFontStyle` also exists separately — consistent with their claim that it is `\|y`, a different command | **Strong support for bbs-land** |
| `B7` `\|2R` | `RIP_RefreshAvailable` and `RIP_RefreshSend` both present — a refresh *negotiation/definition* mechanism, not a bare "perform refresh" | **Leans bbs-land** |
| `B8` `\|1G`/`\|1g` | **`RIP_Scroll` and `RIP_CopyBlit` both present as distinct functions.** RIPlib collapses these into one `COPY_REGION` | **Corroborates their two-command split** |
| `N1` `\|1R` | **`RIP_ReadScene` is in the binary.** `§DEV.4` calls it _"RIPlib-original beyond the published TeleGrafix tables"_ | **Wrong; concede as already recommended** |
| `X6` `\|28` | **No gradient function name of any kind appears in the string table**, though RIPlib attributes `RIP_GRADIENT_FILL` to this DLL | Consistent with bbs-land's "unattested". Needs the dispatch dump before calling |

**The corpus corroborates independently.** `|fZKQO` appears verbatim in `BUTTONS.RIP`, `CURVES.RIP` and others — the exact world-frame string (1280×960) bbs-land cited — and `J10` appears in **22 of the 35 `.RIP` scenes** here, matching their "90 of 116" ratio on a fuller extraction. `ZKQO` read as world dimensions is meaningful; read as RIPlib's `attrib:2 res:2` font attribute it is noise that happens to be the right length.

### 11.2b-i Method fully reproduced — and segment 11's negative claims rest on a category error

A second, independent copy was found at **`C:/RIPtel/`** (fresh install from the original installer). Both copies hash identically — `bade8b1f4e467ac7ad4edb2639738d4c` — giving two provenance chains for the same artifact, and both ship the same 321-file `ICONS/` corpus.

Every step of the recorded method now reproduces and verifies:

| Recorded in the RE notes | Reproduced 2026-08-12 |
| --- | --- |
| 592,896 bytes, PE i386, Oct 16 1997 | ✅ exact |
| Build path `C:\src\rip3\dll32\` | ✅ `rip3`, `dll32` present |
| **Export table enumeration (153 exports)** | ✅ **`dumpbin /exports` → "153 number of functions, 153 number of names"** |
| String table extraction (180+ function names) | ✅ 6,814 strings, 137 unique `RIP_*` |
| Error message cross-referencing | ✅ `riprocmd.cpp - RIP_PortDelete()` form recovered |

**But enumerating the exports exposes a flaw in how segment 11 states its negative findings.** The 153 exports are the DLL's **host-facing API** — `RIP_EngineCreate`, `RIP_InstanceInit`, `RIP_ProcessBuffer`, `RIP_StreamWrite`, `RIP_GetDriverVersion`, palette getters, block-mode and temp-file helpers (one still carrying its MSVC mangling, `?RIP_SetDefaultSettings@@YAHPAURIPINST@@@Z`). **Not one RIPscrip command handler is exported.** `RIP_SetWorldFrame`, `RIP_OneDrawingPalette`, `RIP_PolyPolygon`, `RIP_SetBaseMath` — none appear in the export table, and none ever would; they are internal.

Segment 11 repeatedly grounds a negative claim on exactly that table, e.g. RIP_WORLD_FRAME having _"no implementation found in the DLL export table or function strings."_ Absence from the export table says **nothing whatever** about whether a command exists — no command is there. And the string table only surfaces functions that happen to carry an assertion string of the `module.cpp - Func()` form, which is a subset of the whole.

So the register's negative findings were never as strong as their wording implied, and in the `B3` case the claim is **false on its own terms**: `RIP_SetWorldFrame` *is* in the string table. This is the third distinct failure mode found in segment 11, after the unsourced `B1` renumbering and the lossy `B5`/`B2` roll-up, and it argues for rebuilding the register from the recovered substrate rather than patching it item by item.

**What remains for the dispatch dump.** The string table gives names, not opcode↔handler bindings. Still requiring disassembly of the dispatch table at the addresses recorded in `consumer-handoff/a2gspu/dll-reference.md` (`ripParseStateMachine` `0x10039E90`, jump table `0x1003AB9C`): the exact letter each function binds to, `B1`'s in-code write-mode ordering, `X4`'s GFXSTYLE facing-bit offsets, and `X6`'s gradient question. DIE is already available at `E:/20251006_archive/archive_drive/die_win64_portable_3.10_x64/`.

### 11.2c Superseded — the former blocker

**RIPSCRIP.DLL is not reachable from this environment, and is not on the archive drive either.** There is no `~/src/rip-tools/`, nothing on `D:/`, and the `E:/` archive drive was searched on 2026-08-11 — a full-depth search for `*riplib*` returns **zero hits**, and a scan for `ripscrip*.dll` / `riptel*` / `rtel*.exe` / `ripterm*` likewise finds nothing. The segment-11 disassembly was performed elsewhere, and segment 11 records its *conclusions* without the underlying citations.

What the archive does hold is RIP material of a different generation: `E:/20251006_archive/archive_drive/A2DVI-Firmware-master/RIPscrip154.txt` and `RIPscrip200a4.txt` (the two published texts our spec cites as secondary sources), `E:/20251006_archive/rip/` with third-party 1.5x-era tooling (`RIPTOOLS.ZIP` — a 1994 Turbo Pascal toolkit with a 66 KB manual; `RIPSK101.ZIP` — RIPsketch 1.01), a probable RIPterm 1.51 distribution nested inside `prog113_116.zip` as `0151TER1/2.ZIP`, and an earlier `rip-project/` effort. None of it is the 3.0-era driver.

**This has a consequence wider than the alignment.** The whole of `11-dll-deviations.md` — `§BUG.1` through `§BUG.9`, `§DEAD.1` through `§DEAD.8` — rests on a binary that is not in the repository, not on the archive drive, and not cited to specific offsets or function addresses. Those findings are currently **unfalsifiable by anyone, including us**. That is a documentation-integrity problem in its own right, independent of what bbs-land thinks, and it is the same root cause as their `B4` provenance ask. Recovering the artifact is therefore worth doing even if the alignment work stops today.

Recovery is cheap and the path is documented upstream: bbs-land catalogues `artifacts/riptel-3.10/rtel3100.exe` (RIPtel Visual Telnet 3.10 installer, Win16 NE) with both a Wayback source and a `files.bbs.land` mirror, and records `RIPSCRIP.DLL` 3.0.7 as extracted from it. Fetching that installer and re-extracting the DLL restores the evidence base for Phase 3 **and** re-grounds segment 11.

This blocks the single highest-value phase, and with it:

- `B4`, `B6`, `B7`, `B8`, `X4`, `X6` — all stay open, because each is answered by a dispatch-table or string-table citation we cannot currently produce.
- `B2`, `B3`, `B5` — default to **concede**, since the only thing that would save them is positive binary evidence.
- Phase 7 (opcode reassignments) — depends entirely on Phase 3.

To unblock: either restore the DLL (the RIPtel 3.10 installer is catalogued upstream at `artifacts/riptel-3.10/rtel3100.exe`, mirrored on `files.bbs.land`) and re-run the dispatch dump, or retrieve the original analysis notes if they survive outside this repo. Until then, Phase 3 cannot start and the six open items should be reported upstream as *unresolved-pending-artifact* rather than argued.

### 11.3 Needs a decision before proceeding

| # | Decision | Why it is not mine |
| --- | --- | --- |
| 1 | `§A2G` — rename to a neutral prefix, or declare it an opaque revision tag and amend the constraint? (§4.4) | Amends a binding constraint and affects ~20 external citations |
| 2 | `X1` — what do `$COMPAT$` / `$COPY$` / `$PROT$` become? This document says "rename" without naming the replacements | Names are a product choice; they go on the wire |
| 3 | Deprecation policy for `X2`/`X3` renames — hard swap, or keep the old names as aliases for a release? | Trades cleanliness against breaking content already authored against v3.2 |
| 4 | `X7` — gate `<<DEBUG>>` by compile-time flag or runtime API? | An API-surface choice |

### 11.4 Behavior changes needing sign-off

`B1`, `B9` and `X3` all change what appears on screen for existing content. `B1` in particular means any stream authored against RIPlib's current numbering renders differently afterwards. These are the right changes on the evidence, but they are not silent ones, and each wants a CHANGELOG entry and a version bump rather than a quiet fix. `B9` additionally requires regenerating the `fill_and_shapes` fixture hash.
