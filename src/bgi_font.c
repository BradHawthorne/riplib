/*
 * bgi_font.c — Borland BGI stroke font renderer for A2GSPU card
 *
 * Parses and renders BGI .CHR vector fonts. Each character is a series
 * of move-to (opcode 2) and line-to (opcode 3) commands, terminated
 * by opcode 0 (end of character).
 *
 * Stroke encoding (2 bytes per point):
 *   byte0[7]   = opcode bit 1
 *   byte0[6:0] = X coordinate (signed 7-bit, -64 to +63)
 *   byte1[7]   = opcode bit 0
 *   byte1[6:0] = Y coordinate (signed 7-bit, -64 to +63)
 *   opcode: 0b00 = end of char
 *           0b10 = move to (pen up)
 *           0b11 = line to (pen down)
 *
 * Coordinates are relative to character origin. The renderer scales
 * by multiplying by the RIPscrip charsize (1-10) and draws lines
 * using draw_line() from the unified drawing engine.
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#include "bgi_font.h"
#include "drawing.h"

/* Sign-extend a 7-bit value to int16_t */
static int16_t sign7(uint8_t v) {
    if (v & 0x40) return (int16_t)(v | 0xFF80);  /* negative */
    return (int16_t)(v & 0x3F);
}

bool bgi_font_parse(bgi_font_t *font, const uint8_t *data, int size) {
    if (size < 50) return false;

    /* BGI CHR binary format (bgi2c.py strips the text header, keeping
     * the binary section from after \x1A\x80 in the original .CHR file).
     *
     * The binary data has a metadata prefix, then a '+' (0x2B) marker
     * followed immediately by the font definition:
     *
     *   +0:    '+' (0x2B)
     *   +1,+2: num_chars (LE word, typically 223 = 0x20-0xFE)
     *   +3:    undefined (0x00)
     *   +4:    first_char (ASCII code of first character)
     *   +5,+6: stroke_offset from '+' (LE word, offset to stroke data)
     *   +7:    scan_flag
     *   +8:    org_to_top (signed byte, distance from origin to cap height)
     *   +9:    org_to_baseline (signed byte)
     *   +10:   org_to_bottom (signed byte, descender depth)
     *
     * Then:
     *   +11 .. +11+nchars-1:         width table (1 byte per char)
     *   +11+nchars .. +11+3*nchars-1: stroke offset table (2 bytes LE per char)
     *   +stroke_offset ..:            stroke data (2 bytes per point)
     */

    /* Scan for '+' marker (0x2B) in the binary prefix.
     * Validate: num_chars in range, first_char == 0x20, stroke_offset > 0.
     * Start search past the metadata prefix (~30 bytes) to avoid false matches. */
    int p = -1;
    for (int i = 20; i < size - 16 && i < 256; i++) {
        if (data[i] == 0x2B) {
            uint16_t nc = data[i + 1] | (data[i + 2] << 8);
            uint8_t fc = data[i + 4];
            uint16_t so = data[i + 5] | (data[i + 6] << 8);
            if (nc >= 32 && nc <= 255 && fc < 0x21 && so > 0 && so < size) {
                p = i;
                break;
            }
        }
    }
    if (p < 0) return false;

    /* Parse the 16-byte font header at '+' */
    font->num_chars  = data[p + 1] | (data[p + 2] << 8);
    font->first_char = data[p + 4];

    uint16_t stroke_off = data[p + 5] | (data[p + 6] << 8);

    font->top      = (int8_t)data[p + 8];
    font->baseline = (int8_t)data[p + 9];
    font->bottom   = (int8_t)data[p + 10];
    /* +11 through +15: reserved (5 bytes) */

    /* Stroke offset table: 2 bytes LE per character, starting at '+' + 16 */
    int off_start = p + 16;
    if (off_start + font->num_chars * 2 > size) return false;
    font->offsets = (const uint16_t *)&data[off_start];

    /* Width table: 1 byte per character, after stroke offset table */
    int wid_start = off_start + font->num_chars * 2;
    if (wid_start + font->num_chars > size) return false;
    font->widths = &data[wid_start];

    /* Stroke data: at '+' + stroke_data_offset */
    int strk_start = p + stroke_off;
    if (strk_start >= size) return false;
    font->strokes = &data[strk_start];

    font->data = data;
    font->data_size = size;

    return true;
}

