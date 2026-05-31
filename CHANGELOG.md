# Changelog

All notable changes to RIPlib are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.3.0] — 2026-05-31

Minor release that completes the reentrant multi-session API surface.
Purely additive — no behaviour change to any existing entrypoint, fully
backward compatible with 1.2.x. (Candidate C-004 variant A′; see
`design/adr/0004-multi-session-state-family-completion.md`.)

### Added
- Reentrant `*_state()` counterparts for the four host-event entrypoints
  that previously existed **only** as `g_rip_state`-bound globals:
  `rip_sync_date_byte_state()`, `rip_sync_time_byte_state()`,
  `rip_query_response_byte_state()`, and `rip_apply_palette_state()`.
  The `SESSION SAFETY` block in `include/ripscrip.h` documents a
  two-flavour contract — *"every public entrypoint comes in two
  flavours"* — but before this release that contract was unfulfilled for
  these four functions, so a multi-session embedder could not feed host
  time-sync, query responses, or apply a saved palette to a specific
  non-global session. The complete reentrant surface now lives in one
  block in the header.
- `tests/test_ripscrip.c`: `test_state_api_sync_query_palette_isolation`
  drives two sessions and asserts the new `_state()` forms operate on the
  passed `rip_state_t` independent of `g_rip_state` (`test_ripscrip` is
  now 240 checks; 287 total across the three suites).

### Changed
- The existing single-session globals (`rip_sync_date_byte`,
  `rip_sync_time_byte`, `rip_query_response_byte`, `rip_apply_palette`)
  are now thin wrappers over their `_state()` forms — byte-for-byte
  identical single-session behaviour, mirroring the existing
  `rip_mouse_event_ext` / `rip_file_upload_*` pattern. The documented
  `rip_save_palette(s)` ↔ `rip_apply_palette()` "API asymmetry" is
  resolved: `rip_apply_palette_state(s)` is the matching explicit-state
  form.

### Notes
- The `g_rip_state` singleton and the convenience globals remain for the
  common single-session embedded case. Full removal of the singleton (a
  breaking change) stays parked as candidate **C-004-A** pending a
  concrete multi-session consumer — see
  `design/adr/0004-multi-session-state-family-completion.md` and
  `design/decisions.md`.

## [1.2.2] — 2026-05-30

Audit-and-portability patch release. Fixes the host (gcc/clang) build and
static-library linking, corrects a scalable-text scale bug, hardens scene
state, and reconciles documentation with the source. No public API or
protocol-surface changes — fully backward compatible with 1.2.1.

### Documentation
- Documentation accuracy pass against the current source tree:
  - README file-structure now lists the modules extracted from
    `ripscrip.c` (`rip_preproc.c`, `rip_variables.c`, `rip_clipboard.c`,
    `riplib_version.c`) and the `riplib_version.h` header, and drops the
    stale "(4900+ lines)" annotation.
  - README test counts corrected to **285 total** checks
    (`test_drawing` 41, `test_ripscrip` 238, `test_compat` 6). The
    [1.2.1] note below quoted the earlier 275/228 figures, which the
    test-suite growth has since superseded.
  - README "platform-independent" wording corrected: the library uses
    `string.h`/`stdlib.h`/`stdio.h` (`snprintf`) and `math.h`
    (`sinf`/`cosf`/`atan2f`/`sqrtf`) and links `libm` — not "no libc
    beyond memcpy/memset, no floating point."
  - `docs/spec/01-wire-format.md` banner version corrected v1.2.0 → v1.2.1.
- `docs/spec/11-dll-deviations.md` rewritten so it records only
  deliberate, decided deviations (§DEV.1-5); spec text was corrected to
  match the code (icon lookup order §9.2, scalable-text range §5.9,
  the RIPlib-extension commands in §A.1) and unresolved items moved to
  `design/knowledge.md`.

### Fixed
- `26` SCALABLE_TEXT scale was bit-masked `& 0x07` (silently corrupting
  valid scales — e.g. 10 became 2); now clamped to the BGI renderer's
  real 1-10 range, matching the `|Y` RIP_FONT_STYLE size field.
