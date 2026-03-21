/*
 * drawing.c — Unified rendering primitives for A2GSPU card
 *
 * ALL UHR/Mosaic/protocol framebuffer pixel operations go through here.
 * This is the single source of truth for pixel writes, clipping, and
 * drawing state. IIgs display mode renderers (TEXT40, SHR, etc.) and
 * the terminal grid renderer are the only exceptions — they have
 * specialized pixel formats that don't map to these primitives.
 *
 * Algorithms (all integer, no FPU required — though RP2350 has dual FPUs):
 *   Bresenham (1965) — line
 *   Pitteway (1967), Van Aken (1984) — midpoint circle
 *   Van Aken & Novak (1985) — midpoint ellipse
 *   Cohen & Sutherland (1967) — line clipping
 *   De Casteljau (1959) — Bezier subdivision
 *   Shani (1980) — scanline flood fill
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#include "drawing.h"
#include <string.h>
#include <stdlib.h>  /* abs() */
#include <math.h>    /* atan2f, sinf, cosf — RP2350 has dual single-precision FPUs */

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ── Framebuffer reference ───────────────────────────────────────── */
static uint8_t *g_fb;
static uint16_t g_pitch;
static uint16_t g_width;
static uint16_t g_height;

/* ── Drawing state ───────────────────────────────────────────────── */
static uint8_t  g_color;
static int16_t  g_pos_x, g_pos_y;
static int16_t  g_clip_x0, g_clip_y0, g_clip_x1, g_clip_y1;
static uint8_t  g_write_mode = DRAW_MODE_COPY;
static uint8_t  g_line_pattern = 0xFF;  /* solid */
static uint8_t  g_line_thickness = 1;
static uint8_t  g_fill_pattern = 0;     /* 0 = solid */
static uint8_t  g_fill_color = 0;
static int16_t  g_arc_radius = 0;

/* ── Built-in fill patterns (8×8, 1bpp) ─────────────────────────── */
static const uint8_t fill_patterns[11][8] = {
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, /* 0: solid */
    {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55}, /* 1: 50% checker */
    {0x88,0x44,0x22,0x11,0x88,0x44,0x22,0x11}, /* 2: diagonal \ */
    {0x11,0x22,0x44,0x88,0x11,0x22,0x44,0x88}, /* 3: diagonal / */
    {0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00}, /* 4: horizontal */
    {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA}, /* 5: vertical */
    {0xFF,0xAA,0xFF,0xAA,0xFF,0xAA,0xFF,0xAA}, /* 6: cross-hatch */
    {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01}, /* 7: light diagonal */
    {0xCC,0x33,0xCC,0x33,0xCC,0x33,0xCC,0x33}, /* 8: interleave (BGI 9) */
    {0x80,0x00,0x08,0x00,0x80,0x00,0x08,0x00}, /* 9: wide dot (BGI 10) */
    {0xAA,0x00,0xAA,0x00,0xAA,0x00,0xAA,0x00}, /* 10: close dot (BGI 11) */
};
static uint8_t user_pattern[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/* ── Dirty-row callback ──────────────────────────────────────────── */
static draw_dirty_callback_t g_dirty_cb = NULL;

void draw_set_dirty_callback(draw_dirty_callback_t cb) {
    g_dirty_cb = cb;
}

static inline void mark_dirty(int16_t y_min, int16_t y_max) {
    if (g_dirty_cb) g_dirty_cb(y_min, y_max);
}

/* ── Flood fill stack ────────────────────────────────────────────── */
#define FLOOD_STACK_SIZE 1024
typedef struct { int16_t x, y; } flood_entry_t;
static flood_entry_t flood_stack[FLOOD_STACK_SIZE];

/* ══════════════════════════════════════════════════════════════════
 * INITIALIZATION
 * ══════════════════════════════════════════════════════════════════ */

void draw_init(uint8_t *framebuf, uint16_t pitch, uint16_t width, uint16_t height) {
    g_fb = framebuf;
    g_pitch = pitch;
    g_width = width;
    g_height = height;
    g_color = 0;
    g_pos_x = 0;
    g_pos_y = 0;
    g_clip_x0 = 0;
    g_clip_y0 = 0;
    g_clip_x1 = width - 1;
    g_clip_y1 = height - 1;
    g_write_mode = DRAW_MODE_COPY;
    g_line_pattern = 0xFF;
    g_line_thickness = 1;
    g_fill_pattern = 0;
    g_fill_color = 0;
    g_arc_radius = 0;
}

/* ══════════════════════════════════════════════════════════════════
 * STATE
 * ══════════════════════════════════════════════════════════════════ */

void draw_set_color(uint8_t color) { g_color = color; }
uint8_t draw_get_color(void) { return g_color; }
void draw_set_pos(int16_t x, int16_t y) { g_pos_x = x; g_pos_y = y; }
int16_t draw_get_pos_x(void) { return g_pos_x; }
int16_t draw_get_pos_y(void) { return g_pos_y; }
uint8_t *draw_get_fb(void) { return g_fb; }
int16_t draw_get_clip_x0(void) { return g_clip_x0; }
int16_t draw_get_clip_y0(void) { return g_clip_y0; }
int16_t draw_get_clip_x1(void) { return g_clip_x1; }
int16_t draw_get_clip_y1(void) { return g_clip_y1; }

void draw_set_clip(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= g_width)  x1 = g_width - 1;
    if (y1 >= g_height) y1 = g_height - 1;
    g_clip_x0 = x0; g_clip_y0 = y0;
    g_clip_x1 = x1; g_clip_y1 = y1;
}

void draw_reset_clip(void) {
    g_clip_x0 = 0; g_clip_y0 = 0;
    g_clip_x1 = g_width - 1; g_clip_y1 = g_height - 1;
}

void draw_save_clip(draw_clip_state_t *state) {
    state->x0 = g_clip_x0;
    state->y0 = g_clip_y0;
    state->x1 = g_clip_x1;
    state->y1 = g_clip_y1;
}