/* Render one character, returns X advance */
static int16_t render_char(const bgi_font_t *font,
                            int16_t ox, int16_t oy,
                            uint8_t ch, uint8_t scale,
                            uint8_t color, uint8_t direction) {
    if (ch < font->first_char) return 0;
    int idx = ch - font->first_char;
    if (idx >= font->num_chars) return 0;

    /* Get stroke data offset for this character */
    uint16_t soff = font->offsets[idx];
    const uint8_t *sp = font->strokes + soff;
    const uint8_t *end = font->data + font->data_size;

    int16_t px = ox, py = oy;  /* pen position */
    int16_t prev_x = ox, prev_y = oy;

    draw_set_color(color);

    while (sp + 1 < end) {
        uint8_t b0 = *sp++;
        uint8_t b1 = *sp++;

        uint8_t opcode = ((b0 >> 6) & 2) | ((b1 >> 7) & 1);
        int16_t sx = sign7(b0 & 0x7F);
        int16_t sy = sign7(b1 & 0x7F);

        /* Scale coordinates */
        int16_t dx = sx * scale;
        int16_t dy = sy * scale;

        if (opcode == 0) {
            /* End of character */
            break;
        } else if (opcode == 2) {
            /* Move to (pen up) */
            if (direction == 0) {
                px = ox + dx;
                py = oy - dy;  /* BGI Y is inverted */
            } else if (direction == 1) {
                /* v3.1 dir=1: CW rotation, top-to-bottom.
                 * Readable when tilting head right. */
                px = ox + dy;
                py = oy + dx;
            } else {
                /* v3.1 dir=2: CCW rotation, top-to-bottom.
                 * Readable when tilting head left. §A2G.7 */
                px = ox - dy;
                py = oy - dx;
            }
            prev_x = px;
            prev_y = py;
        } else if (opcode == 3) {
            /* Line to (pen down) */
            int16_t nx, ny;
            if (direction == 0) {
                nx = ox + dx;
                ny = oy - dy;
            } else if (direction == 1) {
                nx = ox + dy;
                ny = oy + dx;
            } else {
                nx = ox - dy;
                ny = oy - dx;
            }
            draw_line(prev_x, prev_y, nx, ny);
            prev_x = nx;
            prev_y = ny;
            px = nx;
            py = ny;
        }
    }

    /* Return character advance width */
    return font->widths[idx] * scale;
}

int16_t bgi_font_draw_string(const bgi_font_t *font,
                              int16_t x, int16_t y,
                              const char *str, int len,
                              uint8_t scale, uint8_t color,
                              uint8_t direction) {
    if (!font || !font->strokes || scale == 0) return 0;
    if (scale > 10) scale = 10;

    int16_t advance = 0;
    for (int i = 0; i < len; i++) {
        int16_t w = render_char(font, x, y, (uint8_t)str[i],
                                 scale, color, direction);
        if (direction == 0) {
            x += w;
        } else {
            y += w;  /* v3.1 FIX: top-to-bottom (readable English).
                      * Borland v1.54 spec says bottom-to-top, which
                      * renders text backwards on screen. Corrected
                      * in A2GSPU v3.1 — no known BBS uses dir=1. */
        }
        advance += w;
    }
    return advance;
}

int16_t bgi_font_draw_string_ex(const bgi_font_t *font,
                                 int16_t x, int16_t y,
                                 const char *str, int len,
                                 uint8_t scale, uint8_t color,
                                 uint8_t direction, uint8_t attrib) {
    if (!font || !font->strokes || scale == 0) return 0;
    if (scale > 10) scale = 10;
    if (attrib == 0) {
        /* No attributes — fast path */
        return bgi_font_draw_string(font, x, y, str, len,
                                     scale, color, direction);
    }

    int16_t start_x = x, start_y = y;
    int16_t advance = 0;

    /* Shadow: draw entire string offset in dark color first */
    if (attrib & BGI_ATTR_SHADOW) {
        int16_t shx = (direction == 0) ? 1 : 0;
        int16_t shy = 1;
        /* Use a dark version of the color (shift right for dimming) */
        uint8_t shadow_col = (color >> 1) & 0x6D;  /* halve RGB332 channels */
        bgi_font_draw_string(font, x + shx, y + shy, str, len,
                              scale, shadow_col, direction);
    }

    /* Bold: draw string, then draw again offset +1px right */
    if (attrib & BGI_ATTR_BOLD) {
        bgi_font_draw_string(font, x + 1, y, str, len,
                              scale, color, direction);
    }

    /* Main string draw (italic handled by shearing the Y baseline) */
    if (attrib & BGI_ATTR_ITALIC) {
        /* Italic: draw char-by-char with X offset proportional to Y.
         * Shear factor: 0.2 * (top - baseline_offset) */
        int16_t italic_shear = (font->top > 0 ? font->top : 8) * scale / 5;
        for (int i = 0; i < len; i++) {
            int16_t w = render_char(font, x + italic_shear, y,
                                     (uint8_t)str[i], scale, color, direction);
            if (direction == 0) x += w;
            else y += w;
            advance += w;
        }
    } else {
        advance = bgi_font_draw_string(font, x, y, str, len,
                                        scale, color, direction);
        if (direction == 0) x = start_x + advance;
        else y = start_y + advance;
    }

    /* Underline: horizontal line at baseline */
    if (attrib & BGI_ATTR_UNDERLINE) {
        int16_t ul_y = start_y + 2;  /* 2px below baseline */
        draw_set_color(color);
        if (direction == 0) {
            draw_line(start_x, ul_y, start_x + advance, ul_y);
        } else {
            /* Vertical: draw vertical underline bar */
            draw_line(start_x - 2, start_y, start_x - 2, start_y + advance);
        }
    }

    return advance;
}

int16_t bgi_font_string_width(const bgi_font_t *font,
                               const char *str, int len,
                               uint8_t scale) {
    if (!font || !font->widths || scale == 0) return 0;

    int16_t total = 0;
    for (int i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)str[i];
        if (ch < font->first_char) continue;
        int idx = ch - font->first_char;
        if (idx >= font->num_chars) continue;
        total += font->widths[idx] * scale;
    }
    return total;
}