- `src/bgi_font.c` now includes `<stddef.h>` for `size_t` — fixes the
  host gcc/clang build (newlib/MSVC pulled it in transitively).
- CMake links `libm` `PUBLIC` so static-library consumers (tests,
  examples, downstream apps) resolve the math symbols.
- The libFuzzer `-fsanitize=fuzzer` flag is scoped to the `fuzz_parser`
  target instead of global flags, so the CMake compiler-probe no longer
  fails to configure.

### Changed
- The `|#` (RIP_NO_MORE) scene terminator now defensively closes any open
  text block, so a malformed stream that omits `|1E` before `|#` cannot
  bleed stale text into the next scene's REGION_TEXT. Well-formed streams
  (which send `|1E`) are unaffected; full state reset remains `|*`.
  Covered by a new regression test (`test_ripscrip` is now 239 checks).
- Audit-driven comment/contract accuracy fixes (no behaviour change):
  documented `draw_text`'s font-buffer size contract (≥ 256×font_height
  bytes) in `drawing.h`; the `$RAND$` comment now states the real
  guarantee (deterministic Knuth/POSIX LCG) instead of unverifiable
  bit-for-bit DLL compatibility; corrected the `RIP_TEXT_WINDOW` routing
  comment and documented the full-screen-window heuristic as deviation
  §DEV.6.

## [1.2.1] — 2026-05-11

Patch release for the RIPscrip v3.2 surface.

### Fixed
- `ESC[!` now advertises the same v3.2 wire ID as `$RIPVER$`:
  **`RIPSCRIP032001`**.
- `|^` / `|~` now preserve and re-apply the full drawing prelude:
  colors, write mode, fill style/background, 16-bit line pattern,
  line thickness, font and extended font fields, draw cursor, viewport,
  and filled-border state.
- Level 2 Drawing Port save/restore now preserves custom line patterns.
- The drawing backend now accepts the full 16-bit RIPscrip user line
  pattern instead of truncating to an 8-bit approximation.
- The `$DOW$` Friday example in the v3.2 spec now matches the Monday=0
  convention.

### Documentation
- README coverage wording now describes RIPlib as a portable
  rendering/parser core, with host-owned operations called out explicitly.
- README test counts updated to 275 total checks:
  `test_drawing` 41, `test_ripscrip` 228, `test_compat` 6.

## [1.2.0] — 2026-05-11

**Bumps the supported protocol from RIPscrip v3.1 → v3.2** by
defining six quality-of-life extensions as §A2G.8 through §A2G.13.
All additions are backward compatible — they use new command
letters, new `$VARIABLE$` names, or new values for previously-
validated parameter fields.  A v3.0 / v3.1 client sees the
additions as either no-ops or as literal text that falls through
`$XYZ$` unrecognized-variable handling.