void draw_restore_clip(const draw_clip_state_t *state) {
    g_clip_x0 = state->x0;
    g_clip_y0 = state->y0;
    g_clip_x1 = state->x1;
    g_clip_y1 = state->y1;
}

void draw_set_write_mode(uint8_t mode) {
    g_write_mode = mode;
}

void draw_set_line_style(uint8_t pattern, uint8_t thickness) {
    g_line_pattern = pattern;
    g_line_thickness = thickness > 0 ? thickness : 1;
}

void draw_set_fill_style(uint8_t pattern_id, uint8_t fill_color) {
    g_fill_pattern = pattern_id;
    g_fill_color = fill_color;
}

void draw_set_user_fill_pattern(const uint8_t *pattern) {
    for (int i = 0; i < 8; i++)
        user_pattern[i] = pattern[i];
}

void draw_set_arc_radius(int16_t radius) {
    g_arc_radius = radius;
}

/* ══════════════════════════════════════════════════════════════════
 * CORE PIXEL WRITE — single point of truth for all pixel output
 *
 * All drawing functions funnel through put_pixel() or fill_span().
 * Write mode (COPY/XOR/OR) is applied here, nowhere else.
 * ══════════════════════════════════════════════════════════════════ */

static inline void put_pixel(int16_t x, int16_t y) {
    if (x >= g_clip_x0 && x <= g_clip_x1 &&
        y >= g_clip_y0 && y <= g_clip_y1) {
        uint8_t *p = &g_fb[y * g_pitch + x];
        switch (g_write_mode) {
        default:
        case DRAW_MODE_COPY: *p = g_color; break;
        case DRAW_MODE_OR:   *p |= g_color; break;
        case DRAW_MODE_AND:  *p &= g_color; break;
        case DRAW_MODE_XOR:  *p ^= g_color; break;
        case DRAW_MODE_NOT:  *p = ~(*p); break;
        }
    }
}

/* Fill a horizontal span, respecting write mode and fill pattern.
 * This is the inner loop for all filled shapes. */
