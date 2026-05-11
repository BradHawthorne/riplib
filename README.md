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

RIPlib provides a complete 2D rendering engine with 37+ drawing primitives, 10 BGI stroke fonts, and a broad RIPscrip protocol parser for v1.54 (Level 0/1), v2.0 (Extended + Level 2 Drawing Ports), v3.0, v3.1 (A2GSPU §A2G.1-7 extensions), and v3.2 (§A2G.8-13 quality-of-life refinements). Storage-oriented client features are mapped to an in-memory icon/clipboard cache and host request queue on embedded targets, and hardware/host-only protocol features use documented embedded fallbacks. It renders to any `uint8_t*` framebuffer with zero platform dependencies.

RIPlib is a portable rendering/parser core, not a complete terminal application. A host app still owns transport, real filesystem transfer, sound playback, external URL/program launch, and OS clipboard integration.

### Comparison

| Feature | Original BGI | SDL_bgi | WinBGIm | **RIPlib** |
|---------|-------------|---------|---------|-----------|
| Line/Rect/Circle/Ellipse | Yes | Yes | Yes | **Yes** |
| Rounded Rectangle | No | No | No | **Yes** |
| Bezier Curves | No | No | No | **Yes (FPU)** |
| Polygon Fill | Basic | Basic | Basic | **Scanline** |
| Flood Fill | Solid | Solid | Solid | **Patterned** |
| Fill Patterns | 8 | 8 | 8 | **13** |
| Write Modes | 3 (COPY/XOR/OR) | 3 | 3 | **5 (+AND/NOT)** |
| Stroke Fonts | 10 CHR (buggy parsers) | Partial | No | **10 CHR (correct parser)** |
| Font Scaling | 1-10 integer | 1-10 | N/A | **1-10 + attributes** |
| Font Attributes | No | No | No | **Bold/Italic/Underline/Shadow** |
| Vertical Text | Bottom-to-top (backwards) | Same | N/A | **Top-to-bottom (corrected)** |
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
| **v2.0** | 1994 | Portable core implemented with embedded fallbacks | Extended drawing commands, header/mode metadata, filled-object border control, icon slots/style, scaled region copy, and Level 2 Drawing Ports with state save/restore |
| **v3.0** | 1995 | Portable core implemented with approximations | Font justification, extended text windows, gradient fill, scalable text state, menu/dialog/scrollbar widgets, palette query, and indexed-color alpha approximation |
| **v3.1** | 2026 | Implemented extensions (§A2G.1-7) | AND/NOT write modes, vertical text CW+CCW, font attributes (bold/italic/underline/shadow), corrected vertical text direction, 13 native fill patterns, FPU curves |
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
- 5 write modes: COPY, OR, AND, XOR, NOT
- 13 fill patterns (11 built-in + user-defined + solid)
- Clip region (set, save, restore)
- Dirty-rectangle callback for efficient screen refresh

### BGI Stroke Font Engine
- Borland CHR binary format parser
- 10 included fonts: Triplex, Small, Sans-Serif, Gothic, Script, Simplex, Triplex Script, Complex, European, Bold
- Scale 1-10, three text directions (horizontal, vertical CW, vertical CCW)
- Font attributes: bold, italic, underline, shadow
- String width measurement for layout

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
- Vertical text CW + CCW (direction 0/1/2)
- Corrected vertical text direction (top-to-bottom, readable)
- Font attributes (bold/italic/underline/shadow)
- 13 native fill patterns (most implementations have 8)
- FPU Bezier curves (no integer rounding artifacts)
- FPU trigonometry (sinf/cosf/atan2f for arcs and pies)
- Scanline pie fill (eliminates flood-fill leak bugs)
- Patterned flood fill (two-pass algorithm)

### v3.2 Extensions (§A2G.8-13, RIPlib quality-of-life refinements)
- **State push/pop stack** — `|^` / `|~` save/restore the drawing prelude (colors, fill/line/write state including custom 16-bit line patterns, font and extended font state, cursor, viewport, filled-border mode). Bounded LIFO, 8 frames.
- **Layout / introspection variables** — `$CX$` `$CY$` `$VPW$` `$VPH$` `$VPCX$` `$VPCY$` `$CCOL$` `$CFCOL$` `$CBCOL$` for "center this text" without hardcoded 320,200.
- **Time component variables** — `$HOUR$` `$MIN$` `$SEC$` `$DOW$` `$DOM$` `$MONTH$` for greeting/banner variations.
- **EGA color-name aliases** — `$BLACK$` `$BLUE$` `$GREEN$` `$CYAN$` `$RED$` `$MAGENTA$` `$BROWN$` `$LIGHTGRAY$` `$DARKGRAY$` `$LIGHTBLUE$` `$LIGHTGREEN$` `$LIGHTCYAN$` `$LIGHTRED$` `$LIGHTMAGENTA$` `$YELLOW$` `$WHITE$`.
- **`<<DEBUG msg>>` preprocessor directive** — pushes `>DEBUG: <msg>\r` to TX for development instrumentation; safe to leave in production.
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
void card_tx_push(const char *buf, int len);
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

