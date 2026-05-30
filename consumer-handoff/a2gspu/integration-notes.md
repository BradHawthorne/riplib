# A2GSPU Integration Notes

Implementation rationale for design choices that look generic in RIPlib
but were originally chosen to fit A2GSPU's embedded constraints. Moved
out of RIPlib per audit candidate **C-001**; preserved here for A2GSPU's
maintainers to absorb into their integration documentation.

## PSRAM arena sizing

`src/ripscrip.c` defines `RIP_PSRAM_ARENA_SIZE = 1 MB`. This is the
default per-session backing store for icon pixels, the clipboard,
file-upload staging buffers, and dynamic icon caching. The 1 MB figure
comes from A2GSPU's RP2350 + 8 MB external PSRAM topology: 1 MB is the
"comfortable" allocation that leaves the other 7 MB for picovu's text
canvas, framebuffer ping-pong, etc.

For consumers with different memory budgets, the value should be made
configurable (audit candidate C-005 / future work). On platforms with
more memory (e.g. desktop), larger values would let bigger clipboard
captures and more cached icons. On platforms with less, the BMP cache
fills up faster and `psram_arena_alloc` starts returning NULL — the
parser handles this gracefully but features degrade silently.

## 640×400 framebuffer default

The library's parser internally uses `scale_y()` to convert the spec's
EGA 350-row coordinate space to the 400-row coordinate space A2GSPU
runs (8/7 multiplier). This is a port-level decision: A2GSPU targets
640×400 because that's the native HSTX/DVI mode the RP2350 outputs
through picovu.

Other consumers that want true EGA 640×350 should bypass `scale_y` or
configure RIPlib to skip it. This isn't currently a public knob.

## ProDOS time-sync byte protocol

`rip_sync_date_byte()` and `rip_sync_time_byte()` accumulate bytes from
the IIgs's ProDOS MLI `GET_TIME` result, one byte per
`CMD_SYNC_DATE` / `CMD_SYNC_TIME` slot-bus write, with the NUL
terminator triggering the commit.

The wire format the IIgs companion app sends:
- `CMD_SYNC_DATE`: 8 bytes "MM/DD/YY" + 0x00
- `CMD_SYNC_TIME`: 8 bytes "HH:MM:SS" + 0x00

The RP2350 has its own RTC but it's not authoritative for BBS
session time because the BBS spec assumes whatever the user's host
clock says. A2GSPU resolves this by snapshotting the IIgs's ProDOS
clock at connect time and using that for `$DATE$` / `$TIME$`
expansion. The fallback is the RP2350 RTC when host hasn't synced.

For non-Apple-II consumers, the relevant pattern is: implement
whatever your host's time-of-day callback is and feed the same
accumulator bytes via `rip_sync_date_byte_state()` /
`rip_sync_time_byte_state()`. The format ("MM/DD/YY" + NUL) is the
only contract.

## Compositor stub interface

`include/riplib_platform.h` declares four `comp_*` no-op stubs:
`comp_passthrough_vt100`, `comp_set_cursor`, `comp_clear_screen`,
`comp_clear_line`. These exist because `src/ripscrip.c` calls into
them when handling protocol bytes that should also affect the host's
ANSI/VT100 layer (typically during ESC[! probe handling or during
fallback from RIPscrip back to text mode).

In A2GSPU, these route into A2GSPU's compositor — the thing that
multiplexes RIPscrip output, Apple II native video modes, and a CP437
terminal grid onto the single DVI framebuffer. The compositor's actual
implementation is in A2GSPU, not RIPlib.

For other consumers: if you don't have a compositor, the no-op stubs
in `riplib_platform.h` are sufficient. The library's RIPscrip
rendering will still work; you just won't get VT100 passthrough
between RIPscrip frames.

The "right" long-term fix per audit candidate C-002 / C-004 is to
make this dependency directional via a callback registration API
instead of weak-linker-style externs. For now, the stubs are
adequate.

## Terminal cell type

`include/riplib_platform.h` declares `term_cell_t`,
`TERM_MAX_COLS = 80`, `TERM_MAX_ROWS = 25`. **No code inside RIPlib
references these.** They exist as a forward declaration for A2GSPU's
compositor, which has its own CP437 grid renderer alongside the
RIPscrip pixel renderer. The shared header was the convenient place
to put the type definition so both libraries agreed on its shape.

For RIPlib's purposes, this type is dead weight. A2GSPU should
move the declaration into its own headers (probably alongside
`platform_a2gspu.c`); RIPlib should then drop it from the platform
header. This is tracked as audit follow-up under C-001.

## Internal patch labels

Various RIPlib source comments reference "Codex FIX N", "FIX V1",
"FIX M3/M4", "FIX L1-2/L1-4/L1-7", "FIX TX1/TX2", "FIX Q1/A7",
"FIX S5/FB-4", and similar labels. These were the internal bug-list
identifiers from A2GSPU's pre-extraction audit work — labels useful
to A2GSPU maintainers who want to cross-reference against the audit
notes, meaningless to anyone else.

The audit recommends scrubbing these labels from RIPlib's source
comments at the next maintenance pass, leaving plain English
explanations behind. The label → context mapping is preserved here
for A2GSPU's reference:

| Label             | What it fixed                                              |
|-------------------|------------------------------------------------------------|
| Codex FIX 1       | rip_init split into _first / _activate / _session_reset    |
| Codex FIX 3       | session reset clears arena + queue, preserves arena block  |
| Codex FIX 4       | rip_icon_clear_requests() called from session reset        |
| Codex FIX 5       | activate preserves query_pending across protocol switch    |
| FIX V1            | host-supplied date/time via CMD_SYNC_DATE/SYNC_TIME        |
| FIX M3            | per-byte sync_date/sync_time accumulator                   |
| FIX M4            | $QUERY$ round-trip via CMD_QUERY_PROMPT/RESPONSE           |
| FIX L1-2          | 1S RIP_IMAGE_STYLE — stored for subsequent icon renders    |
| FIX L1-4          | 1N RIP_SET_ICON_DIR — icon path override stored            |
| FIX L1-7          | 1V extended viewport scale factor                          |
| FIX TX1           | $RAND$ LCG seeded from frame_count                         |
| FIX TX2           | $NOREFRESH$ / $REFRESH$ suppress flag                      |
| FIX Q1 / FIX A7   | last_char tracks previous byte for '!' line-boundary check |
| FIX S5 / FIX FB-4 | num_mouse_regions widened from uint8_t to uint16_t         |
| FIX L11 / L12     | NULL-safety on public entrypoints (added Apr 2026)         |
| FIX L13           | BMP parser explicit truncation + zero-height guards        |
| FIX L14 / L16 / L17 | NULL-font draw_text guard in text rendering paths        |
| FIX L18           | 1D handler no longer double-writes app_vars[0]             |

## Source-of-truth pointers

- The full deviations list (where RIPlib intentionally differs from the
  DLL) lives in `docs/spec/11-dll-deviations.md` *inside RIPlib*. It's
  the normative spec for divergences and belongs with the spec, not
  here.
- The wire-format spec lives in `docs/spec/01-wire-format.md` through
  `06a-v32-extensions.md` *inside RIPlib*.
- A2GSPU's own integration glue (the `platform_a2gspu.c` extern
  implementations, the A2GSPU-side CMake build glue, the A2GSPU
  compositor) is in the A2GSPU repo.