static void fill_span(int16_t x, int16_t y, int16_t len) {
    if (len <= 0 || y < g_clip_y0 || y > g_clip_y1) return;
    int16_t x0 = x, x1 = x + len - 1;
    if (x0 < g_clip_x0) x0 = g_clip_x0;
    if (x1 > g_clip_x1) x1 = g_clip_x1;
    if (x0 > x1) return;

    uint8_t *row = &g_fb[y * g_pitch];
    int16_t count = x1 - x0 + 1;

    /* Fast path: solid fill + COPY mode → memset */
    if (g_fill_pattern == 0 && g_write_mode == DRAW_MODE_COPY) {
        memset(&row[x0], g_color, count);
        return;
    }

    /* Pattern + write mode path */
    const uint8_t *pat = (g_fill_pattern >= 1 && g_fill_pattern <= 10)
                         ? fill_patterns[g_fill_pattern]
                         : (g_fill_pattern == 11) ? user_pattern
                         : fill_patterns[0];
    uint8_t pat_row = pat[y & 7];

    for (int16_t px = x0; px <= x1; px++) {
        uint8_t c = (pat_row & (0x80 >> (px & 7))) ? g_color : g_fill_color;
        switch (g_write_mode) {
        default:
        case DRAW_MODE_COPY: row[px] = c; break;
        case DRAW_MODE_OR:   row[px] |= c; break;
        case DRAW_MODE_AND:  row[px] &= c; break;
        case DRAW_MODE_XOR:  row[px] ^= c; break;
        case DRAW_MODE_NOT:  row[px] = ~row[px]; break;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 * PIXEL + AXIS-ALIGNED LINES + FILL SCREEN
 * ══════════════════════════════════════════════════════════════════ */

void draw_pixel(int16_t x, int16_t y) {
    put_pixel(x, y);
    mark_dirty(y, y);
}

void draw_hline(int16_t x, int16_t y, int16_t len) {
    if (len <= 0 || y < g_clip_y0 || y > g_clip_y1) return;
    int16_t x0 = x, x1 = x + len - 1;
    if (x0 < g_clip_x0) x0 = g_clip_x0;
    if (x1 > g_clip_x1) x1 = g_clip_x1;
    if (x0 > x1) return;

    uint8_t *row = &g_fb[y * g_pitch];
    int16_t count = x1 - x0 + 1;

    if (g_write_mode == DRAW_MODE_COPY) {
        memset(&row[x0], g_color, count);
    } else {
        for (int16_t px = x0; px <= x1; px++) {
            switch (g_write_mode) {
            case DRAW_MODE_OR:  row[px] |= g_color; break;
            case DRAW_MODE_AND: row[px] &= g_color; break;
            case DRAW_MODE_XOR: row[px] ^= g_color; break;
            case DRAW_MODE_NOT: row[px] = ~row[px]; break;
            default:            row[px] = g_color; break;
            }
        }
    }
    mark_dirty(y, y);
}

void draw_vline(int16_t x, int16_t y, int16_t len) {
    if (len <= 0 || x < g_clip_x0 || x > g_clip_x1) return;
    int16_t y0 = y, y1 = y + len - 1;
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    for (int16_t row = y0; row <= y1; row++) {
        uint8_t *p = &g_fb[row * g_pitch + x];
        switch (g_write_mode) {
        default:
        case DRAW_MODE_COPY: *p = g_color; break;
        case DRAW_MODE_OR:   *p |= g_color; break;
        case DRAW_MODE_AND:  *p &= g_color; break;
        case DRAW_MODE_XOR:  *p ^= g_color; break;
        case DRAW_MODE_NOT:  *p = ~(*p); break;
        }
    }
    mark_dirty(y0, y1);
}

void draw_fill_screen(uint8_t color) {
    memset(g_fb, color, g_height * g_pitch);
    mark_dirty(0, (int16_t)(g_height - 1));
}

/* Mark the entire framebuffer dirty without writing any pixels.
 * Called by $REFRESH$ text variable to force a full repaint after
 * a $NOREFRESH$ scene build sequence (equivalent to ripInvalidateAll()
 * in the DLL @ 0x026218). */
void draw_mark_all_dirty(void) {
    mark_dirty(0, (int16_t)(g_height - 1));
}

void draw_write_pixel(uint8_t color) {
    if (g_pos_x >= 0 && g_pos_x < g_width &&
        g_pos_y >= 0 && g_pos_y < g_height) {
        g_fb[g_pos_y * g_pitch + g_pos_x] = color;
        mark_dirty(g_pos_y, g_pos_y);
    }
    g_pos_x++;
    if (g_pos_x >= g_width) { g_pos_x = 0; g_pos_y++; }
    if (g_pos_y >= g_height) g_pos_y = 0;
}

/* ══════════════════════════════════════════════════════════════════
 * COHEN-SUTHERLAND LINE CLIPPING
 * ══════════════════════════════════════════════════════════════════ */

#define OC_LEFT   1
#define OC_RIGHT  2
#define OC_BOTTOM 4
#define OC_TOP    8

static int outcode(int16_t x, int16_t y) {
    int code = 0;
    if (x < g_clip_x0) code |= OC_LEFT;
    else if (x > g_clip_x1) code |= OC_RIGHT;
    if (y < g_clip_y0) code |= OC_TOP;
    else if (y > g_clip_y1) code |= OC_BOTTOM;
    return code;
}

static bool clip_line(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1) {
    int oc0 = outcode(*x0, *y0);
    int oc1 = outcode(*x1, *y1);

    for (;;) {
        if ((oc0 | oc1) == 0) return true;
        if ((oc0 & oc1) != 0) return false;

        int oc_out = oc0 ? oc0 : oc1;
        int16_t x, y;
        int16_t dx = *x1 - *x0, dy = *y1 - *y0;

        if (oc_out & OC_TOP) {
            x = *x0 + (int32_t)dx * (g_clip_y0 - *y0) / dy;
            y = g_clip_y0;
        } else if (oc_out & OC_BOTTOM) {
            x = *x0 + (int32_t)dx * (g_clip_y1 - *y0) / dy;
            y = g_clip_y1;
        } else if (oc_out & OC_RIGHT) {
            y = *y0 + (int32_t)dy * (g_clip_x1 - *x0) / dx;
            x = g_clip_x1;
        } else {
            y = *y0 + (int32_t)dy * (g_clip_x0 - *x0) / dx;
            x = g_clip_x0;
        }

        if (oc_out == oc0) {
            *x0 = x; *y0 = y;
            oc0 = outcode(*x0, *y0);
        } else {
            *x1 = x; *y1 = y;
            oc1 = outcode(*x1, *y1);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 * BRESENHAM LINE — with dash pattern support
 * ══════════════════════════════════════════════════════════════════ */

void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if (!clip_line(&x0, &y0, &x1, &y1)) return;

    int16_t dy_min = y0 < y1 ? y0 : y1;
    int16_t dy_max = y0 < y1 ? y1 : y0;

    int16_t dx = abs(x1 - x0);
    int16_t dy = -abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;
    uint8_t phase = 0;

    for (;;) {
        /* Dash pattern: bit in pattern selects draw vs skip */
        if ((g_line_pattern >> (phase & 7)) & 1) {
            uint8_t *p = &g_fb[y0 * g_pitch + x0];
            switch (g_write_mode) {
            default:
            case DRAW_MODE_COPY: *p = g_color; break;
            case DRAW_MODE_XOR:  *p ^= g_color; break;
            case DRAW_MODE_OR:   *p |= g_color; break;
            }
        }
        if (x0 == x1 && y0 == y1) break;
        phase++;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    mark_dirty(dy_min, dy_max);
}

/* ══════════════════════════════════════════════════════════════════
 * RECTANGLE
 * ══════════════════════════════════════════════════════════════════ */

void draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill) {
    if (w <= 0 || h <= 0) return;
    if (fill) {
        for (int16_t row = 0; row < h; row++)
            fill_span(x, y + row, w);
        mark_dirty(y, (int16_t)(y + h - 1));
    } else {
        draw_hline(x, y, w);
        draw_hline(x, y + h - 1, w);
        draw_vline(x, y, h);
        draw_vline(x + w - 1, y, h);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * ROUNDED RECTANGLE
 * Uses midpoint circle algorithm for quarter-arc corners with
 * straight edges between. Clamps radius to half the smaller dimension.
 * ══════════════════════════════════════════════════════════════════ */

void draw_rounded_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t r, bool fill) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { draw_rect(x, y, w, h, fill); return; }

    /* Clamp radius to half the smaller dimension */
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    /* Corner centers */
    int16_t cx_tl = x + r,         cy_tl = y + r;          /* top-left */
    int16_t cx_tr = x + w - 1 - r, cy_tr = y + r;          /* top-right */
    int16_t cx_bl = x + r,         cy_bl = y + h - 1 - r;  /* bottom-left */
    int16_t cx_br = x + w - 1 - r, cy_br = y + h - 1 - r;  /* bottom-right */

    if (fill) {
        /* Fill: horizontal spans for the rounded shape */
        /* Top rounded section */
        int16_t px = r, py = 0;
        int16_t d = 1 - r;
        while (px >= py) {
            /* Each circle octant maps to a span across TL↔TR or BL↔BR corners */
            fill_span(cx_tl - px, cy_tl - py, cx_tr - cx_tl + 2 * px + 1);
            fill_span(cx_tl - py, cy_tl - px, cx_tr - cx_tl + 2 * py + 1);
            fill_span(cx_bl - px, cy_bl + py, cx_br - cx_bl + 2 * px + 1);
            fill_span(cx_bl - py, cy_bl + px, cx_br - cx_bl + 2 * py + 1);
            py++;
            if (d < 0) {
                d += 2 * py + 1;
            } else {
                px--;
                d += 2 * (py - px) + 1;
            }
        }
        /* Middle rectangular section (between top and bottom curves) */
        for (int16_t row = cy_tl + 1; row < cy_bl; row++)
            fill_span(x, row, w);
        mark_dirty(y, (int16_t)(y + h - 1));
    } else {
        /* Outline: quarter-arcs at corners + straight edges between */
        int16_t px = r, py = 0;
        int16_t d = 1 - r;
        while (px >= py) {
            /* TL corner (quadrant 2: 90-180°) */
            put_pixel(cx_tl - px, cy_tl - py);
            put_pixel(cx_tl - py, cy_tl - px);
            /* TR corner (quadrant 1: 0-90°) */
            put_pixel(cx_tr + px, cy_tr - py);
            put_pixel(cx_tr + py, cy_tr - px);
            /* BL corner (quadrant 3: 180-270°) */
            put_pixel(cx_bl - px, cy_bl + py);
            put_pixel(cx_bl - py, cy_bl + px);
            /* BR corner (quadrant 4: 270-360°) */
            put_pixel(cx_br + px, cy_br + py);
            put_pixel(cx_br + py, cy_br + px);
            py++;
            if (d < 0) {
                d += 2 * py + 1;
            } else {
                px--;
                d += 2 * (py - px) + 1;
            }
        }
        /* Straight edges between corners */
        draw_hline(cx_tl, y, cx_tr - cx_tl + 1);              /* top */
        draw_hline(cx_bl, y + h - 1, cx_br - cx_bl + 1);      /* bottom */
        draw_vline(x, cy_tl, cy_bl - cy_tl + 1);              /* left */
        draw_vline(x + w - 1, cy_tr, cy_br - cy_tr + 1);      /* right */
    }
}

/* ══════════════════════════════════════════════════════════════════
 * MIDPOINT CIRCLE
 * ══════════════════════════════════════════════════════════════════ */

void draw_circle(int16_t cx, int16_t cy, int16_t r, bool fill) {
    if (r <= 0) return;
    int16_t x = r, y = 0;
    int16_t d = 1 - r;

    while (x >= y) {
        if (fill) {
            fill_span(cx - x, cy + y, 2 * x + 1);
            fill_span(cx - x, cy - y, 2 * x + 1);
            fill_span(cx - y, cy + x, 2 * y + 1);
            fill_span(cx - y, cy - x, 2 * y + 1);
        } else {
            put_pixel(cx + x, cy + y);
            put_pixel(cx - x, cy + y);
            put_pixel(cx + x, cy - y);
            put_pixel(cx - x, cy - y);
            put_pixel(cx + y, cy + x);
            put_pixel(cx - y, cy + x);
            put_pixel(cx + y, cy - x);
            put_pixel(cx - y, cy - x);
        }
        y++;
        if (d < 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
    mark_dirty((int16_t)(cy - r), (int16_t)(cy + r));
}

/* ══════════════════════════════════════════════════════════════════
 * MIDPOINT ELLIPSE
 * ══════════════════════════════════════════════════════════════════ */

void draw_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, bool fill) {
    if (rx <= 0 || ry <= 0) return;

    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int16_t x = 0, y = ry;
    int32_t d1 = ry2 - rx2 * ry + rx2 / 4;

    while (ry2 * x < rx2 * y) {
        if (fill) {
            fill_span(cx - x, cy + y, 2 * x + 1);
            fill_span(cx - x, cy - y, 2 * x + 1);
        } else {
            put_pixel(cx + x, cy + y);
            put_pixel(cx - x, cy + y);
            put_pixel(cx + x, cy - y);
            put_pixel(cx - x, cy - y);
        }
        x++;
        if (d1 < 0) {
            d1 += 2 * ry2 * x + ry2;
        } else {
            y--;
            d1 += 2 * ry2 * x - 2 * rx2 * y + ry2;
        }
    }

    int32_t d2 = ry2 * ((int32_t)(2 * x + 1) * (2 * x + 1)) / 4
               + rx2 * ((int32_t)(y - 1) * (y - 1))
               - rx2 * ry2;
    while (y >= 0) {
        if (fill) {
            fill_span(cx - x, cy + y, 2 * x + 1);
            fill_span(cx - x, cy - y, 2 * x + 1);
        } else {
            put_pixel(cx + x, cy + y);
            put_pixel(cx - x, cy + y);
            put_pixel(cx + x, cy - y);
            put_pixel(cx - x, cy - y);
        }
        y--;
        if (d2 > 0) {
            d2 += rx2 - 2 * rx2 * y;
        } else {
            x++;
            d2 += 2 * ry2 * x - 2 * rx2 * y + rx2;
        }
    }
    mark_dirty((int16_t)(cy - ry), (int16_t)(cy + ry));
}

/* ══════════════════════════════════════════════════════════════════
 * ARC — Midpoint circle with angle filtering
 * ══════════════════════════════════════════════════════════════════ */

/* FPU-accelerated angle calculation using atan2f.
 * RP2350 dual FPUs: single-precision in 1-2 cycles, no penalty.
 * Returns 0-359 degrees. Much more accurate than the integer
 * approximation it replaces (~1° error → <0.01° error). */
static int16_t pixel_angle(int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) return 0;
    float rad = atan2f((float)dy, (float)dx);
    int16_t deg = (int16_t)(rad * (180.0f / M_PI));
    return ((deg % 360) + 360) % 360;
}

static bool angle_in_range(int16_t angle, int16_t start, int16_t end) {
    start = ((start % 360) + 360) % 360;
    end = ((end % 360) + 360) % 360;
    if (start <= end)
        return angle >= start && angle <= end;
    else
        return angle >= start || angle <= end;
}

static void arc_pixel(int16_t cx, int16_t cy, int16_t dx, int16_t dy,
                       int16_t start, int16_t end) {
    if (angle_in_range(pixel_angle(dx, -dy), start, end))
        put_pixel(cx + dx, cy + dy);
}

void draw_arc(int16_t cx, int16_t cy, int16_t r,
              int16_t start_deg, int16_t end_deg) {
    if (r <= 0) return;
    int16_t x = r, y = 0;
    int16_t d = 1 - r;

    while (x >= y) {
        arc_pixel(cx, cy,  x,  y, start_deg, end_deg);
        arc_pixel(cx, cy, -x,  y, start_deg, end_deg);
        arc_pixel(cx, cy,  x, -y, start_deg, end_deg);
        arc_pixel(cx, cy, -x, -y, start_deg, end_deg);
        arc_pixel(cx, cy,  y,  x, start_deg, end_deg);
        arc_pixel(cx, cy, -y,  x, start_deg, end_deg);
        arc_pixel(cx, cy,  y, -x, start_deg, end_deg);
        arc_pixel(cx, cy, -y, -x, start_deg, end_deg);

        y++;
        if (d < 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
    mark_dirty((int16_t)(cy - r), (int16_t)(cy + r));
}

/* ══════════════════════════════════════════════════════════════════
 * SCANLINE FLOOD FILL
 * ══════════════════════════════════════════════════════════════════ */

void draw_flood_fill(int16_t x, int16_t y, uint8_t border_color) {
    if (!g_fb) return;
    if (x < g_clip_x0 || x > g_clip_x1 || y < g_clip_y0 || y > g_clip_y1)
        return;

    uint8_t fill = g_color;
    uint8_t seed = g_fb[y * g_pitch + x];
    if (seed == border_color || seed == fill) return;

    int top = 0;
    flood_stack[top++] = (flood_entry_t){x, y};

    int16_t dirty_y0 = y, dirty_y1 = y;

    while (top > 0) {
        flood_entry_t e = flood_stack[--top];
        int16_t px = e.x, py = e.y;

        uint8_t c = g_fb[py * g_pitch + px];
        if (c == border_color || c == fill) continue;

        if (py < dirty_y0) dirty_y0 = py;
        if (py > dirty_y1) dirty_y1 = py;

        int16_t lx = px;
        while (lx > g_clip_x0 &&
               g_fb[py * g_pitch + (lx - 1)] != border_color &&
               g_fb[py * g_pitch + (lx - 1)] != fill)
            lx--;

        int16_t rx = lx;
        bool above_started = false;
        bool below_started = false;

        while (rx <= g_clip_x1) {
            c = g_fb[py * g_pitch + rx];
            if (c == border_color || c == fill) break;

            g_fb[py * g_pitch + rx] = fill;

            if (py > g_clip_y0) {
                uint8_t ca = g_fb[(py - 1) * g_pitch + rx];
                if (ca != border_color && ca != fill) {
                    if (!above_started && top < FLOOD_STACK_SIZE) {
                        flood_stack[top++] = (flood_entry_t){rx, (int16_t)(py - 1)};
                        above_started = true;
                    }
                } else {
                    above_started = false;
                }
            }

            if (py < g_clip_y1) {
                uint8_t cb = g_fb[(py + 1) * g_pitch + rx];
                if (cb != border_color && cb != fill) {
                    if (!below_started && top < FLOOD_STACK_SIZE) {
                        flood_stack[top++] = (flood_entry_t){rx, (int16_t)(py + 1)};
                        below_started = true;
                    }
                } else {
                    below_started = false;
                }
            }

            rx++;
        }
    }
    /* Second pass: apply fill pattern if active.
     * The solid fill above wrote g_color to every pixel in the region.
     * Now replace pixels where the pattern bit is OFF with g_fill_color.
     * This is the DLL-compatible patterned flood fill behavior. */
    if (g_fill_pattern != 0 && fill != g_fill_color) {
        const uint8_t *pat = (g_fill_pattern >= 1 && g_fill_pattern <= 10)
                             ? fill_patterns[g_fill_pattern]
                             : (g_fill_pattern == 11) ? user_pattern
                             : NULL;
        if (pat) {
            for (int16_t py = dirty_y0; py <= dirty_y1; py++) {
                uint8_t pat_row = pat[py & 7];
                uint8_t *row = &g_fb[py * g_pitch];
                int16_t x0 = g_clip_x0, x1 = g_clip_x1;
                for (int16_t px = x0; px <= x1; px++) {
                    if (row[px] == fill) {
                        if (!(pat_row & (0x80 >> (px & 7)))) {
                            row[px] = g_fill_color;
                        }
                    }
                }
            }
        }
    }

    mark_dirty(dirty_y0, dirty_y1);
}

/* ══════════════════════════════════════════════════════════════════
 * DE CASTELJAU CUBIC BEZIER
 * ══════════════════════════════════════════════════════════════════ */

/* FPU cubic Bezier — parametric evaluation with adaptive step count.
 * Uses single-precision float for exact interpolation (no integer
 * rounding accumulation). Cortex-M33 FMUL is 1 cycle. */
void draw_bezier(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                 int16_t x2, int16_t y2, int16_t x3, int16_t y3) {
    /* Adaptive step count based on curve length estimate */
    float dx = (float)(x3 - x0), dy = (float)(y3 - y0);
    float chord = sqrtf(dx * dx + dy * dy);
    float ctrl = sqrtf((float)((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0))) +
                 sqrtf((float)((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1))) +
                 sqrtf((float)((x3-x2)*(x3-x2) + (y3-y2)*(y3-y2)));
    int steps = (int)(ctrl * 0.5f);
    if (steps < 4) steps = 4;
    if (steps > 64) steps = 64;

    float fx0 = (float)x0, fy0 = (float)y0;
    float fx1 = (float)x1, fy1 = (float)y1;
    float fx2 = (float)x2, fy2 = (float)y2;
    float fx3 = (float)x3, fy3 = (float)y3;

    int16_t px = x0, py = y0;
    float t_step = 1.0f / (float)steps;

    for (int i = 1; i <= steps; i++) {
        float t = (i == steps) ? 1.0f : (float)i * t_step;
        float u = 1.0f - t;
        float u2 = u * u, t2 = t * t;
        float u3 = u2 * u, t3 = t2 * t;

        float x = u3 * fx0 + 3.0f * u2 * t * fx1 +
                  3.0f * u * t2 * fx2 + t3 * fx3;
        float y = u3 * fy0 + 3.0f * u2 * t * fy1 +
                  3.0f * u * t2 * fy2 + t3 * fy3;

        int16_t nx = (int16_t)(x + 0.5f);
        int16_t ny = (int16_t)(y + 0.5f);
        draw_line(px, py, nx, ny);
        px = nx;
        py = ny;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * RASTER / REGION OPERATIONS
 * ══════════════════════════════════════════════════════════════════ */

void draw_copy_rect(int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                    int16_t w, int16_t h) {
    if (w <= 0 || h <= 0 || !g_fb) return;
    /* Clamp to framebuffer */
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if (sx + w > g_width)  w = g_width - sx;
    if (sy + h > g_height) h = g_height - sy;
    if (dx + w > g_width)  w = g_width - dx;
    if (dy + h > g_height) h = g_height - dy;
    if (w <= 0 || h <= 0) return;

    /* Use memmove order to handle overlapping regions */
    if (dy < sy || (dy == sy && dx <= sx)) {
        for (int16_t r = 0; r < h; r++)
            memmove(&g_fb[(dy + r) * g_pitch + dx],
                    &g_fb[(sy + r) * g_pitch + sx], w);
    } else {
        for (int16_t r = h - 1; r >= 0; r--)
            memmove(&g_fb[(dy + r) * g_pitch + dx],
                    &g_fb[(sy + r) * g_pitch + sx], w);
    }
    mark_dirty(dy, (int16_t)(dy + h - 1));
}

void draw_save_region(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint8_t *dest) {
    if (!g_fb || !dest || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; dest -= x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_width)  w = g_width - x;
    if (y + h > g_height) h = g_height - y;
    for (int16_t r = 0; r < h; r++)
        memcpy(&dest[r * w], &g_fb[(y + r) * g_pitch + x], w);
}

void draw_restore_region(int16_t x, int16_t y, int16_t w, int16_t h,
                         const uint8_t *src) {
    if (!g_fb || !src || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; src -= x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_width)  w = g_width - x;
    if (y + h > g_height) h = g_height - y;
    if (w <= 0 || h <= 0) return;
    for (int16_t r = 0; r < h; r++)
        memcpy(&g_fb[(y + r) * g_pitch + x], &src[r * w], w);
    mark_dirty(y, (int16_t)(y + h - 1));
}

uint8_t draw_get_pixel(int16_t x, int16_t y) {
    if (!g_fb || x < 0 || x >= g_width || y < 0 || y >= g_height) return 0;
    return g_fb[y * g_pitch + x];
}

/* ══════════════════════════════════════════════════════════════════
 * TEXT OVERLAY — render string at pixel coordinates
 *
 * Used by: status overlay, protocol text commands (RIPscrip !|T,
 * ReGIS T'str', HPGL LB), Wozix window titles, Nuklear text.
 * Replaces 2 duplicate glyph renderers in main.c.
 *
 * font: glyph bitmap data. 8 pixels wide, font_height rows per glyph.
 *   For Apple II character_rom: 7-bit wide, 8 rows, indexed as
 *   character_rom[(ch << 3) | glyph_line], bits 0-6 = pixels.
 *   For cp437_8x16: 8-bit wide, 16 rows, indexed as
 *   cp437_8x16[ch * 16 + glyph_line], bits 7-0 = pixels.
 *   NULL font = no rendering (caller must provide valid font).
 * bg: 0xFF means transparent background (only draw foreground pixels).
 * ══════════════════════════════════════════════════════════════════ */

void draw_text(int16_t x, int16_t y, const char *str, int len,
               const uint8_t *font, uint8_t font_height,
               uint8_t fg, uint8_t bg) {
    if (!g_fb || !font || !str || len <= 0) return;

    uint8_t font_width = 8;  /* all our fonts are 8px wide max */

    for (int ci = 0; ci < len; ci++) {
        uint8_t ch = (uint8_t)str[ci];
        int16_t cx = x + ci * font_width;

        /* Quick reject: entire glyph off-screen */
        if (cx + font_width <= g_clip_x0 || cx > g_clip_x1) continue;
        if (y + font_height <= g_clip_y0 || y > g_clip_y1) return;

        for (uint8_t gl = 0; gl < font_height; gl++) {
            int16_t py = y + gl;
            if (py < g_clip_y0 || py > g_clip_y1) continue;

            uint8_t bits;
            if (font_height <= 8) {
                /* Apple II character ROM: 7-bit, 8 rows, (ch<<3)|line */
                bits = font[((uint16_t)ch << 3) | gl];
            } else {
                /* CP437-style: 8-bit, N rows, ch*height+line */
                bits = font[(uint16_t)ch * font_height + gl];
            }

            uint8_t *row = &g_fb[py * g_pitch];
            for (int bit = 0; bit < 8; bit++) {
                int16_t px = cx + bit;
                if (px < g_clip_x0 || px > g_clip_x1) continue;

                bool on;
                if (font_height <= 8)
                    on = (bits >> bit) & 1;        /* character_rom: LSB first */
                else
                    on = (bits >> (7 - bit)) & 1;  /* cp437: MSB first */

                if (on)
                    row[px] = fg;
                else if (bg != 0xFF)
                    row[px] = bg;
            }
        }
    }
    mark_dirty(y, (int16_t)(y + font_height - 1));
}

/* ══════════════════════════════════════════════════════════════════
 * POLYLINE — connected multi-segment line
 * ══════════════════════════════════════════════════════════════════ */

void draw_polyline(const int16_t *points, int n) {
    if (!points || n < 2) return;
    for (int i = 0; i < n - 1; i++) {
        draw_line(points[i * 2], points[i * 2 + 1],
                  points[(i + 1) * 2], points[(i + 1) * 2 + 1]);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * POLYGON — closed path, outline or scanline-filled
 *
 * Outline: polyline + closing segment.
 * Filled: scanline fill using sorted active edge table (AET).
 * Handles convex and concave polygons correctly.
 * ══════════════════════════════════════════════════════════════════ */

void draw_polygon(const int16_t *points, int n, bool fill) {
    if (!points || n < 3) return;

    if (!fill) {
        /* Outline: draw edges + closing segment */
        draw_polyline(points, n);
        draw_line(points[(n - 1) * 2], points[(n - 1) * 2 + 1],
                  points[0], points[1]);
        return;
    }

    /* Scanline fill: find Y extent, then for each scanline find
     * all edge intersections, sort, fill between pairs. */

    /* Find Y bounds */
    int16_t y_min = points[1], y_max = points[1];
    for (int i = 1; i < n; i++) {
        int16_t y = points[i * 2 + 1];
        if (y < y_min) y_min = y;
        if (y > y_max) y_max = y;
    }

    /* Clamp to clip rect */
    if (y_min < g_clip_y0) y_min = g_clip_y0;
    if (y_max > g_clip_y1) y_max = g_clip_y1;

    /* For each scanline, collect X intersections with all edges */
    #define MAX_POLY_INTERSECTIONS 64
    int16_t x_ints[MAX_POLY_INTERSECTIONS];

    for (int16_t y = y_min; y <= y_max; y++) {
        int num_ints = 0;

        for (int i = 0; i < n && num_ints < MAX_POLY_INTERSECTIONS; i++) {
            int j = (i + 1) % n;
            int16_t y0 = points[i * 2 + 1];
            int16_t y1 = points[j * 2 + 1];

            /* Skip horizontal edges and edges not crossing this scanline */
            if (y0 == y1) continue;
            if ((y < y0 && y < y1) || (y >= y0 && y >= y1)) continue;

            /* Compute X intersection using integer math */
            int16_t x0 = points[i * 2];
            int16_t x1 = points[j * 2];
            int16_t xi = x0 + (int32_t)(y - y0) * (x1 - x0) / (y1 - y0);
            x_ints[num_ints++] = xi;
        }

        /* Sort intersections (insertion sort — small N) */
        for (int a = 1; a < num_ints; a++) {
            int16_t key = x_ints[a];
            int b = a - 1;
            while (b >= 0 && x_ints[b] > key) {
                x_ints[b + 1] = x_ints[b];
                b--;
            }
            x_ints[b + 1] = key;
        }

        /* Fill between pairs */
        for (int k = 0; k + 1 < num_ints; k += 2) {
            int16_t xl = x_ints[k];
            int16_t xr = x_ints[k + 1];
            if (xl < g_clip_x0) xl = g_clip_x0;
            if (xr > g_clip_x1) xr = g_clip_x1;
            if (xl <= xr)
                fill_span(xl, y, xr - xl + 1);
        }
    }
    #undef MAX_POLY_INTERSECTIONS
}

/* ══════════════════════════════════════════════════════════════════
 * PIE SLICE — arc sector with two radii to center
 * ══════════════════════════════════════════════════════════════════ */

/* FPU-accelerated sin/cos. Returns values scaled by 1024 for
 * compatibility with circle_point/ellipse_point callers. */
static void sincos_deg(int16_t deg, int16_t *out_s, int16_t *out_c) {
    float rad = (float)deg * (M_PI / 180.0f);
    *out_s = (int16_t)(sinf(rad) * 1024.0f);
    *out_c = (int16_t)(cosf(rad) * 1024.0f);
}

/* Helper: compute point on circle at given angle (degrees) */
static void circle_point(int16_t cx, int16_t cy, int16_t r,
                          int16_t deg, int16_t *ox, int16_t *oy) {
    int16_t s, c;
    sincos_deg(deg, &s, &c);
    *ox = cx + (int16_t)((int32_t)r * c / 1024);
    *oy = cy - (int16_t)((int32_t)r * s / 1024);
}

void draw_pie(int16_t cx, int16_t cy, int16_t r,
              int16_t start_deg, int16_t end_deg, bool fill) {
    if (r <= 0) return;

    /* Draw arc */
    draw_arc(cx, cy, r, start_deg, end_deg);

    /* Draw two radii from center to arc endpoints */
    int16_t sx, sy, ex, ey;
    circle_point(cx, cy, r, start_deg, &sx, &sy);
    circle_point(cx, cy, r, end_deg, &ex, &ey);
    draw_line(cx, cy, sx, sy);
    draw_line(cx, cy, ex, ey);

    /* Scanline-based sector fill (replaces flood fill which leaks
     * through pixel gaps in the arc+radii boundary).
     * For each row in the bounding box, test each pixel: if it's
     * inside the circle AND its angle is in [start, end], fill it. */
    if (fill) {
        int32_t r2 = (int32_t)r * r;
        int16_t y0 = (cy - r < g_clip_y0) ? g_clip_y0 : cy - r;
        int16_t y1 = (cy + r > g_clip_y1) ? g_clip_y1 : cy + r;
        for (int16_t py = y0; py <= y1; py++) {
            int32_t dy = py - cy;
            int32_t dx_max2 = r2 - dy * dy;
            if (dx_max2 < 0) continue;
            /* Integer sqrt approximation */
            int16_t dx_max = (int16_t)sqrtf((float)dx_max2);
            int16_t x0 = (cx - dx_max < g_clip_x0) ? g_clip_x0 : cx - dx_max;
            int16_t x1 = (cx + dx_max > g_clip_x1) ? g_clip_x1 : cx + dx_max;
            for (int16_t px = x0; px <= x1; px++) {
                int16_t ddx = px - cx, ddy = py - cy;
                if ((int32_t)ddx * ddx + (int32_t)ddy * ddy > r2) continue;
                int16_t a = pixel_angle(ddx, -ddy);
                if (angle_in_range(a, start_deg, end_deg))
                    put_pixel(px, py);
            }
        }
        mark_dirty(y0, y1);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * ELLIPTICAL ARC — midpoint ellipse with angle filtering
 * ══════════════════════════════════════════════════════════════════ */

static void earc_pixel(int16_t cx, int16_t cy, int16_t dx, int16_t dy,
                        int16_t start, int16_t end) {
    if (angle_in_range(pixel_angle(dx, -dy), start, end))
        put_pixel(cx + dx, cy + dy);
}

void draw_elliptical_arc(int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                          int16_t start_deg, int16_t end_deg) {
    if (rx <= 0 || ry <= 0) return;

    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int16_t x = 0, y = ry;
    int32_t d1 = ry2 - rx2 * ry + rx2 / 4;

    /* Region 1 */
    while (ry2 * x < rx2 * y) {
        earc_pixel(cx, cy,  x,  y, start_deg, end_deg);
        earc_pixel(cx, cy, -x,  y, start_deg, end_deg);
        earc_pixel(cx, cy,  x, -y, start_deg, end_deg);
        earc_pixel(cx, cy, -x, -y, start_deg, end_deg);
        x++;
        if (d1 < 0) {
            d1 += 2 * ry2 * x + ry2;
        } else {
            y--;
            d1 += 2 * ry2 * x - 2 * rx2 * y + ry2;
        }
    }

    /* Region 2 */
    int32_t d2 = ry2 * ((int32_t)(2 * x + 1) * (2 * x + 1)) / 4
               + rx2 * ((int32_t)(y - 1) * (y - 1))
               - rx2 * ry2;
    while (y >= 0) {
        earc_pixel(cx, cy,  x,  y, start_deg, end_deg);
        earc_pixel(cx, cy, -x,  y, start_deg, end_deg);
        earc_pixel(cx, cy,  x, -y, start_deg, end_deg);
        earc_pixel(cx, cy, -x, -y, start_deg, end_deg);
        y--;
        if (d2 > 0) {
            d2 += rx2 - 2 * rx2 * y;
        } else {
            x++;
            d2 += 2 * ry2 * x - 2 * rx2 * y + rx2;
        }
    }
    mark_dirty((int16_t)(cy - ry), (int16_t)(cy + ry));
}

/* ══════════════════════════════════════════════════════════════════
 * ELLIPTICAL PIE — elliptical arc + two radii + optional fill
 *
 * Draws an elliptical sector: arc from start_deg to end_deg with
 * radii rx/ry, plus two lines from center to arc endpoints.
 * Uses FPU sincos_deg for endpoint computation.
 * ══════════════════════════════════════════════════════════════════ */

static void ellipse_point(int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                           int16_t deg, int16_t *ox, int16_t *oy) {
    int16_t s, c;
    sincos_deg(deg, &s, &c);
    *ox = cx + (int16_t)((int32_t)rx * c / 1024);
    *oy = cy - (int16_t)((int32_t)ry * s / 1024);
}

void draw_elliptical_pie(int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                          int16_t start_deg, int16_t end_deg, bool fill) {
    if (rx <= 0 || ry <= 0) return;

    /* Draw elliptical arc */
    draw_elliptical_arc(cx, cy, rx, ry, start_deg, end_deg);

    /* Draw two radii from center to arc endpoints */
    int16_t sx, sy, ex, ey;
    ellipse_point(cx, cy, rx, ry, start_deg, &sx, &sy);
    ellipse_point(cx, cy, rx, ry, end_deg, &ex, &ey);
    draw_line(cx, cy, sx, sy);
    draw_line(cx, cy, ex, ey);

    /* Scanline-based sector fill for elliptical pie. */
    if (fill) {
        int32_t rx2 = (int32_t)rx * rx;
        int32_t ry2 = (int32_t)ry * ry;
        int16_t y0 = (cy - ry < g_clip_y0) ? g_clip_y0 : cy - ry;
        int16_t y1 = (cy + ry > g_clip_y1) ? g_clip_y1 : cy + ry;
        for (int16_t py = y0; py <= y1; py++) {
            int32_t dy = py - cy;
            /* Ellipse equation: dx²/rx² + dy²/ry² <= 1
             * → dx² <= rx²(1 - dy²/ry²) = rx²(ry² - dy²)/ry² */
            int32_t num = rx2 * (ry2 - dy * dy);
            if (num < 0) continue;
            int32_t dx_max2 = num / ry2;
            int16_t dx_max = (int16_t)sqrtf((float)dx_max2);
            int16_t x0 = (cx - dx_max < g_clip_x0) ? g_clip_x0 : cx - dx_max;
            int16_t x1 = (cx + dx_max > g_clip_x1) ? g_clip_x1 : cx + dx_max;
            for (int16_t px = x0; px <= x1; px++) {
                int16_t ddx = px - cx, ddy = py - cy;
                /* Check inside ellipse */
                if ((int32_t)ddx * ddx * ry2 + (int32_t)ddy * ddy * rx2 > rx2 * ry2)
                    continue;
                int16_t a = pixel_angle(ddx, -ddy);
                if (angle_in_range(a, start_deg, end_deg))
                    put_pixel(px, py);
            }
        }
        mark_dirty(y0, y1);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * THICK LINE — perpendicular brush stamp
 *
 * For thickness N, draw N parallel lines offset perpendicular to
 * the line direction by ±N/2. Uses integer perpendicular offset.
 * ══════════════════════════════════════════════════════════════════ */

void draw_thick_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    if (g_line_thickness <= 1) {
        draw_line(x0, y0, x1, y1);
        return;
    }

    int16_t dx = x1 - x0, dy = y1 - y0;
    int16_t t = g_line_thickness;
    int16_t half = t / 2;

    if (dx == 0) {
        /* Vertical: thicken horizontally */
        for (int16_t i = -half; i < t - half; i++)
            draw_line(x0 + i, y0, x1 + i, y1);
    } else if (dy == 0) {
        /* Horizontal: thicken vertically */
        for (int16_t i = -half; i < t - half; i++)
            draw_line(x0, y0 + i, x1, y1 + i);
    } else {
        /* Diagonal: offset perpendicular. For simplicity, approximate
         * by offsetting in the shorter axis direction. */
        int16_t ax = abs(dx), ay = abs(dy);
        if (ax >= ay) {
            /* More horizontal: offset in Y */
            for (int16_t i = -half; i < t - half; i++)
                draw_line(x0, y0 + i, x1, y1 + i);
        } else {
            /* More vertical: offset in X */
            for (int16_t i = -half; i < t - half; i++)
                draw_line(x0 + i, y0, x1 + i, y1);
        }
    }
}
