# Extraction audit trail

Running log of which file:line locations had A2GSPU-specific content
removed and where each piece landed in `consumer-handoff/a2gspu/`.

Useful if A2GSPU's maintainers need to reconcile their vendored copy of
RIPlib with the cleaned-up upstream, or to trace any specific historical
comment back to where it now lives.

## 2026-05-25 — initial C-001 sweep

### Public headers — top-of-file ownership comments

| File                       | Before                                                  | After                                          |
|----------------------------|---------------------------------------------------------|------------------------------------------------|
| `include/drawing.h`        | "Unified rendering primitives for A2GSPU card"          | "Unified rendering primitives for RIPlib"      |
| `include/bgi_font.h`       | "Borland BGI stroke font renderer for A2GSPU card", "from C:\\RIPtel\\FONTS\\" | "Borland BGI stroke font renderer for RIPlib", "from the RIPtel font set" |
| `include/rip_icons.h`      | "RIPscrip icon loader for A2GSPU card", "built from RIPtel BMPs" | "RIPscrip icon loader for RIPlib", "built from the RIPtel BMP set" |
| `include/rip_icn.h`        | "BGI putimage (.ICN) format parser for A2GSPU card"     | "BGI putimage (.ICN) format parser for RIPlib" |
| `include/ripscrip.h`       | "RIPscrip 1.54 graphics protocol parser for A2GSPU card", DLL field offsets in mouse-region struct, "640×350 EGA coordinates scaled to 640×400 framebuffer", "A2GSPU v3.1: ..." comments throughout, "Codex FIX N" labels, "FIX *" labels | Generic "RIPscrip 1.54+ protocol parser for RIPlib", DLL references → `consumer-handoff/a2gspu/dll-reference.md`, scaling note generalized, "A2GSPU v3.1" → "v3.1" |
| `include/ripscrip2.h`      | "RIPscrip 2.0 protocol extensions for A2GSPU card", "A2GSPU v3.1 Extensions — dead-code activation from DLL binary analysis" | Generic "RIPscrip 2.0+ protocol extensions for RIPlib", DLL-binary-analysis note moved to dll-reference.md |
| `include/riplib_platform.h` | "minimal types and stubs that replace A2GSPU card-specific dependencies", "On embedded (RP2350), this is a bump allocator in 8MB PSRAM" | Generic platform-abstraction language; PSRAM sizing context → `integration-notes.md` |

### Source-file top-of-file comments

| File                  | Action                                                         |
|-----------------------|----------------------------------------------------------------|
| `src/drawing.c`       | Renamed banner from "for A2GSPU card" to "for RIPlib"          |
| `src/bgi_font.c`      | Same                                                           |
| `src/ripscrip.c`      | Same; one-time disclaimer added that historical "Codex FIX N" and "FIX *" labels remain inline for the moment and will be scrubbed in a follow-up sweep |
| `src/ripscrip2.c`     | Same                                                           |
| `src/rip_icons.c`     | Same                                                           |
| `src/rip_icn.c`       | Same                                                           |

### Content lifted out wholesale

- DLL state-machine address `0x10039E90`, jump table `0x1003AB9C`, mouse-region binding `0x1000A964`, Level 2 chord RVA `0x012663`, `ripTextVarEngine` `0x026218` → `dll-reference.md`
- Mouse field record offsets (+0x20, +0x2B, +0x2C, +0x2D, +0x32, +0x3E, +0xA0) → `dll-reference.md`
- "DLL maintains 36 independent GDI DCs (one per port slot)" — generalized to "port spec defines 36 independent drawing surfaces" → `dll-reference.md` for the DLL context
- ProDOS MLI GET_TIME / CMD_SYNC_DATE / CMD_SYNC_TIME byte protocol rationale → `integration-notes.md`
- PSRAM arena 1 MB rationale → `integration-notes.md`
- `term_cell_t` / `TERM_MAX_COLS/ROWS` rationale → `integration-notes.md` (the symbols stay in `riplib_platform.h` for now because A2GSPU references them; long-term should move to A2GSPU)
- "Codex FIX N" → "FIX *" label index → `integration-notes.md`

### Things deliberately left in place

