# Knowledge

Project-specific knowledge that accumulates across decisions: heuristic rules,
prior-art surveys, and open empirical questions. The /decide workflow reads
from here at Step 0 and writes back at Phases 1, 6, and 7.

---

## Heuristic rules (HR-NNN)

Operational rules extracted from Phase 7 surprises, post-mortems, or retrospectives.
Each rule must change a future decision.

### HR-001 — Spec deviations must be documented in `docs/spec/11-dll-deviations.md`
- **Rule**: When the implementation intentionally diverges from the original
  TeleGrafix RIPscrip behavior (DLL, RIPterm, spec text), record the deviation
  in `docs/spec/11-dll-deviations.md` with the rationale. Silent divergence is
  prohibited.
- **Origin**: Repo already contains `11-dll-deviations.md`; this rule formalizes
  the existing practice so future audits catch new silent drifts.
- **Applies to**: any change to ripscrip.c / ripscrip2.c protocol behavior that
  doesn't match the v1.54 / v2.0 / v3.0 historical specs.
- **Recorded**: 2026-05-25

### HR-002 — Extraction/refactor output must be brace-balanced (and compiled when possible) before it's logged "done"
- **Rule**: Any subsystem extraction or large mechanical refactor (awk/sed-driven
  lifts especially) must be verified before being recorded complete: at minimum a
  per-file `{`/`}` balance census, and a real compile when a toolchain is available.
  A non-zero `{`/`}` delta in exactly the touched file is a near-certain dropped/added
  brace. Reconcile any apparent delta against char/string literals first
  (e.g. `src/ripscrip.c` carries three `'{'` command-letter literals → a harmless +3).
  Untracked / uncommitted WIP is **not** covered by CI — CI only runs on pushed
  commits — so when there is no local compiler, a build is genuinely unverified and the
  report must say so rather than implying "done = builds".
- **Origin**: The 2026-05-30 Opus-4.8 re-audit found a CRITICAL build-breaker (missing
  `}` closing `rip_query_prompt_begin`, `src/rip_variables.c:148`) introduced by the
  C-002 step-2 variable-engine extraction and shipped undetected by the prior audit.
  Root cause of the miss: no native C compiler in this environment + the file was
  untracked so CI never saw it. A brace-census caught it instantly (rip_variables.c
  was the only module with a non-zero delta).
- **Applies to**: every future C-002-style extraction and any audit pass that produces
  code without a green build.
- **How to apply**: after an extraction run
  `for f in <touched>; do echo "$f $(grep -o '{' $f|wc -l) $(grep -o '}' $f|wc -l)"; done`
  and reconcile non-zero deltas; commit so CI can compile it; if no local compiler,
  state that explicitly. See `memory/feedback_riplib_no_compiler_verification.md`.
- **Corollary (added 2026-05-30 during C-012..C-016 execution)**: re-read each
  finding against a *fresh, clean* read of the actual code before editing. Three
  audit findings were falsified at execution time — the `1U` 1-segment button
  (already correct via a registration fallback), the clipboard blit re-assert
  (invariant already holds), and a phantom `rip2_copy_scaled` "stray break"
  (an artifact of garbled/partial tool output). Garbled reads and over-cautious
  agents both manufacture phantom findings; a clean re-read is the cheap filter.
- **Recorded**: 2026-05-30

### HR-003 — Local newlib(ARM)+MSVC cannot reproduce glibc/host link bugs; CI is ground truth for link-time + platform-divergence issues
- **Rule**: A clean local build is necessary but NOT sufficient. The local
  toolchains (arm-none-eabi-gcc = newlib bare-metal, library-only; MSVC = math
  in CRT) structurally cannot surface a host gcc/clang **link** failure such as
  a missing `-lm`. For anything that only manifests at the final executable
  link or differs by libc/platform (libm, POSIX feature-test macros, `-lpthread`,
  linker GC, ABI), treat **CI as the only ground truth** and verify there before
  declaring done. Watch the run to completion; do not infer green from a partial
  local pass.
