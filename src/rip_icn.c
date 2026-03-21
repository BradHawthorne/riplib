/*
 * rip_icn.c — BGI putimage (.ICN) format parser for A2GSPU card
 *
 * ICN file layout (BGI getimage/putimage format):
 *   Bytes 0-1: (width - 1)  as uint16_t LE
 *   Bytes 2-3: (height - 1) as uint16_t LE
 *   Bytes 4-5: unused (reserved/row_bytes, ignored)
 *   Bytes 6+:  pixel data, 4 EGA bitplanes interleaved per row
 *
 * Each row stores 4 planes sequentially:
 *   plane 0 (blue):  ceil(width/8) bytes
 *   plane 1 (green): ceil(width/8) bytes
 *   plane 2 (red):   ceil(width/8) bytes
 *   plane 3 (intensity): ceil(width/8) bytes
 *
 * Each pixel's EGA index = (intensity<<3)|(red<<2)|(green<<1)|blue
 * mapped to palette indices 0-15.
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#include "rip_icn.h"

bool rip_icn_parse(const uint8_t *data, int size,
                   uint8_t *out_pixels,
                   uint16_t *out_w, uint16_t *out_h) {
    if (size < 6) return false;

    uint16_t w = (data[0] | (data[1] << 8)) + 1;
    uint16_t h = (data[2] | (data[3] << 8)) + 1;
    /* bytes 4-5 ignored */

    if (w == 0 || h == 0 || w > 640 || h > 400) return false;

    int row_bytes = (w + 7) / 8;
    int row_stride = row_bytes * 4; /* 4 planes per row */
    int expected = 6 + row_stride * h;
    if (size < expected) return false;

    *out_w = w;
    *out_h = h;

    const uint8_t *src = data + 6;

    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + y * row_stride;
        const uint8_t *p0 = row;                    /* blue */
        const uint8_t *p1 = row + row_bytes;        /* green */
        const uint8_t *p2 = row + row_bytes * 2;    /* red */
        const uint8_t *p3 = row + row_bytes * 3;    /* intensity */
        uint8_t *dst = out_pixels + y * w;

        for (int x = 0; x < w; x++) {
            int byte_idx = x >> 3;
            int bit_mask = 0x80 >> (x & 7);
            uint8_t b = (p0[byte_idx] & bit_mask) ? 1 : 0;
            uint8_t g = (p1[byte_idx] & bit_mask) ? 1 : 0;
            uint8_t r = (p2[byte_idx] & bit_mask) ? 1 : 0;
            uint8_t i = (p3[byte_idx] & bit_mask) ? 1 : 0;
            dst[x] = (i << 3) | (r << 2) | (g << 1) | b;
        }
    }

    return true;
}
