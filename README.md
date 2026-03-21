# RIPlib

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Language: C99](https://img.shields.io/badge/Language-C99-blue.svg)
![Build: CMake](https://img.shields.io/badge/Build-CMake-red.svg)
![Platform: Any](https://img.shields.io/badge/Platform-Independent-orange.svg)
![Protocol: RIPscrip v3.1](https://img.shields.io/badge/RIPscrip-v3.1-purple.svg)

**A platform-independent RIPscrip-compatible drawing library in pure C99.**

[![RIPlib Diagnostic Harness on RP2350 DVI Hardware](https://img.youtube.com/vi/IAHWylT1gd8/0.jpg)](https://youtu.be/IAHWylT1gd8)

*31-page diagnostic harness running on RP2350 HSTX → DVI at 720×480 60fps. Click to watch.*

RIPlib provides a complete 2D rendering engine with 37+ drawing primitives, 10 BGI stroke fonts, and a full RIPscrip protocol parser supporting **all protocol versions**: v1.54 (Level 0/1), v2.0 (Extended + Level 2 Drawing Ports), v3.0, and v3.1 (A2GSPU extensions). It renders to any `uint8_t*` framebuffer with zero platform dependencies.

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
| Protocol Parser | No | No | No | **Full RIPscrip v1.54-v3.1** |
| Platform | DOS only | SDL2/SDL3 | Windows | **Any C99 + framebuffer** |
| Dependencies | DOS/BIOS | SDL2 | Win32 | **None** |

### Protocol Version Support

| Version | Year | Coverage | Notes |
|---------|------|----------|-------|
| **v1.54** | 1993 | 100% | Full Level 0 (35 drawing commands) + Level 1 (17 interactive commands) |
| **v2.0** | 1994 | 100% | Extended commands (rounded rect, scroll, poly bezier, bounded text, etc.) + Level 2 Drawing Port system (36 ports with state save/restore) |
| **v3.0** | 1995 | 100% | Font justification, extended text windows, gradient fill, scalable text, menu/dialog/scrollbar widgets |
| **v3.1** | 2026 | 100% | A2GSPU extensions: AND/NOT write modes, vertical text CW+CCW, font attributes (bold/italic/underline/shadow), corrected vertical text direction, 13 native fill patterns, FPU curves |

## Features

### Drawing Primitives
- Line (Bresenham, dash patterns, variable thickness)
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
- 90+ commands across Level 0, Level 1, Extended, and Level 2
- Full v1.54 drawing command set
- Level 2 Drawing Port system (36 ports with state save/restore)
- Variable expansion ($RAND$, $DATE$, $RIPVER$, etc.)
- Mouse region hit testing with button support
- ESC[! auto-detection and version response
- Icon lookup with BMP/ICN format support

### v3.1 Extensions (unique to RIPlib)
- AND and NOT write modes (beyond standard COPY/XOR/OR)
- Vertical text CW + CCW (direction 0/1/2)
- Corrected vertical text direction (top-to-bottom, readable)
- Font attributes (bold/italic/underline/shadow)
- 13 native fill patterns (most implementations have 8)
- FPU Bezier curves (no integer rounding artifacts)
- FPU trigonometry (sinf/cosf/atan2f for arcs and pies)
- Scanline pie fill (eliminates flood-fill leak bugs)
- Patterned flood fill (two-pass algorithm)

## Quick Start

```c
#include "drawing.h"
#include "bgi_font.h"

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
make
```

With examples:
```bash
cmake -DRIPLIB_BUILD_EXAMPLES=ON ..
make
./riplib_demo > output.pgm
```

## Platform Interface

RIPlib requires 3 extern functions implemented by your platform:

```c
// Set a palette color (indexed color → RGB565)
void palette_write_rgb565(uint8_t index, uint16_t rgb565);

// Read a palette color
uint16_t palette_read_rgb565(uint8_t index);

// Allocate memory for caching (malloc on desktop, PSRAM on embedded)
void *gpu_psram_alloc(uint32_t size);

// Send bytes to BBS (TCP send, serial write, etc.)
void card_tx_push(const char *buf, int len);
```

See `examples/platform_stubs.c` for a minimal desktop implementation.

## RIPscrip Protocol Usage

```c
#include "ripscrip.h"

rip_state_t rip;
rip_init_first(&rip);

// Feed bytes from a BBS connection:
while (connected) {
    uint8_t byte = read_from_bbs();
    rip_process_byte(&rip, byte);
    // Drawing commands automatically render to the framebuffer
}
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
│   ├── ripscrip.c        Protocol parser (3100+ lines)
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
- **RP2350 Cortex-M33** @ 384MHz (A2GSPU card firmware)
- **x86/x64 Windows** (GSSquared Apple IIgs emulator)
- **Any C99 platform** with a framebuffer

The library uses single-precision FPU (`sinf`, `cosf`, `atan2f`, `sqrtf`) for accurate curve and angle calculations. On platforms without hardware FPU, the compiler provides software implementations — no code changes needed.

## Origins

RIPlib is extracted from the [A2GSPU](https://github.com/BradHawthorne) card firmware — a GPU coprocessor for the Apple IIgs that provides DVI output, RIPscrip terminal rendering, and 10 BGI stroke fonts on an RP2350 microcontroller. The drawing engine was designed for embedded use but is fully platform-independent.

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 SimVU (Brad Hawthorne)
