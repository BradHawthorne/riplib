# RIPlib

**A platform-independent RIPscrip-compatible drawing library in pure C99.**

RIPlib provides a complete 2D rendering engine with 37+ drawing primitives, 10 BGI stroke fonts, and a full RIPscrip v1.54/v2.0/v3.0 protocol parser. It renders to any `uint8_t*` framebuffer with zero platform dependencies.

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
