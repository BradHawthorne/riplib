# Changelog

All notable changes to RIPlib are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2026-05-11

The protocol-complete + sanitizer-clean milestone.

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