## File Structure

```
riplib/
├── include/          Header files
│   ├── drawing.h         Drawing primitives API
│   ├── bgi_font.h        BGI stroke font API
│   ├── ripscrip.h        RIPscrip protocol parser
│   ├── ripscrip2.h       Level 2 port system
│   ├── rip_icons.h       Icon lookup + cache
│   ├── rip_icn.h         ICN format parser
│   └── riplib_platform.h Platform abstraction
├── src/              Implementation
│   ├── drawing.c         37+ drawing primitives
│   ├── bgi_font.c        CHR font parser + renderer
│   ├── ripscrip.c        Protocol parser (4900+ lines)
│   ├── ripscrip2.c       Level 2 extensions
│   ├── rip_icons.c       Icon pipeline
│   └── rip_icn.c         ICN format decoder
├── fonts/            Font data (flash-embedded)
│   ├── font_bgi_*.h      10 BGI stroke fonts (~76KB)
│   └── font_cp437_*.h    CP437 bitmap fonts
├── icons/            Icon data (optional)
│   ├── rip_icons_data.*   95 BMP icons (~1.6MB)
│   └── rip_icns_data.*    3 ICN icons (~90KB)
├── examples/         Demo programs
└── docs/             Documentation
```

## Portability

RIPlib is proven on:
- **Raspberry Pi RP2350 / Pico 2** @ 384MHz (A2GSPU card firmware, Processor V)
- **x86/x64 Windows** (GSSquared Apple IIgs emulator)
- **Any C99 platform** with a framebuffer

The library uses single-precision FPU (`sinf`, `cosf`, `atan2f`, `sqrtf`) for accurate curve and angle calculations. On platforms without hardware FPU, the compiler provides software implementations — no code changes needed.

### Reference target: A2GSPU two-RP2350 architecture

RIPlib was extracted from the A2GSPU card, an Apple IIgs GPU
coprocessor with two RP2350 (Pico 2) MCUs:

```
       ┌──────────────┐                           ┌──────────────┐
       │  USB (host)  │◄──┐                  ┌───►│ DVI / HDMI   │
       └──────────────┘   │                  │    └──────────────┘
                          │                  │ PIO / HSTX
       ┌──────────────┐   │   ┌────────┐     │
       │ SDIO microSD │◄──┼───│  "B"   │ IPL │    ┌────────┐
       └──────────────┘   │   │RP235XB │◄────┼───►│  "V"   │◄── USB
                          │   │48 GPIO │ PIO │    │RP235XA │◄── UX
       ┌──────────────┐   │   │        │     │    │30 GPIO │
       │ Apple II Bus │◄──┘   └────────┘     │    └────────┘
       └──────────────┘             ▲        │         ▲
                                    │        ▼         │
                                ┌───┴────────────────────┐
                                │   Expansion Connector  │
                                └────────────────────────┘
```

- **Processor "B" (RP235XB, 48 GPIO)** — host bus, SDIO/microSD storage, USB-A device side, Apple II bus glue.
- **Processor "V" (RP235XA, 30 GPIO)** — video out (HSTX or PIO-DVI), USB keyboard, UX inputs.  **This is where RIPlib runs.**

The two processors communicate via an IPL (Inter-Processor Link) over PIO so that B can stream a remote BBS connection to V, and V renders the RIPscrip protocol to the framebuffer.

Board reference (block diagram source): <https://www.facebook.com/groups/5251478676/posts/10166402670968677/>.

To build RIPlib for the Pico 2 / RP2350 target:

```bash
cmake -B build-pico2 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build-pico2
# produces build-pico2/libriplib.a
```

The toolchain file targets Cortex-M33 + fpv5-sp-d16 FPU + Thumb with `-DPICO_RP2350=1` so downstream firmware that uses the pico-sdk's chip-detection macros sees a consistent build flag.  Both RP2350 variants (V/RP235XA and B/RP235XB) share the CPU core, so the same toolchain file builds for either processor — pin-count differences are a board-level concern.

## Testing

```bash
cmake -B build -DRIPLIB_BUILD_TESTS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite currently ships 275 individual checks across three binaries:
- `test_drawing` — 41 rendering primitives, fonts, and edge-case checks.
- `test_ripscrip` — 228 FSM transitions, dispatched commands, mouse
  hit-testing, variable expansion, host callbacks, port system.
- `test_compat` — 6 fixture replays with FNV-1a frame-hash lockdown so
  pixel-level regressions show up immediately.

CI runs the matrix on Linux, macOS, and Windows in both Debug and
Release, plus dedicated UBSan/ASan, coverage-floor, embedded ARM archive,
and `-fanalyzer` jobs.

## Origins

RIPlib is extracted from the [A2GSPU](https://github.com/BradHawthorne) card firmware — a GPU coprocessor for the Apple IIgs that provides DVI output, RIPscrip terminal rendering, and 10 BGI stroke fonts on an RP2350 microcontroller. The drawing engine was designed for embedded use but is fully platform-independent.

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 SimVU (Brad Hawthorne)
