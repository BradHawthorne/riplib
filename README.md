# RIPlib

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Language: C99](https://img.shields.io/badge/Language-C99-blue.svg)
![Build: CMake](https://img.shields.io/badge/Build-CMake-red.svg)
![Platform: Any](https://img.shields.io/badge/Platform-Independent-orange.svg)
![Protocol: RIPscrip v3.2](https://img.shields.io/badge/RIPscrip-v3.2-purple.svg)
[![Build & Test](https://github.com/BradHawthorne/riplib/actions/workflows/build.yml/badge.svg)](https://github.com/BradHawthorne/riplib/actions/workflows/build.yml)

**A platform-independent RIPscrip-compatible drawing library in pure C99.**

[![RIPlib Diagnostic Harness on RP2350 DVI Hardware](https://img.youtube.com/vi/IAHWylT1gd8/0.jpg)](https://youtu.be/IAHWylT1gd8)

*31-page diagnostic harness running on RP2350 HSTX → DVI at 720×480 60fps. Click to watch.*

RIPlib provides a complete 2D rendering engine with 37+ drawing primitives, 10 BGI stroke fonts, and a broad RIPscrip protocol parser for v1.54 (Level 0/1), v2.0 (Extended + Level 2 Drawing Ports), v3.0, v3.1 (§A2G.1-7 extensions), and v3.2 (§A2G.8-13 quality-of-life refinements). Storage-oriented client features are mapped to an in-memory icon/clipboard cache and host request queue on embedded targets, and hardware/host-only protocol features use documented embedded fallbacks. It renders to any `uint8_t*` framebuffer with zero platform dependencies.

RIPlib is a portable rendering/parser core, not a complete terminal application. A host app still owns transport, real filesystem transfer, sound playback, external URL/program launch, and OS clipboard integration.

### Comparison

| Feature | Original BGI | SDL_bgi | WinBGIm | **RIPlib** |
|---------|-------------|---------|---------|-----------|
| Line/Rect/Circle/Ellipse | Yes | Yes | Yes | **Yes** |
| Rounded Rectangle | No | No | No | **Yes** |
| Bezier Curves | No | No | No | **Yes (FPU)** |
| Polygon Fill | Basic | Basic | Basic | **Scanline** |
| Flood Fill | Solid | Solid | Solid | **Patterned** |
| Fill Patterns | 8 | 8 | 8 | **11 + user** |
| Write Modes | 3 (COPY/XOR/OR) | 3 | 3 | **5 (all rendered)** |
| Stroke Fonts | 10 CHR (buggy parsers) | Partial | No | **10 CHR (correct parser)** |
| Font Scaling | 1-10 integer | 1-10 | N/A | **1-10 + attributes** |
| Font Attributes | No | No | No | **Bold/Italic/Underline/Shadow** |
| Vertical Text | Bottom-to-top only | Same | N/A | **Spec-correct bottom-to-top + 2 extensions** |
| Alpha/Transparency | No | Yes (SDL) | No | **Per-port (v3.1)** |
| Multiple Windows | No | Yes (SDL) | No | **36 Drawing Ports** |
| Mouse Regions | No | Yes (SDL) | Yes | **Yes + hit testing** |
| Protocol Parser | No | No | No | **Broad RIPscrip v1.54-v3.2** |
| Platform | DOS only | SDL2/SDL3 | Windows | **Any C99 + framebuffer** |
| Dependencies | DOS/BIOS | SDL2 | Win32 | **None** |
| Embedded target | No | No | No | **Pico 2 / RP2350 cross-compile verified in CI** |

### Protocol Version Support

| Version | Year | Status | Notes |
|---------|------|--------|-------|
| **v1.54** | 1993 | Portable core implemented | Level 0 drawing plus Level 1 interactive commands, icon cache lookup, clipboard capture/paste, file query, variables, and host callback fallbacks |
| **v2.0** | 1995 | Portable core implemented with embedded fallbacks | Extended drawing commands, header/mode metadata, filled-object border control, icon slots/style, scaled region copy, and Level 2 Drawing Ports with state save/restore |
| **v3.0** | 1997 | Portable core implemented with approximations | Font justification, extended text windows, gradient fill, scalable text state, menu/dialog/scrollbar widgets, palette query, and indexed-color alpha approximation |
| **v3.1** | 2026 | Implemented extensions (§A2G.1-7) | vertical text CW+CCW (directions 2 and 3, RIPlib extensions the driver rejects — see [§14.3.5](docs/spec/14-divergence-register.md)), font attributes (bold/italic/underline/shadow), 11 native fill patterns, FPU curves |
| **v3.2** | 2026 | Implemented extensions (§A2G.8-13) | State push/pop stack, layout/introspection variables, time component variables, EGA color-name aliases, `<<DEBUG>>` directive, radial gradient |

Host-mediated operations such as real filesystem transfer, Zmodem/RAF storage, OS clipboard integration, URL launch/delete callbacks, true direct-RGB framebuffers, and monitor overscan remain outside the portable core. The parser accepts those protocol surfaces where possible and exposes embedded-friendly fallbacks instead of claiming host behavior the library cannot provide by itself.

## Features

### Drawing Primitives
- Line (Bresenham, 16-bit dash patterns, variable thickness)
- Rectangle, Rounded Rectangle (outline + filled)
- Circle, Ellipse (midpoint algorithm, outline + filled)
- Arc, Pie, Elliptical Arc/Pie (FPU-accurate angle test)
- Cubic Bezier (FPU parametric evaluation, adaptive step count)
- Polyline, Polygon (outline + scanline fill)
- Flood Fill (border-color semantics, patterned fill)
- Text rendering (bitmap 8x8/8x16 + BGI stroke fonts)
- Copy/Save/Restore region, Get Pixel
- 5 write modes: COPY(0), XOR(1), OR(2), AND(3), NOT(4) — wire order per the shipping RIPSCRIP.DLL (MD5 `bade8b1f…`, self-reported version 3.00.04)
- 11 built-in fill patterns + user-defined slot (4 BGI styles are approximated)
- Clip region (set, save, restore)
- Dirty-rectangle callback for efficient screen refresh

### BGI Stroke Font Engine
- Borland CHR binary format parser
- 10 included fonts: Triplex, Small, Sans-Serif, Gothic, Script, Simplex, Triplex Script, Complex, European, Bold
- Scale 1-10, three text directions (horizontal, vertical CW, vertical CCW)
- Font attributes: bold, italic, underline, shadow
- String width measurement for layout

### Aligned to the shipping driver (v2.0.0)

RIPlib's command set is checked against the RIPscrip driver TeleGrafix
shipped, rather than against the published specifications alone. In v2.0.0
that moved **thirteen Level-0 commands** to different meanings — see
[CHANGELOG.md](CHANGELOG.md) for the list and
[`docs/spec/12-dll-provenance.md`](docs/spec/12-dll-provenance.md) for the
evidence behind each one.

- The driver's own dispatch table is transcribed verbatim in
  [`docs/spec/13-dll-command-table.md`](docs/spec/13-dll-command-table.md);
  the scripts under `scripts/` regenerate every binary-derived table and
  verify the image fingerprint before reporting.
- `scripts/ci-local.sh` runs the CI jobs against the local toolchain.
  It reports `PASS`, `FAIL`, or `SKIP` **with a reason**, and never
  counts a skip as a pass — the summary lists skips separately under
  "A SKIP IS NOT A PASS" and the exit status ignores them. Use
  `--list` to see which jobs this machine can run; the sanitizer job
  needs a toolchain that ships `libasan`/`libubsan`, which a Cygwin
  gcc does not.
- **Base-64 MegaNum** is implemented. RIPscrip has a second radix
  (`0-9 A-Z a-z # &`, case-sensitive) that four commands always use
  regardless of any global setting — see
  [§1.5.1](docs/spec/01-wire-format.md).
- All 36 `RIP_PolyMarker` glyph outlines are carried, and negotiated
  coordinate/colour widths (`|n`, `|M`) are honoured.
- Authentic `.RIP` scenes are replayed byte-for-byte as a regression net.
  Those scenes are third-party content and are **not** vendored, so this
  suite reports SKIP in CI and on a plain checkout; point
  `-DRIPLIB_CORPUS_DIR` at a RIPterm/RIPtel install to run it.

Known limitations are recorded rather than implied: character spacing
reaches the stroke fonts only, the coordinate-width conversion saturates at
1295 (sound for a fixed 640×400 device space, revisit for a world
transform), and two Level-3 handlers remain unidentified.

### RIPscrip Protocol Parser
- 100+ recognized command surfaces across Level 0, Level 1, Extended, and Level 2, with host-only operations bridged to callbacks/fallbacks
- Full v1.54 drawing command set
- Level 2 Drawing Port system (36 ports with state save/restore)
- Variable expansion (30+ built-in vars including $RAND$, $DATE$, $RIPVER$, $WOYM$, layout/time/color-name vars)
- Mouse region hit testing with button support
- ESC[! auto-detection and version response (advertises v3.2: `RIPSCRIP032001`)
- Icon lookup with BMP/ICN format support

### v3.1 Extensions (§A2G.1-7, unique to RIPlib)
- AND and NOT write modes (beyond standard COPY/XOR/OR)
- Vertical text: dir 1 bottom-to-top (BGI VERT_DIR, as the 1.54 specification states), dir 2 CCW, dir 3 CW top-to-bottom
- Font attributes (bold/italic/underline/shadow) on `|q`
- 11 native fill bitmaps + user slot (most implementations have 8; 4 BGI styles remain approximated — see §A2G.4)
- FPU Bezier curves (no integer rounding artifacts)
- FPU trigonometry (sinf/cosf/atan2f for arcs and pies)
- Scanline pie fill (eliminates flood-fill leak bugs)
- Patterned flood fill (two-pass algorithm)

### v3.2 Extensions (§A2G.8-13, RIPlib quality-of-life refinements)
- **State push/pop stack** — `|^` / `|~` save/restore the drawing prelude (colors, fill/line/write state including custom 16-bit line patterns, font and extended font state, cursor, viewport, filled-border mode). Bounded LIFO, 8 frames.
- **Layout / introspection variables** — `$CX$` `$CY$` `$VPW$` `$VPH$` `$VPCX$` `$VPCY$` `$CCOL$` `$CFCOL$` `$CBCOL$` for "center this text" without hardcoded 320,200.
- **Time component variables** — `$HOUR$` (12-hour) `$MHOUR$` (24-hour) `$MIN$` `$SEC$` `$WDAY$` (0=Sunday) `$DOW$` (day name) `$DAY$` `$MONTHNUM$` `$MONTH$` (month name) for greeting/banner variations. Names and semantics match the shipping RIPSCRIP.DLL.
- **EGA color-name aliases** — `$BLACK$` `$BLUE$` `$GREEN$` `$CYAN$` `$RED$` `$MAGENTA$` `$BROWN$` `$LIGHTGRAY$` `$DARKGRAY$` `$LIGHTBLUE$` `$LIGHTGREEN$` `$LIGHTCYAN$` `$LIGHTRED$` `$LIGHTMAGENTA$` `$YELLOW$` `$WHITE$`.
- **`<<DEBUG msg>>` preprocessor directive** — **off by default.** Enable with `-DRIPLIB_ENABLE_DEBUG_DIRECTIVE=ON` to push `>DEBUG: <msg>\r` to TX for development instrumentation. It is *not* safe to leave enabled in production: unsolicited terminal-to-host traffic has no precedent in the protocol, and a BBS sitting at a prompt reads those bytes as keystrokes. The directive is parsed and consumed either way, so rendering is identical.
- **Radial gradient** — `|28` gains mode 2 for FPU per-pixel radial fill alongside the existing horizontal (0) and vertical (1) modes.

`$RIPVER$` and the `ESC[!` probe both report **`RIPSCRIP032001`**.

## Quick Start

```c
#include "drawing.h"
#include "bgi_font.h"
#include "font_bgi_trip.h"

uint8_t framebuffer[640 * 400];

int main(void) {
    draw_init(framebuffer, 640, 640, 400);

    // Draw shapes
    draw_set_color(0xFF);
    draw_rounded_rect(10, 10, 200, 100, 8, true);
    draw_circle(320, 200, 50, false);

    // BGI stroke font
    bgi_font_t font;
    bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);
    bgi_font_draw_string(&font, 30, 60, "Hello RIPlib", 12, 2, 0xFF, 0);

    // framebuffer now contains the rendered image
    return 0;
}
```

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

With examples:
```bash
cmake -DRIPLIB_BUILD_EXAMPLES=ON ..
cmake --build .
./riplib_demo > output.pgm
```

## Platform Interface

RIPlib requires 3 extern functions implemented by your platform:

```c
// Set a palette color (indexed color → RGB565)
void palette_write_rgb565(uint8_t index, uint16_t rgb565);

// Read a palette color
uint16_t palette_read_rgb565(uint8_t index);

// Send bytes to BBS (TCP send, serial write, etc.)
void riplib_host_tx(const char *buf, int len);
```

See `examples/platform_stubs.c` for a minimal desktop implementation.
The PSRAM arena allocator is provided as `static inline` in
`riplib_platform.h` and uses `malloc()` on desktop platforms.

## RIPscrip Protocol Usage

```c
#include "ripscrip.h"

rip_state_t rip = {0};        // caller MUST zero-init before first use
comp_context_t ctx = {0};
rip_init_first(&rip);

// Feed bytes from a BBS connection:
while (connected) {
    uint8_t byte = read_from_bbs();
    rip_process(&rip, &ctx, byte);
    // Drawing commands automatically render to the framebuffer
}

// On disconnect (preserves PSRAM arena, clears session state):
rip_session_reset(&rip);
```

## Session Safety

RIPlib is **single-session by design**. Every public entrypoint comes
in two flavours: explicit-state `*_state(rip_state_t *s, ...)` variants
that are safe across distinct sessions, and globals-based shortcuts
(`rip_mouse_event_ext`, `rip_file_upload_*`, `rip_sync_*_byte`,
`rip_query_response_byte`, `rip_apply_palette`) that operate on the
single global session set by the most recent `rip_init_first()` call.

Embedders running more than one concurrent session **must** use only
the `*_state()` variants, or serialise all calls behind a single mutex.
Calling `rip_init_first(&sessionB)` silently flips the global pointer
away from `sessionA`, so any subsequent globals-based call would
operate on the wrong session.

See the `SESSION SAFETY` block at the bottom of `include/ripscrip.h`
for the full per-function classification.

## File Structure

```
riplib/
├── include/          Public header files
│   ├── drawing.h         Drawing primitives API
│   ├── bgi_font.h        BGI stroke font API
│   ├── ripscrip.h        RIPscrip protocol parser
│   ├── ripscrip2.h       Level 2 port system
│   ├── rip_icons.h       Icon lookup + cache
│   ├── rip_icn.h         ICN format parser
│   ├── riplib_platform.h Platform abstraction
│   └── riplib_version.h  Version macros + accessor
├── src/              Implementation
│   ├── drawing.c         Drawing primitives
│   ├── bgi_font.c        CHR font parser + renderer
│   ├── ripscrip.c        Protocol parser FSM + handlers
│   ├── ripscrip2.c       Level 2 port system + widgets
│   ├── rip_preproc.c     <<IF>>/<<ELSE>>/<<ENDIF>> preprocessor
│   ├── rip_variables.c   $VAR$ expansion + IF-expr evaluator
│   ├── rip_clipboard.c   Clipboard capture/blit/scale
│   ├── rip_icons.c       Icon pipeline
│   ├── rip_icn.c         ICN format decoder
│   ├── riplib_version.c  Runtime version accessor
│   └── (rip_meganum.h, rip_internal.h — header-only helpers)
├── fonts/            Font data (flash-embedded)
│   ├── font_bgi_*.h      10 BGI stroke fonts (~76KB)
│   └── font_cp437_*.h    CP437 bitmap fonts (8×8 + 8×16)
├── icons/            Icon data (optional)
│   ├── rip_icons_data.*   95 BMP icons (~1.6MB)
│   └── rip_icns_data.*    3 ICN icons (~90KB)
├── tests/            Test suites
│   ├── test_drawing.c    Rendering primitives
│   ├── test_ripscrip.c   Parser FSM, commands, variables
│   ├── test_compat.c     Fixture replay with frame-hash lockdown
│   ├── test_corpus.c     Authentic .RIP scene replay (opt-in)
│   ├── test_fuzz_seeded.c Seeded mutation fuzzer
│   └── fuzz_parser.c     libFuzzer target (optional, needs clang)
├── scripts/          Tooling
│   ├── ci-local.sh       Run the CI jobs locally (PASS/FAIL/SKIP-with-reason)
│   ├── dll-*.py          Regenerate binary-derived tables from the driver
│   ├── ref-compare.py    RIPlib and a third-party reference vs the driver
│   ├── corpus-scan.py    Opcode census over a corpus
│   ├── check-branding.sh Platform-independence lint (CI)
│   ├── check-command-docs.py Parser/spec agreement lint (CI)
│   ├── check-spec-examples.py Spec command blocks vs the record (CI)
│   └── check-dll-table.py  Segment 13 vs the dispatch record
├── examples/         Demo programs
└── docs/             Documentation
```

## Portability

RIPlib is proven on:
- **Raspberry Pi RP2350 / Pico 2** @ 384MHz (RP235XA, 30 GPIO)
- **x86/x64 Windows, Linux, macOS** (CI matrix, Debug + Release)
- **Any C99 platform** with a framebuffer

The library uses single-precision FPU (`sinf`, `cosf`, `atan2f`, `sqrtf`) for accurate curve and angle calculations. On platforms without hardware FPU, the compiler provides software implementations — no code changes needed.

### Reference target: RP2350

<a id="reference-target-a2gspu-firmware"></a>

RIPlib's first deployment was on an RP2350-class microcontroller, and
that remains the reference target for embedded builds: Cortex-M33 with
a single-precision FPU, rendering to an 8-bit indexed framebuffer with
video scanned out over HSTX or PIO-DVI.  Nothing in the library depends
on that target — it is simply the configuration the embedded numbers in
this README were measured on.

To build RIPlib for the Pico 2 / RP2350 target:

```bash
cmake -B build-pico2 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build-pico2
# produces build-pico2/libriplib.a
```

The toolchain file targets Cortex-M33 + fpv5-sp-d16 FPU + Thumb with `-DPICO_RP2350=1` so downstream firmware that uses the pico-sdk's chip-detection macros sees a consistent build flag.  Both RP2350 packages — RP235XA (30 GPIO, QFN-60) and RP235XB (48 GPIO, QFN-80) — share the same CPU core, so one toolchain file builds for either; pin-count differences are a board-level concern.

## Testing

```bash
cmake -B build -DRIPLIB_BUILD_TESTS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite ships 330 individual checks plus two behavioural suites:
- `test_drawing` — 41 rendering primitives, fonts, and edge-case checks.
- `test_ripscrip` — 283 FSM transitions, dispatched commands, mouse
  hit-testing, variable expansion, host callbacks, port system.
- `test_compat` — 6 fixture replays with FNV-1a frame-hash lockdown so
  pixel-level regressions show up immediately.
- `test_corpus` — replays authentic TeleGrafix `.RIP` scenes byte-for-byte,
  asserting no crash, no wedged FSM and no drawing outside the framebuffer,
  and reporting painted pixels, distinct colours and pending asset requests.
  Reports SKIP unless `-DRIPLIB_CORPUS_DIR` points at an installation.
- `test_fuzz_seeded` — fixed-seed mutation fuzzer over the command layer,
  including long payloads with `\` continuations, against a guard-banded
  framebuffer. Takes an iteration count; ctest runs 20,000.

CI runs the matrix on Linux, macOS, and Windows in both Debug and
Release, plus dedicated UBSan/ASan, coverage-floor, embedded ARM archive,
`-fanalyzer`, and two lints — platform-independence and parser/spec
agreement — for 12 jobs in total.

A local sanitizer build is **not** equivalent to the Linux job, which is
worth knowing before trusting one. UBSan's `nonnull-attribute` check —
passing NULL to a parameter declared never-null, such as `memcpy`'s
source at zero length — fires only because *glibc* annotates those
functions that way. Windows CRT headers carry no such annotation, so the
check cannot trigger there under any compiler, clang included. A real
defect of exactly that shape passed every local suite and was caught only
by the Linux job. ASan is no help either: a zero-length copy touches no
memory. Reproduce that class on Linux, or trust CI for it.

## Origins

RIPlib is extracted from the [A2GSPU](https://github.com/BradHawthorne) firmware — the rendering software for an RP2350-based Apple IIgs GPU coprocessor that provides DVI output, RIPscrip terminal rendering, and 10 BGI stroke fonts.  RIPlib is a parallel project: the drawing engine was designed for that embedded use, but is fully platform-independent.

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 SimVU (Brad Hawthorne)
