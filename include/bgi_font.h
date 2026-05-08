/*
 * bgi_font.h — Borland BGI stroke font renderer for A2GSPU card
 *
 * Renders vector (stroke) fonts from the BGI .CHR format used by
 * RIPscrip, Turbo Pascal, and Borland C++. Each character is defined
 * as a series of move-to and line-to operations in a coordinate grid.
 *
 * The renderer scales strokes to the requested text size and draws
 * them using draw_line() from the unified drawing engine.
 *
 * Built-in fonts (from C:\RIPtel\FONTS\):
 *   TRIP = Triplex (serif, default RIPscrip font)
 *   SANS = Sans-serif
 *   GOTH = Gothic
 *   LITT = Small
 *   SCRI = Script
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License. See LICENSE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* BGI font IDs (matching RIPscrip font numbering) */
#define BGI_FONT_DEFAULT    0  /* 8×8 bitmap (not BGI) */
#define BGI_FONT_TRIPLEX    1  /* TRIP.CHR */
#define BGI_FONT_SMALL      2  /* LITT.CHR */
#define BGI_FONT_SANS       3  /* SANS.CHR */
#define BGI_FONT_GOTHIC     4  /* GOTH.CHR */
#define BGI_FONT_SCRIPT     5  /* SCRI.CHR */
#define BGI_FONT_SIMPLEX    6  /* SIMP.CHR */
#define BGI_FONT_TRIPLEX_SCR 7 /* TSCR.CHR */
#define BGI_FONT_COMPLEX    8  /* LCOM.CHR */
#define BGI_FONT_EUROPEAN   9  /* EURO.CHR */
#define BGI_FONT_BOLD       10 /* BOLD.CHR */

/* Parsed BGI font header */
typedef struct {
    const uint8_t *data;     /* Raw .CHR binary data (after header) */
    int            data_size;
    uint16_t       num_chars; /* Number of characters in font (on-disk u16 LE) */
    uint8_t        first_char;/* ASCII code of first character */
    int16_t        top;       /* Distance from origin to top */
    int16_t        baseline;  /* Distance from origin to baseline */
    int16_t        bottom;    /* Distance from origin to bottom */
    const uint8_t *widths;    /* Character width table */
    const uint8_t *offsets;   /* LE stroke offset table (2 bytes per char) */
    const uint8_t *strokes;   /* Stroke data */
} bgi_font_t;

/* Parse a BGI .CHR font from raw binary data.
 * Returns true on success, false if data is invalid. */
bool bgi_font_parse(bgi_font_t *font, const uint8_t *data, int size);

/* Draw a string using a BGI stroke font.
 * x, y = baseline origin. scale = 1-10 (RIPscrip charsize).
 * color = palette index. direction = 0(horiz) or 1(vert).
 * Returns the X advance (total width drawn). */
int16_t bgi_font_draw_string(const bgi_font_t *font,
                              int16_t x, int16_t y,
                              const char *str, int len,
                              uint8_t scale, uint8_t color,
                              uint8_t direction);

/* Draw a string with font attributes (v3.1 extension).
 * attrib bitmask: bit0=bold, bit1=italic, bit2=underline, bit3=shadow */
#define BGI_ATTR_BOLD      0x01
#define BGI_ATTR_ITALIC    0x02
#define BGI_ATTR_UNDERLINE 0x04
#define BGI_ATTR_SHADOW    0x08

int16_t bgi_font_draw_string_ex(const bgi_font_t *font,
                                 int16_t x, int16_t y,
                                 const char *str, int len,
                                 uint8_t scale, uint8_t color,
                                 uint8_t direction, uint8_t attrib);

/* Get the width of a string in pixels at the given scale. */
int16_t bgi_font_string_width(const bgi_font_t *font,
                               const char *str, int len,
                               uint8_t scale);
