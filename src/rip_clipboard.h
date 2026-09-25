/*
 * rip_clipboard.h — RIPlib clipboard + raster blit operations.
 *
 * Despite the name, this module owns more than just the RIPscrip
 * GET_IMAGE / PUT_IMAGE clipboard.  It groups the related raster ops
 * that share write-mode, scaling, and tiling logic:
 *
 *   - The clipboard itself (capture/store/cache-as-icon/save-to-slot)
 *   - Pixel blit (point, scaled, tiled)
 *   - Icon-style-aware draw (honours stretch / tile / center /
 *     proportional fit per the active 1S / & state)
 *   - Screen-to-screen copy with optional scaling
 *
 * Extracted from src/ripscrip.c as step 6 of audit candidate C-002
 * (decompose ripscrip.c monolith).  Functions take rip_state_t* and
 * operate on existing fields so the rip_state_t layout stays unchanged.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License.  See LICENSE.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ripscrip.h"
#include "rip_icons.h"

/* ── Clipboard ──────────────────────────────────────────────────── */
typedef struct { int16_t x, y, width, height; } rip_image_rect_t;

/* Native memory-DIB sampling; position < destination, positive dimensions. */
uint16_t rip_gdi_sample(uint16_t position, uint16_t source, uint16_t destination,
                        bool copy_samples);

/* Capture the rendered icon using the driver's expanded clipboard extent. */
bool rip_clipboard_capture_icon(rip_state_t *s, const rip_image_rect_t *rect);

/* Allocate the clipboard backing buffer if not already done.  Returns
 * true if the buffer is ready to use (newly allocated OR already
 * present), false on arena exhaustion. */
bool rip_clipboard_alloc(rip_state_t *s);

/* Copy `width`x`height` pixels into the clipboard (treating clipboard
 * as a generic 8bpp source).  Allocates the clipboard if needed. */
bool rip_clipboard_store_pixels(rip_state_t *s,
                                const uint8_t *pixels,
                                uint16_t width,
                                uint16_t height);

/* Capture a rectangle of the framebuffer into the clipboard. */
bool rip_clipboard_capture(rip_state_t *s,
                           int16_t x, int16_t y,
                           int16_t width, int16_t height);

/* Save the current clipboard contents as a named icon in the runtime
 * cache.  If `out_icon` is non-NULL, also fills it with the cached
 * descriptor. */
bool rip_cache_clipboard_as_icon(rip_state_t *s,
                                 const char *name, int name_len,
                                 rip_icon_t *out_icon);

/* Save the current clipboard contents into a numbered icon slot
 * (RIPscrip v2 SAVE_ICON). */
bool rip_save_clipboard_slot(rip_state_t *s, uint16_t slot);

/* ── Blit primitives ────────────────────────────────────────────── */

/* Draw an 8bpp source rectangle into the framebuffer with the given
 * write mode.  Scales (src_w x src_h) to (dst_w x dst_h) via nearest-
 * neighbour when the dimensions differ. Modes follow RIP image semantics:
 * 0=COPY, 1=XOR, 2=OR, 3=AND, 4=invert SOURCE (NOTSRCCOPY). The public
 * drawing API's DRAW_MODE_NOT still inverts the destination. */
void rip_blit_pixels(rip_state_t *s,
                     int16_t dx, int16_t dy,
                     const uint8_t *pixels,
                     uint16_t src_w, uint16_t src_h,
                     int16_t dst_w, int16_t dst_h,
                     uint8_t write_mode);

/* Standard LOAD_ICON stretch path, with measured GDI sampling. */
void rip_blit_pixels_gdi(rip_state_t *s, int16_t x, int16_t y,
                         const uint8_t *pixels, uint16_t sw, uint16_t sh,
                         int16_t dw, int16_t dh, uint8_t mode);

/* Tile a small source rectangle across the [x0,y0..x1,y1] region. */
void rip_blit_pixels_tiled(rip_state_t *s,
                           int16_t x0, int16_t y0,
                           int16_t x1, int16_t y1,
                           const uint8_t *pixels,
                           uint16_t src_w, uint16_t src_h,
                           uint8_t write_mode);

/* Draw an icon honouring the active 1S / & ICON_STYLE mode (0=stretch,
 * 1=tile, 2=center, 3=proportional fit). */
void rip_draw_icon_pixels(rip_state_t *s,
                          int16_t x, int16_t y,
                          const uint8_t *pixels,
                          uint16_t src_w, uint16_t src_h,
                          int16_t requested_w, int16_t requested_h,
                          uint8_t write_mode, rip_image_rect_t *rendered);