Protocol ID advertised by `$RIPVER$` and the ESC[! probe response
is now **`RIPSCRIP032001`**.

### Added (v3.2 protocol — §A2G.8 through §A2G.13)
- **State push/pop stack** — `|^` and `|~` save/restore the drawing
  prelude (colors, fill/line/write state, font fields, draw cursor,
  viewport).  Bounded to 8 frames; overflow drops silently, pop on
  empty is a no-op.  Cleared by `|*` and session reset.  See §A2G.8.
- **Layout / introspection variables** — `$CX$` `$CY$` `$VPW$` `$VPH$`
  `$VPCX$` `$VPCY$` `$CCOL$` `$CFCOL$` `$CBCOL$` expose current
  drawing state.  Use case: "center this text" without hardcoding
  320,200.  See §A2G.9.
- **Time component variables** — `$HOUR$` `$MIN$` `$SEC$` `$DOW$`
  `$DOM$` `$MONTH$` extend the existing `$DATE$` / `$TIME$` family.
  All fall back to local RTC when host hasn't synced.  See §A2G.10.
- **EGA color-name aliases** — `$BLACK$` through `$WHITE$` each
  expand to the 2-digit MegaNum of the EGA palette index.  Useful
  in `<<IF>>` comparisons and in text bodies.  See §A2G.11.
- **`<<DEBUG msg>>` preprocessor directive** — pushes
  `0x3E DEBUG: <msg>\r` to TX, suppressed by enclosing
  `<<IF false>>` branches.  Safe to leave in production scripts
  (hosts that don't recognize the prefix drop the line).  See §A2G.12.
- **Radial gradient** — `|28` gains mode 2 (radial), in addition to
  the existing horizontal (0) and vertical (1) modes.  Per-pixel
  interpolation by normalized squared distance, using the FPU we
  already require for §A2G.5 trig.  Existing clients sending
  mode 0/1 are unaffected.  See §A2G.13.

### Changed
- `$RIPVER$` now reports `"RIPSCRIP032001"` (was `"RIPSCRIP031001"`).

### Tests added (+14)
- `test_state_stack_*` — push/pop roundtrip, pop-on-empty, overflow
- `test_var_*` — CX/CY/VPW/VPH/VPCX/VPCY, CCOL/CFCOL, $RED$, $LIGHTMAGENTA$,
  HOUR/MIN, DOW/DOM/MONTH
- `test_preproc_debug_*` — DEBUG emits to TX, suppressed inside false IF
- `test_l2_gradient_radial_mode` — radial gradient renders pixels

Test count: 254 → 268 total (drawing 41, ripscrip 222, compat 5).

## [1.1.0] — 2026-05-11

The protocol-complete + sanitizer-clean milestone.  Builds on the
initial v1.0.0 release with a systematic spec/code/test audit that
surfaced 18 protocol bugs, drove every documented command through
wire-level tests, and locked in coverage + sanitizer + analyzer
guards in CI.

### Added
- Full RIPscrip protocol coverage: every one of the **99 documented commands**
  across Level 0 (38), Level 1 (20), Extended (27), and Level 2 (14) is parsed
  and dispatched, with at least one wire-level test per command.
- **251 regression tests** across three suites:
  - `test_drawing` (41) — drawing primitives
  - `test_ripscrip` (205) — protocol parser, state, FSM, text variables,
    mouse regions, icon caching, BMP/ICN ingestion
  - `test_compat` (5) — golden-frame FNV-1a hash fixtures
- CI matrix: Linux/macOS/Windows × Debug/Release, plus three guard jobs:
  - **sanitizers** — UBSan + ASan + `detect_leaks=1` on Linux
  - **static-analysis** — `gcc -fanalyzer` warnings-as-errors
  - **coverage** — per-file floor thresholds (gcov) prevent regression
- `examples/rip2ppm` — a CLI that reads a `.rip` wire stream and writes a PPM
  image, demonstrating end-to-end rendering without any platform glue.
- v3.1 (§A2G) extensions: AND/NOT write modes, vertical text direction
  correction, font attribute rendering, native fill patterns, palette index
  correction, extended text directions.

### Reverse-engineered (documented bugs found and fixed during audit)
- **L1**–**L18**: 18 distinct protocol-layer bugs identified by spec/code/test
  cross-reading. Fixes are in `git log` between the initial implementation and
  v1.0.  Notable examples:
  - L13: BMP parser rejected truncated and zero-height images correctly only
    after the audit added explicit guards.
  - L14, L16a/b, L17a/b: silent NULL-font draw_text calls in text paths.
  - L18: 1D handler double-wrote `app_vars[0]` via a legacy fallback even
    when the modern `=` syntax already stored the value.

### Coverage (line %, gcov, Linux gcc -O0)
| File | Coverage |
|---|---|
| `src/rip_icn.c` | 97% |
| `src/drawing.c` | 88% |
| `src/ripscrip2.c` | 88% |
| `src/bgi_font.c` | 85% |
| `src/rip_icons.c` | 84% |
| `src/ripscrip.c` | 84% |

### Notes for consumers
- After a `!|cmd|` sequence the FSM is in `RIP_ST_COMMAND` (state 2), waiting
  for either another command letter or `CR`/`LF`.  Raw text bytes fed without
  a trailing newline will be interpreted as command letters, not text.  Feed
  `\n` or call `rip_session_reset()` between scenes if you intend the next
  bytes to be text.
- Tests that create more than one `rip_state_t` should call
  `psram_arena_destroy(&s.psram_arena)` between init calls or track every
  arena base for cleanup; ASan with `detect_leaks=1` will catch leaks.
