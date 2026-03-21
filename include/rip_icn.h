/*
 * rip_icn.h — BGI putimage (.ICN) format parser for A2GSPU card
 *
 * RIPscrip's native icon format: 6-byte header + 4 EGA bitplanes
 * interleaved by row. Converts to 8bpp palette indices (0-15).
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Parse an ICN (BGI putimage) file from memory.
 * Deinterleaves 4 EGA bitplanes to 8bpp palette indices (0-15).
 * out_pixels must have space for width * height bytes.
 * Returns true on success. */
bool rip_icn_parse(const uint8_t *data, int size,
                   uint8_t *out_pixels,
                   uint16_t *out_w, uint16_t *out_h);