- **Origin**: 2026-05-30. After pushing the audit work (e2f5652) with local
  verification "238/238 on MSVC + clean ARM", CI went RED — 6/10 jobs failed
  (ubuntu×2, macOS×2, sanitizers, coverage; all host gcc/clang). The two
  passing-and-relevant local configs (MSVC, library-only ARM newlib) are
  exactly the two that CANNOT reproduce a glibc/clang host failure.
  CAUTIONARY half of the lesson: I then guessed the cause (missing libm,
  "supported" by `nm` showing `U sinf`), pushed a PRIVATE→PUBLIC link fix
  (c6e59c2, kept — it is correct for a static lib regardless), AND wrote a doc
  entry claiming "all 10 green" — which was FALSE and unverified (c6e59c2's CI
  failed with the identical 6-job set, proving libm was not the cause). The
  `nm` evidence was confirmation bias: real but not the actual failure. Never
  record a CI result you have not read.
- **Applies to**: any CMake `target_link_libraries` visibility choice on a
  static lib; any "it builds locally, ship it" moment; release checklists.
- **How to apply**: a library's external deps (libm, pthread, …) must be
  `PUBLIC`/`INTERFACE` on a STATIC lib so they reach the final link. After any
  push, poll the run via the GitHub REST API (`/actions/runs?branch=main`,
  cache-bust the URL) until `status=completed`, then confirm `conclusion=success`
  and spot-check the per-job list.
- **Recorded**: 2026-05-30

---

## Prior art register

Inspected references, libraries, papers, comparable projects, blog posts, RFCs.
Kept so the next /decide run doesn't redo the same search.

| Source | Type (lib/paper/repo/post/RFC) | Inspected for | Date       | Verdict (use / learn-from / reject) | Notes |
|--------|--------------------------------|---------------|------------|--------------------------------------|-------|
| `docs/historical/RIPSCRIP_v154.DOC` (232 KB) | spec | v1.54 protocol ground truth | 2026-05-25 | use | TeleGrafix original 1993 specification — authoritative for Level 0/1 |
| SQLite (`sqlite3.h` + `sqliteInt.h`) | lib | opaque-struct pattern with internal-header opt-in for tests | 2026-05-25 | learn-from | Canonical "opaque public + tests use sqliteInt.h" model. Closest fit for RIPlib's eventual variant B. |
| libcurl (`CURL *`) | lib | fully opaque, no internal-header escape hatch | 2026-05-25 | learn-from | Conservative end of the spectrum. Forces accessor functions for everything. Would be variant A for RIPlib. |
| libgit2 (`git_*` opaque types) | lib | opaque public + private internal headers | 2026-05-25 | learn-from | Same pattern as SQLite. Modern C convention. |
| zlib (`z_stream`) | lib | public struct, fields documented as access-via-API | 2026-05-25 | learn-from | Opaque-by-policy reference point. Closest to RIPlib's chosen variant C. Works in practice for a well-known consumer audience. |
| libnotify, libgudev premature-opacification regret | post | What goes wrong when you opacify too early — multiple breaking-API rounds until accessor set settles | 2026-05-25 | learn-from | Strong argument for staging C → B rather than jumping straight to B. |
| `docs/historical/RIPSCRIP_v2A4.PRN` (1.5 MB) | spec | v2.0 protocol ground truth | 2026-05-25 | use | TeleGrafix 1994 specification — authoritative for Drawing Ports + Level 2 |
| `docs/spec/01-wire-format.md` through `11-dll-deviations.md` | spec | RIPlib's own normative spec for v1.54+v2.0+v3.0+v3.1+v3.2 | 2026-05-25 | use | The repo's first-party protocol contract. Audit reads vs implementation must reconcile here |
| Original BGI / SDL_bgi / WinBGIm | lib | Drawing primitive comparison (README table) | 2026-05-25 | learn-from | Comparison documented in README §Comparison; RIPlib intentionally exceeds in fill patterns, write modes, font attrs |
| RIPSCRIP.DLL (TeleGrafix RIPterm) | reverse-engineered binary | DLL field offsets cited throughout `include/ripscrip.h` and `src/ripscrip.c` | (historical) | learn-from | Reverse-engineering notes baked into public header comments — see candidate to relocate them |
| `docs/spec/11-dll-deviations.md` §BUG.7 / §DEAD.2 / §11.2 | spec (RE deviation register) | confirming canonical write-mode order, BGI font table order, and the command-letter-collision convention for C-012 | 2026-05-30 | use | §DEAD.2 states "offsets-before-widths table order" → corroborates that `bgi_font.c` parse code is correct and its :58-61 comment is wrong; §BUG.7 confirms the 0=COPY/1=OR/3=XOR wire order is already honoured (write-mode finding is clean); §11.2 Erratum 1 shows the established "disambiguate by argument length" convention. Establishes the precedent that for this RE'd dead protocol, observed DLL behaviour is ground truth and the published spec is the side corrected. |

