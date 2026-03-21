# RIPlib — Standalone Drawing Library Snapshot

## What It Is

A pure C drawing library extracted from the A2GSPU card firmware.
Platform-independent, zero dependencies beyond `<math.h>` and
`<string.h>`. Renders to any `uint8_t*` framebuffer.

**Name**: RIPlib (RIPscrip-compatible drawing library)
**License**: MIT
**Language**: C99
**Dependencies**: None (pure C, optional FPU)

## What's Included

### Core Drawing Engine
- `drawing.c` / `drawing.h` — 37+ primitives
  - Line (Bresenham, dash patterns, thickness)
  - Rectangle (filled/outline)
  - Rounded rectangle (midpoint circle corners)
  - Circle, Ellipse (midpoint algorithm, filled/outline)
  - Arc, Pie, Elliptical arc/pie (FPU angle test)
  - Bezier cubic (FPU parametric, adaptive steps)
  - Polyline, Polygon (filled via scanline)
  - Flood fill (border-color, patterned)
  - Text (bitmap 8x8/8x16)
  - Pixel, HLine, VLine
  - Copy rect, Save/Restore region, Get pixel
  - 5 write modes (COPY, OR, AND, XOR, NOT)
  - 13 fill patterns (11 built-in + user + solid)
  - Clip region (set, save, restore)
  - Dirty-rect callback for efficient refresh

### BGI Stroke Font Engine
- `bgi_font.c` / `bgi_font.h` — CHR file parser + renderer
  - Borland CHR binary format parser ('+' scan, 16-byte header)
  - 10 fonts: Triplex, Small, Sans, Gothic, Script, Simplex,
    Triplex Script, Complex, European, Bold
  - Scale 1-10, horizontal + vertical (CW/CCW)
  - Font attributes: bold, italic, underline, shadow
  - String width measurement
  - Font data headers (font_bgi_*.h)

### RIPscrip Protocol Parser
- `ripscrip.c` / `ripscrip.h` — v1.54/v2.0/v3.0/v3.1
  - 90+ commands (Level 0 + Level 1 + Extended + Level 2)
  - 13-state FSM parser
  - EGA 350→400 coordinate scaling
  - Variable expansion (19 tokens)
  - Mouse region hit testing
  - ESC[! auto-detect
  - Session lifecycle management

### Level 2 Port System
- `ripscrip2.c` / `ripscrip2.h` — Drawing ports
  - 36 port slots with per-port state save/restore
  - Create/delete/switch/copy/flags
  - Gradient fill, scalable text, widgets

### Icon Pipeline
- `rip_icons.c` / `rip_icons.h` — BMP/ICN lookup + cache
- `rip_icn.c` / `rip_icn.h` — ICN (BGI putimage) parser

## Platform Interface (3 extern stubs)

```c
// Implement these for your platform:
void palette_write_rgb565(uint8_t index, uint16_t rgb565);
void *gpu_psram_alloc(uint32_t size);  // or just malloc()
void card_tx_push(const char *buf, int len);  // send to BBS
```

## What's NOT Included

- RP2350/Pico SDK dependencies (HSTX, DMA, PSRAM hardware)
- DispHSTX display driver
- FreeRTOS compositor/window manager
- Flash filesystem / Zmodem receiver
- Apple IIgs bus interface / slot protocol
- GS/OS System 7 trap installer
- QuickDraw II extensions (future, post-snapshot)

## Usage Example

```c
#include "drawing.h"
#include "bgi_font.h"
#include "ripscrip.h"

uint8_t framebuffer[640 * 400];

// Init
draw_init(framebuffer, 640, 640, 400);

// Draw directly
draw_set_color(0xFF);
draw_rounded_rect(10, 10, 200, 100, 8, true);
draw_circle(320, 200, 50, false);

// Or parse RIPscrip protocol
rip_state_t rip;
rip_init_first(&rip);
// Feed bytes from BBS:
for (int i = 0; i < data_len; i++)
    rip_process_byte(&rip, data[i]);
```

## v3.1 Extensions (unique to RIPlib)

Features not in any other BGI implementation:
- AND + NOT write modes
- Vertical text CW + CCW (direction 0/1/2)
- Top-to-bottom vertical text (corrected from Borland bottom-to-top)
- Font attributes (bold/italic/underline/shadow)
- 13 native fill patterns (most implementations have 8)
- FPU bezier (parametric, no integer rounding)
- FPU trig (sinf/cosf/atan2f for arcs/pies)
- Scanline pie fill (no flood fill leak bug)
- Patterned flood fill (two-pass)
- Drawing port system with alpha/compositing
- Full RIPscrip v1.54/v2.0/v3.0 protocol parser

## Proven Portability

Already compiles and runs on:
- RP2350 Cortex-M33 @ 384MHz (A2GSPU card firmware)
- x86/x64 Windows (GSSquared emulator, SDL2)
- Any platform with a C99 compiler + framebuffer