- `cmake/arm-none-eabi.cmake` keeps RP2350 / Cortex-M33 / Pico 2 names
  (the toolchain legitimately targets that specific chip) but its
  A2FUSION-card / A2GSPU-firmware / dual-RP2350 / "Processor V vs B" /
  Facebook board-reference URL content was extracted (those are
  consumer-specific deployment details that don't belong in the
  library's build glue).  RIPlib stays portable across any RP2350 board
  by default and across other Cortex-M33 boards with the flag tweaks
  noted in the file comment.
- `README.md` mentions A2GSPU and A2FUSION in the *Origins* and
  *Reference target* sections. That's the documented exception — the
  user's directive explicitly allows one README acknowledgement.
- Compositor stubs (`comp_*`) and `term_cell_t` declarations in
  `riplib_platform.h` remain functionally — RIPlib still calls them —
  but their docstrings are now generic. The structural cleanup is
  tracked as audit candidate C-002 / C-004.

### Inline source-comment cleanup (partially done in this pass)

More than originally planned for "phase 1" got scrubbed:

- `src/ripscrip.c`: bulk-replaced "A2GSPU v3.1" → "v3.1", "RP2350 RTC"
  → "local RTC", "IIgs bridge loop" → "host bridge", "A2GSPU:" →
  "RIPlib:"; rewrote the $DATE$/$TIME$, $BEEP$/$BLIP$, $RIPVER$,
  $QUERY$, state-machine docstring, and rip_sync_*_byte /
  rip_query_response_byte function-header blocks to drop Apple II /
  IIgs / ProDOS / RP2350 / A2GSPU language.
- `src/ripscrip2.c`: scrubbed the file header and the inline
  "(A2GSPU v3.1 extension)" / "On the RP2350 single-framebuffer"
  comments.
- `src/drawing.c`: scrubbed the file header and the two Apple-II
  character-ROM references; replaced RP2350-FPU specific notes with
  generic FPU-equipped-target language.
- `src/bgi_font.c`: scrubbed file header and the lone "A2GSPU v3.1"
  inline note.
- `docs/spec/`: scrubbed Apple II / A2GSPU / RP2350 / IIgs / ProDOS
  references from 01-wire-format.md (§1.3 and §1.8), 03-level1-
  interactive.md (audio/MIDI sections), 05-level2-ports.md (port
  architecture intro + v3.1 extension note), 07-variables.md ($DATE$,
  $TIME$, $USER$, $RAND$, $BEEP$, sound tokens, $TEXTDATA$), 08-font-
  specification.md (Apple IIe convention reference), 10-appendices.md
  (version history table).
- `fonts/font_cp437_8x16.h`: scrubbed lone RP2350 reference in a
  flash-placement comment.
- `cmake/arm-none-eabi.cmake`: stripped the A2FUSION card / dual-
  RP2350 architecture / "Processor V vs B" / Facebook board-reference
  block.  Kept RP2350 / Cortex-M33 / Pico 2 names (legitimate target
  identification) and added a note about other Cortex-M33 boards.

### Inline label scrub — completed 2026-05-25 (third pass)

All "Codex FIX N", "FIX V1/M3/M4/L1-2/L1-4/L1-7/TX1..TX11/Q1/A7/B4"
labels have been scrubbed from `include/ripscrip.h` struct comments,
`src/ripscrip.c` function bodies, `src/rip_icons.c`, and
`tests/test_ripscrip.c`.  Comments rewritten to describe the
*behaviour* rather than the patch identifier; the label → meaning
index stays preserved in `integration-notes.md` for anyone needing
to cross-reference a prior commit.

### Dead-symbol drop — completed 2026-05-25 (second pass)

`term_cell_t`, `TERM_MAX_COLS`, `TERM_MAX_ROWS` removed from
`include/riplib_platform.h` per U-008.  Repo-wide grep confirmed
zero callsites inside RIPlib.  A2GSPU's own usage (if any) is an
A2GSPU concern and lives in A2GSPU's own headers.

### Cross-module forward declarations — added 2026-05-25 (third pass)

New file `src/rip_internal.h` holds small inline helpers and
forward declarations used at the seam between `src/ripscrip.c` and
the extracted subsystem modules:

- `rip_strnlen` (static inline)
- `rip_reset_windows_state` (forward decl; defined non-static in
  ripscrip.c so the variable engine can call it from $RESET$)

Future subsystem extractions (port system, file-upload staging,
clipboard) will likely add a few more entries here.

### Fourth-pass clipboard + filename-safety extraction (2026-05-25)

The clipboard subsystem (capture / store / cache-as-icon / save-to-
slot) and its sibling raster-blit helpers (point, tiled, icon-style-
aware draw, screen-region copy with scaling) — 9 functions total,
~290 LOC — moved into `src/rip_clipboard.{h,c}`.  `rip_filename_is_safe`
promoted from a `static` helper in `src/ripscrip.c` to a `static
inline` in `src/rip_internal.h` so the clipboard module can validate
icon names without dragging a ripscrip.c symbol over.

The port system (audit's listed C-002 step 4) turned out not to need
extraction — it's already cleanly in `src/ripscrip2.c`.  The
file-upload staging (audit's listed C-002 step 5) was deliberately
deferred: its `end_state` handler dispatches to a non-trivial RAF
archive parser that lives inside `ripscrip.c`, and the `rip_filename_is_safe`
helper it relies on is shared with five unrelated callsites.
Extracting just the upload state machine while leaving RAF behind
would create more inter-file plumbing than the cleanup would buy.
That decision is recorded as "evaluated, not worth extracting" in
`design/decisions.md` rather than as a remaining task.

### What remains

- Structural restructure of `riplib_platform.h` to remove the
  `comp_*` stub leaks from the public surface.  Blocked on C-004
  variant A (the breaking change), which is parked until a real
  multi-session consumer asks for it.  Until then, the stubs stay
  in the public header with the comment-only re-classification
  that's already been applied.