---

## Open empirical questions

Questions whose empirical answer would change a design analysis — typically because
the constraint or behavior they probe is not documented in any authoritative source.
Open questions are not failures; they are the next move with a name. **Answered
unknowns stay here with the answer recorded** — they're findings worth keeping.

Unknowns are **grouped by the candidate that raised them**, because decisions often
surface multiple related questions that share status flow (open → answered) and tend
to be answered together (one spike or one hardware test resolves several).

### Cross-cutting (raised by the 2026-05-25 audit)

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-001 | Is `output.pgm` at repo root (committed 2026-03-21, 256 KB) intentional or stray test output? `.gitignore` rule `*.pgm` post-dates this commit | 2026-05-25 | answered 2026-05-25 | Stray. `git ls-files output.pgm` was empty and `git log -- output.pgm` had no commits — the file was never tracked, just a working-tree artifact from running the demo per README. Deleted in T-001. |
| U-002 | Does the A2GSPU vendored copy diverge from RIPlib HEAD today? `scripts/check-a2gspu-parity.sh` can answer if A2GSPU repo path is provided | 2026-05-25 | open | — |
| U-003 | What is the actual on-target memory footprint of `libriplib.a` on RP2350 (text/data/bss split)? CI builds the archive but doesn't size-report | 2026-05-25 | partially answered 2026-05-25 | Addressable now: C-009's CI hardening pass added a per-object `arm-none-eabi-size` step to `.github/workflows/build.yml` (the `embedded-rp2350` job). The first CI run after the next push will produce the concrete text/data/bss numbers. |
| U-004 | Does any production RIPscrip BBS in the wild emit v2.0 Drawing Port commands (`!|2P` `!|2s`) that exercise the path-not-just-state save/restore? Affects how aggressively port system can be simplified | 2026-05-25 | open | — |
| U-005 | What does the `RIPSCRIP032001` ID actually mean in the v3.1/v3.2 wire? The string is RIPlib-defined (not TeleGrafix); will real RIPterm/clients accept or reject it on `ESC[!` probe? | 2026-05-25 | open | — |

### Raised by C-001 — De-couple consumer-specific content from RIPlib's public surface

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-006 | What's the complete inventory of A2GSPU/Apple-II-specific content currently in RIPlib? Need a grep-based census before deciding which holding-area scheme (A/B/C) is right | 2026-05-25 | open | — |
| U-007 | Does A2GSPU's `libriplib/` integration layer (the vendored copy's `platform_a2gspu.c`, `CMakeLists.txt`, `README.md`) have a natural home for extracted RE notes + ProDOS time-sync rationale + DLL field offsets, or does it need a new design-doc layer? | 2026-05-25 | open | — |
| U-008 | Is `term_cell_t` / `TERM_MAX_COLS/ROWS` actually consumed by any current code (riplib or A2GSPU), or is it dead forward-declaration? Audit found no callsite inside riplib. If dead, drop instead of relocate | 2026-05-25 | answered 2026-05-25 | Dead inside RIPlib. Repo-wide grep returned only the declaration in `include/riplib_platform.h:120-123` and references in design/handoff docs. No source/test/example consumer. Drop from public header (tracked as part of C-004 phase 1 or as a standalone trace). A2GSPU's own consumer-side usage is its own problem and tracked in the handoff `integration-notes.md`. |
| U-009 | Same question for the compositor stub set (`comp_passthrough_vt100`, `comp_set_cursor`, `comp_clear_screen`, `comp_clear_line`): is the parser actually calling them in current code paths, and what does A2GSPU substitute when it vendors? | 2026-05-25 | answered 2026-05-25 | LIVE in RIPlib. `src/ripscrip.c` calls into all four stubs at 8 sites (line numbers: 2406, 3317, 3321, 3331, 3349, 4426, 4482, 4504-5, 4519, 4521, 4538). Cannot drop without redesigning how the parser notifies the host about VT100/cursor events. Structural fix is C-004 territory (variant A would replace with explicit callback registration; variant B leaves them as-is and just documents the contract). |

### Raised by C-002 — Decompose ripscrip.c monolith

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-010 | How much of `ripscrip.c`'s 100+ command handlers are exercised by real BBS streams vs dead-code legacy? Affects whether decomposition should drop unused command surface or preserve it | 2026-05-25 | open | — |
| U-011 | Does the monolithic file actively cause coverage gaps via test-discoverability problems (audit observed that Level 3 prefix has no tests despite state 13 being defined)? | 2026-05-25 | open | — |

### Raised by C-004 — Multi-session correctness pass

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-012 | Is multi-session RIPlib usage a real scenario today, or speculative? A Synchronet-style multi-connection BBS server would need it; a single-card embedded use would not. The answer drives whether (A) breaking-change-with-explicit-state or (B) document-single-session-constraint wins | 2026-05-25 | open | — |

### Raised by C-003 — rip_state_t opaque-by-policy

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-020 | How many direct-field-access sites does `tests/test_ripscrip.c` have? | 2026-05-25 | answered 2026-05-25 | **211 sites** (grep against an ~40-field union). Heavy white-box. Drives the design: variant A (full opacification + accessor calls) would require rewriting all 211 sites; variants B and C leave them untouched (B via opt-in private header, C by keeping the struct visible). |
| U-021 | Does A2GSPU's vendored copy (`platform_a2gspu.c` + their integration glue) access `rip_state_t` fields directly outside of the public API? | 2026-05-25 | open | Cannot be answered from this environment. Resolution gates the timing of any future variant-B promotion: if A2GSPU is API-only today, B can land at the next major; if A2GSPU has direct accesses, B requires coordinated migration. Inspect their source tree to answer. |
| U-022 | Are there legitimate consumer patterns (debugging, serialisation, hot-path optimisation) that require permanent direct field access? | 2026-05-25 | open | Should be answered before any future variant-B promotion. If yes, the accessor set for B needs to cover those patterns; if no, B can be strict. |

### Raised by C-006 — Test-coverage gap pack

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-013 | Is there a documented hash-update workflow for the 6 compat fixtures (`tests/fixtures/compat/*.expect`)? When a legitimate bug fix changes rendered output, a maintainer needs to know how to re-bless safely. Test agent flagged this as undocumented | 2026-05-25 | open | — |
| U-014 | Are there historical RIP wire captures (from archived BBSes, e.g., textfiles.com) that could be added as regression fixtures? Currently the 6 fixtures are synthetic | 2026-05-25 | open | — |

### Raised by C-007 — Code safety fix pack

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-015 | Does the existing test suite ever exercise deeply-nested `<<IF>>` (>256 deep) that would trigger the `preproc_overflow` uint8_t wraparound that code-safety agent flagged? If yes, the bug is latent; if no, fuzzing should find it after C-011 lands | 2026-05-25 | answered 2026-05-30 | **Moot — the widening is correct and over-provisioned.** The 2026-05-30 re-audit verified the increment site is *saturating* (`if (preproc_overflow < UINT16_MAX) preproc_overflow++`), so the wraparound is unreachable at *any* depth regardless of test coverage; `test_preproc_depth_overflow` only drives depth 9, but saturation (not the field width) is what makes it safe. uint16 is extra headroom; even uint8 would be safe with the saturating guard. |
| U-016 | Are there any BBS implementations known to send `$QUERY$` responses longer than 63 bytes? Audit found `rip_query_response_byte` lacks a length cap before NUL | 2026-05-25 | open | — |

### Raised by C-009 — CI hardening pack

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-017 | Does the Windows CI runner use MSVC or MinGW? Affects which sanitizer story is even possible (MSVC has its own ASan since VS2022) | 2026-05-25 | open | — |
| U-018 | What is the current `arm-none-eabi-gcc` version on Ubuntu-latest runners, and how often does it drift across GitHub runner image refreshes? Drives whether pinning is urgent or just hygiene | 2026-05-25 | resolved 2026-05-25 | Moot — C-009 pinned the toolchain to `13.2.Rel1` via `carlosperate/arm-none-eabi-gcc-action@v1.10.1`, decoupling CI from the runner image's apt repo entirely. Whatever the apt-shipped version drift would have been, it no longer matters. |

### Raised by C-011 — Property-based + fuzzing infrastructure

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-019 | Would AFL/libFuzzer on the parser surface bugs not caught by the current 16K-random-byte test? Would need a short spike to find out | 2026-05-25 | open | — |

### Raised by the 2026-05-30 Opus-4.8 re-audit

| ID    | Question | Date raised | Status | Answer + source |
|-------|----------|-------------|--------|-----------------|
| U-023 | Does the now-fixed working tree actually compile + pass `ctest`? | 2026-05-30 | answered 2026-05-30 | **YES.** A local toolchain was found (D:\dev: arm-none-eabi-gcc 14.2 + cmake 3.30 + ninja; VS18/MSVC 19.50) — the "no compiler here" premise was wrong (stale build-* dirs pointed at a gone C:/Users/brad mingw). ARM cross-build: clean compile+link, all 13 objects → libriplib.a. MSVC host build + `ctest`: **238/238 pass** (after fixing 2 pre-existing coordinate bugs in C-006 gap tests that surfaced on first real run — they checked RIP_PIXEL 'X' output at unscaled Y, but 'X' applies scale_y=y*8/7, identity only for y<=6). T-006 + all C-012/013/014/015/016 edits now compile-and-test verified, not just brace-censused. Full 3-OS CI matrix not yet exercised, but local gcc 14.2 + MSVC 19.50 cover the main compiler-divergence risks. |
| U-024 | Ground truth for `1M` mouse reserved field: did the original DLL/RIPterm consume 11 chars (spec §3.2) or 17 (code, with res:4)? Decides whether to fix the code or amend the spec | 2026-05-30 | open | — Needs DLL/RIPterm disassembly or a real `1M` wire capture. NOTE (2026-05-30): deliberately NOT recorded in `11-dll-deviations.md` — it is an unresolved question, not a decided deviation; the code keeps its current 17-char behaviour pending evidence but the project has not blessed it. When answered, either fix the code (if DLL used 11) or document a real deviation + correct spec §3.2 (if DLL used 17). |
| U-025 | Ground truth for `1D` DEFINE wire grammar: does the real DLL carry the `flags:3 res:2` prelude the code expects, or the bare `name=value` of spec §3.18? | 2026-05-30 | open | — Same source as U-024. Same handling: not in the deviations register until resolved; code keeps the flags-prelude grammar meanwhile. |
| U-026 | Are the undocumented commands `1V`/`1X`/`1R` + Level-0 backtick/group/comment markers RIPlib-original extensions or recovered (undocumented) DLL behaviour? Decides whether C-012 documents them as extensions or as DLL commands | 2026-05-30 | open | — |
