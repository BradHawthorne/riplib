/*
 * ripscrip.c — RIPscrip 1.54 graphics protocol parser for A2GSPU card
 *
 * TeleGrafix RIPscrip (1992-1994). Vector graphics protocol for BBSes.
 * !| prefix, | terminator, base-36 MegaNum parameters, EGA 640×350
 * coordinates scaled to 640×400. Highest "wow factor" protocol — full
 * color vector art, buttons, mouse regions, text windows.
 *
 * Reference: TeleGrafix RIPscrip 1.54 specification (RIPSCRIP.DOC)
 * Reference: RIPtermJS (JavaScript implementation)
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License. See LICENSE.
 */

#include "ripscrip.h"
#include "ripscrip2.h"
#include "riplib_platform.h"
#include "drawing.h"
#include "bgi_font.h"
#include "font_bgi_trip.h"
#include "font_bgi_sans.h"
#include "font_bgi_goth.h"
#include "font_bgi_litt.h"
#include "font_bgi_scri.h"
#include "font_bgi_simp.h"
#include "font_bgi_tscr.h"
#include "font_bgi_lcom.h"
#include "font_bgi_euro.h"
#include "font_bgi_bold.h"
#include <stdlib.h> /* atoi() — used by eval_if_expr() for numeric comparisons */
#include <string.h>
#include <stdio.h>
#include <time.h>  /* time_t, time(), localtime() — RTC fallback for $DATE$/$TIME$ */

#include "rip_icons.h"
#include "rip_icn.h"
/* rip_raf: stubbed in riplib */
#include "font_cp437_8x16.h"

/* Write/read RIPscrip palette entries to/from hardware (FORMAT_8_PAL LUT) */
extern void palette_write_rgb565(uint8_t index, uint16_t rgb565);
extern uint16_t palette_read_rgb565(uint8_t index);

/* PSRAM arena allocator for session-scoped allocations */
#include "riplib_platform.h"

/* Arena size for a single RIPscrip session: 1 MB covers the clipboard
 * (640×400 = 256 KB), uploaded icon cache, and file staging buffer. */
#define RIP_PSRAM_ARENA_SIZE (1024u * 1024u)

/* Reset the per-frame command-level prefix flags.  Used by the FSM at
 * every dispatch boundary (CR/LF, '|', error recovery) to start the
 * next command in a clean Level 0 state. */
static inline void clear_levels(rip_state_t *s) {
    s->is_level1 = false;
    s->is_level2 = false;
    s->is_level3 = false;
}

/* L15: write both s->vp_* and ports[active_port].vp_*.  The port table
 * is the source of truth on port switch (port_load_state copies from
 * the port back into rip_state_t), so viewport-setting commands ('v',
 * '1V', '*') must touch both — otherwise a subsequent !|2s revert
 * silently undoes the new viewport.  Also pushes the clip to the draw
 * layer so subsequent primitives respect it immediately. */
static inline void set_session_viewport(rip_state_t *s,
                                        int16_t x0, int16_t y0,
                                        int16_t x1, int16_t y1) {
    s->vp_x0 = x0;
    s->vp_y0 = y0;
    s->vp_x1 = x1;
    s->vp_y1 = y1;
    if (s->active_port < RIP_MAX_PORTS) {
        s->ports[s->active_port].vp_x0 = x0;
        s->ports[s->active_port].vp_y0 = y0;
        s->ports[s->active_port].vp_x1 = x1;
        s->ports[s->active_port].vp_y1 = y1;
    }
    draw_set_clip(x0, y0, x1, y1);
}

/* BGI stroke fonts (parsed at init, indexed by BGI_FONT_* ID) */
#define BGI_FONT_COUNT 11  /* 0=bitmap, 1-10=stroke */
static bgi_font_t bgi_fonts[BGI_FONT_COUNT];
static bool bgi_fonts_loaded = false;

/* Backward compat alias */
#define bgi_triplex         bgi_fonts[BGI_FONT_TRIPLEX]
#define bgi_triplex_loaded  bgi_fonts_loaded

/* Global parser state (one RIPscrip session at a time) */
static rip_state_t *g_rip_state = NULL;

static void apply_session_draw_state(rip_state_t *s);
static void rip_upload_reset(rip_state_t *s);
static void rip_cache_icn_if_valid(rip_state_t *s, const char *name, int name_len,
                                   const uint8_t *data, int size);
static void rip_reset_windows_state(rip_state_t *s, comp_context_t *c);

/* Card->host TX FIFO helper (implemented in main.c / emulator stubs). */
extern void card_tx_push(const char *buf, int len);

/* FILE UPLOAD — receive BMP/ICN data from host for PSRAM caching */
#define FILE_UPLOAD_MAX  (128 * 1024)  /* 128KB max per file */

/* ══════════════════════════════════════════════════════════════════
 * MEGANUM DECODER — base-36 parameter encoding
 * ══════════════════════════════════════════════════════════════════ */

static int mega_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    return 0;
}

static int16_t mega2(const char *p) {
    return (int16_t)(mega_digit(p[0]) * 36 + mega_digit(p[1]));
}

static int32_t mega3(const char *p) {
    return (int32_t)(mega_digit(p[0]) * 1296 +
                     mega_digit(p[1]) * 36 +
                     mega_digit(p[2]));
}

static int32_t mega4(const char *p) {
    return (int32_t)(mega_digit(p[0]) * 46656 + mega_digit(p[1]) * 1296 +
                     mega_digit(p[2]) * 36 + mega_digit(p[3]));
}

static size_t rip_strnlen(const char *s, size_t max_len) {
    size_t n = 0;
    if (!s)
        return 0;
    while (n < max_len && s[n] != '\0')
        n++;
    return n;
}

/* Scale RIPscrip Y (640×350) to card Y (640×400).
 * Two variants prevent gaps between adjacent rectangles:
 * scale_y  = floor (for top edges, y-positions, single coords)
 * scale_y1 = ceiling (for bottom edges — ensures adjacent rects touch) */
static int16_t scale_y(int16_t y) {
    return (int16_t)((y * 8) / 7);
}
static int16_t scale_y1(int16_t y) {
    return (int16_t)((y * 8 + 6) / 7);
}

static void clamp_ega_rect(int16_t *x0, int16_t *y0,
                           int16_t *x1, int16_t *y1) {
    int16_t tx0 = *x0;
    int16_t ty0 = *y0;
    int16_t tx1 = *x1;
    int16_t ty1 = *y1;

    if (tx0 > tx1) { int16_t t = tx0; tx0 = tx1; tx1 = t; }
    if (ty0 > ty1) { int16_t t = ty0; ty0 = ty1; ty1 = t; }

    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > 639) tx1 = 639;
    if (ty1 > 349) ty1 = 349;

    *x0 = tx0;
    *y0 = ty0;
    *x1 = tx1;
    *y1 = ty1;
}

static bool rip_filename_is_safe(const char *name, int len) {
    if (!name || len <= 0)
        return false;

    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20u || c == 0x7Fu)
            return false;
        if (c == '/' || c == '\\' || c == ':')
            return false;
        if (c == '.' && i + 1 < len && name[i + 1] == '.')
            return false;
    }

    return true;
}

static bool rip_var_name_copy(const char *name, int len,
                              char *out, int out_cap) {
    int start = 0;
    int end = len;
    int n;

    if (!name || !out || out_cap <= 0 || len <= 0)
        return false;
    while (start < end && (name[start] == ' ' || name[start] == '\t'))
        start++;
    while (end > start && (name[end - 1] == ' ' || name[end - 1] == '\t'))
        end--;
    if (end - start >= 2 && name[start] == '$' && name[end - 1] == '$') {
        start++;
        end--;
    }
    n = end - start;
    if (n <= 0 || n >= out_cap)
        return false;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[start + i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_'))
            return false;
        out[i] = (char)c;
    }
    out[n] = '\0';
    return true;
}

static bool rip_var_name_eq(const char *stored, const char *name, int len) {
    int slen;

    if (!stored || !name || len <= 0)
        return false;
    slen = (int)strlen(stored);
    if (slen != len)
        return false;
    return memcmp(stored, name, (size_t)len) == 0;
}

static int rip_user_var_find(const rip_state_t *s, const char *name, int len) {
    if (!s || !name || len <= 0)
        return -1;
    for (int i = 0; i < s->user_var_count && i < RIP_USER_VAR_MAX; i++) {
        if (rip_var_name_eq(s->user_var_names[i], name, len))
            return i;
    }
    return -1;
}

static bool rip_user_var_set(rip_state_t *s,
                             const char *name, int name_len,
                             const char *value, int value_len) {
    char key[RIP_USER_VAR_NAME_MAX + 1];
    int idx;
    int copy;

    if (!s || !value || value_len < 0)
        return false;
    if (!rip_var_name_copy(name, name_len, key, sizeof(key)))
        return false;
    if (rip_var_name_eq(key, "APP0", 4) ||
        rip_var_name_eq(key, "APP1", 4) ||
        rip_var_name_eq(key, "APP2", 4) ||
        rip_var_name_eq(key, "APP3", 4) ||
        rip_var_name_eq(key, "APP4", 4) ||
        rip_var_name_eq(key, "APP5", 4) ||
        rip_var_name_eq(key, "APP6", 4) ||
        rip_var_name_eq(key, "APP7", 4) ||
        rip_var_name_eq(key, "APP8", 4) ||
        rip_var_name_eq(key, "APP9", 4)) {
        int app = key[3] - '0';
        copy = value_len < (int)sizeof(s->app_vars[0]) - 1
             ? value_len
             : (int)sizeof(s->app_vars[0]) - 1;
        memcpy(s->app_vars[app], value, (size_t)copy);
        s->app_vars[app][copy] = '\0';
        return true;
    }

    idx = rip_user_var_find(s, key, (int)strlen(key));
    if (idx < 0) {
        if (s->user_var_count >= RIP_USER_VAR_MAX)
            return false;
        idx = s->user_var_count++;
        strcpy(s->user_var_names[idx], key);
    }
    copy = value_len < RIP_USER_VAR_VALUE_MAX ? value_len : RIP_USER_VAR_VALUE_MAX;
    memcpy(s->user_var_values[idx], value, (size_t)copy);
    s->user_var_values[idx][copy] = '\0';
    return true;
}

static bool rip_query_prompt_begin(rip_state_t *s,
                                   const char *vname, int vlen) {
    char prompt_buf[40];
    int plen = 0;
    int copy;

    if (!s || !vname || vlen <= 0 || s->query_pending)
        return false;

    prompt_buf[plen++] = (char)0x3E;  /* CMD_QUERY_PROMPT marker */
    copy = vlen < (int)sizeof(prompt_buf) - 2
         ? vlen
         : (int)sizeof(prompt_buf) - 2;
    for (int k = 0; k < copy; k++)
        prompt_buf[plen++] = vname[k];
    prompt_buf[plen++] = '\0';
    card_tx_push(prompt_buf, plen);

    copy = vlen < (int)sizeof(s->query_var_name) - 1
         ? vlen
         : (int)sizeof(s->query_var_name) - 1;
    memcpy(s->query_var_name, vname, (size_t)copy);
    s->query_var_name[copy] = '\0';
    memset(s->query_response, 0, sizeof(s->query_response));
    s->query_response_len = 0;
    s->query_pending = true;
    return true;
}

static bool rip_parse_host_date(const char *date, int *year, int *month, int *day) {
    int mm;
    int dd;
    int yy;

    if (!date || !year || !month || !day)
        return false;
    if (!(date[0] >= '0' && date[0] <= '9' &&
          date[1] >= '0' && date[1] <= '9' &&
          date[2] == '/' &&
          date[3] >= '0' && date[3] <= '9' &&
          date[4] >= '0' && date[4] <= '9' &&
          date[5] == '/' &&
          date[6] >= '0' && date[6] <= '9' &&
          date[7] >= '0' && date[7] <= '9'))
        return false;
    mm = (date[0] - '0') * 10 + (date[1] - '0');
    dd = (date[3] - '0') * 10 + (date[4] - '0');
    yy = (date[6] - '0') * 10 + (date[7] - '0');
    if (mm < 1 || mm > 12 || dd < 1 || dd > 31)
        return false;
    *year = 2000 + yy;
    *month = mm;
    *day = dd;
    return true;
}

static bool rip_is_leap_year(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || (year % 400) == 0;
}

static int rip_day_of_year(int year, int month, int day) {
    static const int month_offsets[12] =
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int yday = month_offsets[month - 1] + day;
    if (month > 2 && rip_is_leap_year(year))
        yday++;
    return yday;
}

static int rip_days_from_civil(int year, int month, int day) {
    int y = year;
    unsigned m = (unsigned)month;
    unsigned d = (unsigned)day;
    unsigned mp;
    int era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;

    y -= (m <= 2u);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    mp = (m > 2u) ? (m - 3u) : (m + 9u);
    doy = (153u * mp + 2u) / 5u + d - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int)doe - 719468;
}

static int rip_weekday_monday0(int year, int month, int day) {
    int days = rip_days_from_civil(year, month, day);
    int dow = (days + 3) % 7; /* 1970-01-01 was Thursday. */
    if (dow < 0)
        dow += 7;
    return dow;
}

static int rip_iso_weeks_in_year(int year) {
    int jan1 = rip_weekday_monday0(year, 1, 1);
    return (jan1 == 3 || (jan1 == 2 && rip_is_leap_year(year))) ? 53 : 52;
}

static int rip_iso_week(int year, int month, int day) {
    int doy = rip_day_of_year(year, month, day);
    int dow = rip_weekday_monday0(year, month, day) + 1; /* Monday=1 */
    int week = (doy - dow + 10) / 7;

    if (week < 1)
        return rip_iso_weeks_in_year(year - 1);
    if (week > rip_iso_weeks_in_year(year))
        return 1;
    return week;
}

static void rip_upload_reset(rip_state_t *s) {
    if (!s) return;
    s->upload_pos = 0;
    s->upload_name[0] = '\0';
    s->upload_name_len = 0;
    s->upload_name_remaining = 0;
    s->upload_name_overflow = false;
    s->upload_reading_name = false;
}

static void rip_cache_icn_if_valid(rip_state_t *s, const char *name, int name_len,
                                   const uint8_t *data, int size) {
    uint16_t icn_w;
    uint16_t icn_h;
    size_t pixel_count;
    uint8_t *pixels;

    if (!s || !data || !rip_filename_is_safe(name, name_len))
        return;
    if (!rip_icn_measure(data, size, &icn_w, &icn_h))
        return;

    pixel_count = (size_t)icn_w * (size_t)icn_h;
    if (pixel_count == 0 || pixel_count > RIP_CLIPBOARD_MAX)
        return;
    if (rip_icon_cache_count(&s->icon_state) >= RIP_ICON_CACHE_MAX &&
        !rip_icon_cache_has_runtime(&s->icon_state, name, name_len))
        return;

    pixels = (uint8_t *)psram_arena_alloc(&s->psram_arena, (uint32_t)pixel_count);
    if (!pixels)
        return;

    if (!rip_icn_parse(data, size, pixels, &icn_w, &icn_h))
        return;

    (void)rip_icon_cache_pixels_replace(&s->icon_state, name, name_len,
                                         pixels, icn_w, icn_h);
}

static uint8_t palette_slot(int idx) {
    return (uint8_t)(240 + idx);
}

static bool rip_clipboard_alloc(rip_state_t *s) {
    if (!s)
        return false;
    if (!s->clipboard.data) {
        s->clipboard.data = (uint8_t *)psram_arena_alloc(&s->psram_arena,
                                                         RIP_CLIPBOARD_MAX);
    }
    return s->clipboard.data != NULL;
}

static bool rip_clipboard_store_pixels(rip_state_t *s,
                                       const uint8_t *pixels,
                                       uint16_t width,
                                       uint16_t height) {
    size_t bytes;

    if (!s || !pixels || width == 0 || height == 0)
        return false;

    bytes = (size_t)width * (size_t)height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;
    if (!rip_clipboard_alloc(s))
        return false;

    memmove(s->clipboard.data, pixels, bytes);
    s->clipboard.width = (int16_t)width;
    s->clipboard.height = (int16_t)height;
    s->clipboard.valid = true;
    return true;
}

static bool rip_clipboard_capture(rip_state_t *s,
                                  int16_t x, int16_t y,
                                  int16_t width, int16_t height) {
    size_t bytes;

    if (!s || width <= 0 || height <= 0)
        return false;

    bytes = (size_t)(uint16_t)width * (size_t)(uint16_t)height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;
    if (!rip_clipboard_alloc(s))
        return false;

    draw_save_region(x, y, width, height, s->clipboard.data);
    s->clipboard.width = width;
    s->clipboard.height = height;
    s->clipboard.valid = true;
    return true;
}

static bool rip_cache_clipboard_as_icon(rip_state_t *s,
                                        const char *name,
                                        int name_len,
                                        rip_icon_t *out_icon) {
    size_t bytes;
    uint8_t *pixels;

    if (!s || !s->clipboard.valid || !s->clipboard.data ||
        !rip_filename_is_safe(name, name_len))
        return false;

    bytes = (size_t)(uint16_t)s->clipboard.width *
            (size_t)(uint16_t)s->clipboard.height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;

    pixels = (uint8_t *)psram_arena_alloc(&s->psram_arena, (uint32_t)bytes);
    if (!pixels)
        return false;

    memcpy(pixels, s->clipboard.data, bytes);
    if (!rip_icon_cache_pixels_replace(&s->icon_state, name, name_len, pixels,
                                       (uint16_t)s->clipboard.width,
                                       (uint16_t)s->clipboard.height))
        return false;

    if (out_icon) {
        out_icon->pixels = pixels;
        out_icon->width = (uint16_t)s->clipboard.width;
        out_icon->height = (uint16_t)s->clipboard.height;
    }
    return true;
}

static bool rip_save_clipboard_slot(rip_state_t *s, uint16_t slot) {
    size_t bytes;
    uint8_t *pixels;
    char slot_name[RIP_ICON_NAME_MAX + 1];

    if (!s || slot >= RIP_ICON_SLOT_MAX ||
        !s->clipboard.valid || !s->clipboard.data)
        return false;

    bytes = (size_t)(uint16_t)s->clipboard.width *
            (size_t)(uint16_t)s->clipboard.height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;

    pixels = (uint8_t *)psram_arena_alloc(&s->psram_arena, (uint32_t)bytes);
    if (!pixels)
        return false;
    memcpy(pixels, s->clipboard.data, bytes);

    s->icon_slots[slot].pixels = pixels;
    s->icon_slots[slot].width = (uint16_t)s->clipboard.width;
    s->icon_slots[slot].height = (uint16_t)s->clipboard.height;
    s->icon_slot_valid[slot] = true;

    snprintf(slot_name, sizeof(slot_name), "SLOT%02u", (unsigned)slot);
    (void)rip_icon_cache_pixels_replace(&s->icon_state, slot_name,
                                        (int)strlen(slot_name),
                                        pixels,
                                        (uint16_t)s->clipboard.width,
                                        (uint16_t)s->clipboard.height);
    return true;
}

static void rip_blit_pixels(rip_state_t *s,
                            int16_t dx, int16_t dy,
                            const uint8_t *pixels,
                            uint16_t src_w, uint16_t src_h,
                            int16_t dst_w, int16_t dst_h,
                            uint8_t write_mode) {
    uint8_t old_color;

    if (!pixels || src_w == 0 || src_h == 0 || dst_w <= 0 || dst_h <= 0)
        return;
    if (write_mode > DRAW_MODE_NOT)
        write_mode = DRAW_MODE_COPY;

    old_color = draw_get_color();
    draw_set_write_mode(write_mode);

    if (dst_w == (int16_t)src_w && dst_h == (int16_t)src_h) {
        draw_restore_region(dx, dy, dst_w, dst_h, pixels);
    } else {
        for (int16_t yy = 0; yy < dst_h; yy++) {
            uint16_t sy = (uint16_t)(((uint32_t)(uint16_t)yy * src_h) /
                                     (uint16_t)dst_h);
            for (int16_t xx = 0; xx < dst_w; xx++) {
                uint16_t sx = (uint16_t)(((uint32_t)(uint16_t)xx * src_w) /
                                         (uint16_t)dst_w);
                draw_set_color(pixels[(size_t)sy * src_w + sx]);
                draw_pixel((int16_t)(dx + xx), (int16_t)(dy + yy));
            }
        }
    }

    draw_set_write_mode(s ? s->write_mode : DRAW_MODE_COPY);
    draw_set_color(old_color);
}

static void rip_blit_pixels_tiled(rip_state_t *s,
                                  int16_t x0, int16_t y0,
                                  int16_t x1, int16_t y1,
                                  const uint8_t *pixels,
                                  uint16_t src_w, uint16_t src_h,
                                  uint8_t write_mode) {
    draw_clip_state_t saved_clip;

    if (!pixels || src_w == 0 || src_h == 0 || x1 < x0 || y1 < y0)
        return;

    draw_save_clip(&saved_clip);
    draw_set_clip(x0, y0, x1, y1);
    for (int16_t y = y0; y <= y1; y = (int16_t)(y + src_h)) {
        for (int16_t x = x0; x <= x1; x = (int16_t)(x + src_w)) {
            rip_blit_pixels(s, x, y, pixels, src_w, src_h,
                            (int16_t)src_w, (int16_t)src_h, write_mode);
            if (src_w == 0)
                break;
        }
        if (src_h == 0)
            break;
    }
    draw_restore_clip(&saved_clip);
}

static void rip_draw_icon_pixels(rip_state_t *s,
                                 int16_t x, int16_t y,
                                 const uint8_t *pixels,
                                 uint16_t src_w, uint16_t src_h,
                                 int16_t requested_w,
                                 int16_t requested_h,
                                 uint8_t write_mode) {
    bool has_box;
    int16_t bx0;
    int16_t by0;
    int16_t bx1;
    int16_t by1;
    int16_t dst_w;
    int16_t dst_h;
    uint8_t mode;

    if (!s || !pixels || src_w == 0 || src_h == 0)
        return;

    has_box = (requested_w > 0 && requested_h > 0);
    if (has_box) {
        bx0 = x;
        by0 = y;
        bx1 = (int16_t)(x + requested_w - 1);
        by1 = (int16_t)(y + requested_h - 1);
    } else if (s->icon_style_active) {
        bx0 = s->icon_style_x0;
        by0 = s->icon_style_y0;
        bx1 = s->icon_style_x1;
        by1 = s->icon_style_y1;
    } else {
        rip_blit_pixels(s, x, y, pixels, src_w, src_h,
                        (int16_t)src_w, (int16_t)src_h, write_mode);
        return;
    }

    if (bx0 > bx1) { int16_t t = bx0; bx0 = bx1; bx1 = t; }
    if (by0 > by1) { int16_t t = by0; by0 = by1; by1 = t; }
    dst_w = (int16_t)(bx1 - bx0 + 1);
    dst_h = (int16_t)(by1 - by0 + 1);
    if (dst_w <= 0 || dst_h <= 0)
        return;

    mode = s->icon_style_active ? (uint8_t)(s->icon_style_style & 0x03u)
                                : (uint8_t)(s->image_style & 0x03u);

    if (mode == 1) {
        rip_blit_pixels_tiled(s, bx0, by0, bx1, by1, pixels, src_w, src_h,
                              write_mode);
        return;
    }

    if (mode == 2) {
        dst_w = (int16_t)src_w;
        dst_h = (int16_t)src_h;
        if ((s->icon_style_align & 0x03u) == 2u) {
            bx0 = (int16_t)(bx1 - dst_w + 1);
            by0 = (int16_t)(by1 - dst_h + 1);
        } else {
            bx0 = (int16_t)(bx0 + ((bx1 - bx0 + 1) - dst_w) / 2);
            by0 = (int16_t)(by0 + ((by1 - by0 + 1) - dst_h) / 2);
        }
    } else if (mode == 3) {
        int32_t w_fit = (int32_t)(bx1 - bx0 + 1);
        int32_t h_fit = ((int32_t)w_fit * src_h) / src_w;
        if (h_fit > (int32_t)(by1 - by0 + 1)) {
            h_fit = (int32_t)(by1 - by0 + 1);
            w_fit = ((int32_t)h_fit * src_w) / src_h;
        }
        if (w_fit <= 0) w_fit = 1;
        if (h_fit <= 0) h_fit = 1;
        dst_w = (int16_t)w_fit;
        dst_h = (int16_t)h_fit;
        bx0 = (int16_t)(bx0 + ((bx1 - bx0 + 1) - dst_w) / 2);
        by0 = (int16_t)(by0 + ((by1 - by0 + 1) - dst_h) / 2);
    }

    rip_blit_pixels(s, bx0, by0, pixels, src_w, src_h, dst_w, dst_h,
                    write_mode);
}

static void rip_copy_screen_region_scaled(rip_state_t *s,
                                          int16_t sx, int16_t sy,
                                          int16_t sw, int16_t sh,
                                          int16_t dx, int16_t dy,
                                          int16_t dw, int16_t dh,
                                          uint8_t write_mode) {
    size_t bytes;
    uint8_t *scratch;

    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    if (write_mode > DRAW_MODE_NOT)
        write_mode = DRAW_MODE_COPY;

    if (sw == dw && sh == dh && write_mode == DRAW_MODE_COPY) {
        draw_copy_rect(sx, sy, dx, dy, sw, sh);
        return;
    }

    bytes = (size_t)(uint16_t)sw * (size_t)(uint16_t)sh;
    if (bytes == 0)
        return;

    scratch = (uint8_t *)malloc(bytes);
    if (!scratch)
        return;
    draw_save_region(sx, sy, sw, sh, scratch);
    rip_blit_pixels(s, dx, dy, scratch, (uint16_t)sw, (uint16_t)sh,
                    dw, dh, write_mode);
    free(scratch);
}

static int rip_font_id_from_name(const char *name, int len) {
    static const struct {
        const char *tag;
        int id;
    } fonts[] = {
        { "TRIP", BGI_FONT_TRIPLEX },
        { "LITT", BGI_FONT_SMALL },
        { "SANS", BGI_FONT_SANS },
        { "GOTH", BGI_FONT_GOTHIC },
        { "SCRI", BGI_FONT_SCRIPT },
        { "SIMP", BGI_FONT_SIMPLEX },
        { "TSCR", BGI_FONT_TRIPLEX_SCR },
        { "LCOM", BGI_FONT_COMPLEX },
        { "EURO", BGI_FONT_EUROPEAN },
        { "BOLD", BGI_FONT_BOLD },
    };
    int start = 0;
    int end = len;

    if (!name || len <= 0)
        return -1;
    for (int i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':')
            start = i + 1;
    }
    for (int i = start; i < len; i++) {
        if (name[i] == '.') {
            end = i;
            break;
        }
    }

    for (size_t fi = 0; fi < sizeof(fonts) / sizeof(fonts[0]); fi++) {
        const char *tag = fonts[fi].tag;
        size_t tag_len = strlen(tag);
        if ((size_t)(end - start) != tag_len)
            continue;
        bool match = true;
        for (size_t j = 0; j < tag_len; j++) {
            char c = name[start + (int)j];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            if (c != tag[j]) {
                match = false;
                break;
            }
        }
        if (match)
            return fonts[fi].id;
    }
    return -1;
}

/* ══════════════════════════════════════════════════════════════════
 * DEFAULT EGA PALETTE → RGB565
 * ══════════════════════════════════════════════════════════════════ */

/* Default 16-color EGA palette as RGB565 (for draw color mapping) */
static const uint16_t ega_default_rgb565[16] = {
    0x0000, /* 0:  black */
    0x0015, /* 1:  blue */
    0x0540, /* 2:  green */
    0x0555, /* 3:  cyan */
    0xA800, /* 4:  red */
    0xA815, /* 5:  magenta */
    0xAAA0, /* 6:  brown */
    0xAD55, /* 7:  light gray */
    0x52AA, /* 8:  dark gray */
    0x52BF, /* 9:  light blue */
    0x57EA, /* 10: light green */
    0x57FF, /* 11: light cyan */
    0xFAAA, /* 12: light red */
    0xFABF, /* 13: light magenta */
    0xFFEA, /* 14: yellow */
    0xFFFF, /* 15: white */
};

/* EGA 64-color master palette → RGB565 conversion.
 * EGA 6-bit format: bits 5:0 = r'g'b'RGB where RGB are high intensity,
 * r'g'b' are secondary (2/3 intensity). Each channel is 2-bit:
 *   channel = (high_bit << 1) | low_bit → 0,1,2,3 → {0x00, 0x55, 0xAA, 0xFF} */
static uint16_t ega64_to_rgb565(uint8_t ega) {
    /* Bit layout: bit5=r' bit4=g' bit3=b' bit2=R bit1=G bit0=B */
    uint8_t R = ((ega >> 2) & 1) << 1 | ((ega >> 5) & 1);
    uint8_t G = ((ega >> 1) & 1) << 1 | ((ega >> 4) & 1);
    uint8_t B = ((ega >> 0) & 1) << 1 | ((ega >> 3) & 1);
    /* 2-bit to 8-bit: 0→0, 1→0x55, 2→0xAA, 3→0xFF */
    static const uint8_t lut[4] = {0x00, 0x55, 0xAA, 0xFF};
    uint8_t r8 = lut[R], g8 = lut[G], b8 = lut[B];
    return ((uint16_t)(r8 >> 3) << 11) |
           ((uint16_t)(g8 >> 2) << 5) |
           ((uint16_t)(b8 >> 3));
}

/* ══════════════════════════════════════════════════════════════════
 * INITIALIZATION
 * ══════════════════════════════════════════════════════════════════ */

/* Save current hardware palette indices 240-255 into rip_state_t.
 * Called by the compositor's ripscrip_deactivate() before switching away so
 * that BBS-customized colors (written by RIP_SET_PALETTE / RIP_ONE_PALETTE)
 * are not lost when xterm-256 reloads its own palette on the next protocol. */
void rip_save_palette(rip_state_t *s) {
    if (!s) return;
    for (int i = 0; i < 16; i++)
        s->saved_palette_rgb565[i] = palette_read_rgb565(palette_slot(i));
}

/* Re-apply EGA palette to hardware — called when the EMU's xterm palette
 * overwrites entries 240-255 after a mode switch back to RIPscrip.
 * Uses saved_palette_rgb565 if a snapshot exists (non-zero); otherwise
 * falls back to EGA defaults so a fresh session still has correct colors. */
void rip_apply_palette(void) {
    if (!g_rip_state) return;
    /* Check whether a non-default snapshot exists. A BBS that called
     * RIP_SET_PALETTE will have written at least one non-default value; if
     * saved_palette_rgb565 is entirely zero the snapshot has never been taken
     * (new session) and we restore EGA defaults instead. */
    bool has_snapshot = false;
    for (int i = 0; i < 16; i++) {
        if (g_rip_state->saved_palette_rgb565[i] != 0) {
            has_snapshot = true;
            break;
        }
    }
    if (has_snapshot) {
        for (int i = 0; i < 16; i++)
            palette_write_rgb565(palette_slot(i), g_rip_state->saved_palette_rgb565[i]);
    } else {
        for (int i = 0; i < 16; i++)
            palette_write_rgb565(palette_slot(i), ega_default_rgb565[i]);
    }
}

/* Codex FIX 1: Boot-time init — call ONCE at power-on (or on first use).
 * Performs the full memset, arena reservation, drawing defaults, and BGI
 * font parse.  Calling this on a mid-session protocol switch would wipe
 * session state (clipboard, mouse regions, text variables, PSRAM arena).
 * For protocol switches, call rip_activate() instead.
 *
 * IMPORTANT API CONTRACT: the caller MUST zero `s` before the very first
 * call (e.g. via memset or by using a static-storage instance).  The
 * function reads s->psram_arena to decide whether an arena is already
 * reserved — if the struct contains uninitialized stack garbage, that
 * read is UB and the arena allocation may be skipped.  All callers in
 * this tree (test fixtures, demo, embedded boot) honor this contract. */
void rip_init_first(rip_state_t *s) {
    psram_arena_t saved_arena;

    if (!s) return;

    /* Snapshot the arena state.  After memset we will restore it so a
     * second rip_init_first() call (mid-session re-init) does not leak
     * the previously allocated PSRAM block.  On the first call, the
     * caller-zeroed struct has saved_arena.base == NULL which is the
     * "no arena yet" signal that drives psram_arena_init() below. */
    saved_arena = s->psram_arena;
    memset(s, 0, sizeof(*s));
    s->psram_arena = saved_arena;

    /* Allocate arena on first init.  Treat (base==NULL || size==0) as
     * "needs alloc" — covers both fresh zero-init and prior failed
     * allocation states.  A previously-successful arena has base!=NULL
     * and size>0, so we preserve it across re-init. */
    if (s->psram_arena.base == NULL || s->psram_arena.size == 0)
        psram_arena_init(&s->psram_arena, RIP_PSRAM_ARENA_SIZE);

    /* Bind the icon module to this arena.  Cache is cleared because we just
     * memset'd — all PSRAM pixel pointers from a previous session are gone. */
    rip_icon_set_arena(&s->icon_state, &s->psram_arena);

    g_rip_state = s;
    s->draw_color = 15; /* white */
    s->back_color = 0;  /* Fix Q2: background color — default black (DLL GFXSTYLE+0x02) */
    s->line_thick = 1;
    s->fill_pattern = 1; /* solid */
    s->fill_color = 15; /* Fix B4: default fill_color is white (15), not black (rip_defaults.c:113) */
    s->font_size = 1;
    s->tw_x1 = 639;
    s->tw_y1 = 349;
    s->vp_x0 = 0; s->vp_y0 = 0;
    s->vp_x1 = 639; s->vp_y1 = 399;

    /* Default palette: map EGA indices 0-15 to framebuffer values 240-255.
     * This avoids conflicting with xterm-256 colors at indices 0-239.
     * RIP draw commands write framebuffer value s->palette[color], and the
     * display converts via emu->palette[240+i] → EGA RGB565. */
    for (int i = 0; i < 16; i++) {
        s->palette[i] = palette_slot(i);
        palette_write_rgb565(palette_slot(i), ega_default_rgb565[i]);
    }

    /* Scene/protocol mode defaults.  Rendering is always to the fixed indexed
     * framebuffer, but the v2.x commands and variables observe this state. */
    s->header_type = 0;
    s->header_id = 0;
    s->header_flags = 0;
    s->resolution_mode = 0;
    s->coordinate_size = 2;
    s->coordinate_res = 0;
    s->color_mode = 0;
    s->color_bits = 0;
    s->filled_borders_enabled = true;

    /* A2GSPU v3.1: application variables and overflow pagination */
    memset(s->app_vars, 0, sizeof(s->app_vars));

    /* FIX TX1: Seed the $RAND$ LCG from the RP2350 RTC timestamp.
     * Using time() gives a different sequence per session (good enough for BBS
     * use).  The memset above zeroed rand_state; a zero seed is valid for the
     * Knuth LCG — first output will be 12345 — but we prefer time entropy. */
    s->rand_state = (uint32_t)time(NULL);

    /* FIX TX2: refresh_suppress starts false (normal refresh enabled). */
    s->refresh_suppress = false;

    ripscrip2_init(&s->rip2_state);

    /* Parse all BGI stroke fonts (flash data, parsed once at boot). */
    if (!bgi_fonts_loaded) {
        bgi_font_parse(&bgi_fonts[BGI_FONT_TRIPLEX], bgi_font_trip, bgi_font_trip_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_SMALL],   bgi_font_litt, bgi_font_litt_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_SANS],    bgi_font_sans, bgi_font_sans_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_GOTHIC],  bgi_font_goth, bgi_font_goth_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_SCRIPT],  bgi_font_scri, bgi_font_scri_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_SIMPLEX],    bgi_font_simp, bgi_font_simp_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_TRIPLEX_SCR],bgi_font_tscr, bgi_font_tscr_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_COMPLEX],    bgi_font_lcom, bgi_font_lcom_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_EUROPEAN],   bgi_font_euro, bgi_font_euro_size);
        bgi_font_parse(&bgi_fonts[BGI_FONT_BOLD],       bgi_font_bold, bgi_font_bold_size);
        bgi_fonts_loaded = true;
    }

    /* ── Drawing Ports — boot-time initialization ──────────────────
     * Port 0 is permanent: full-screen viewport, protected, always active.
     * memset above already zeroed ports[]; only non-zero fields need init. */
    {
        rip_port_t *p0 = &s->ports[0];
        p0->allocated    = true;
        p0->flags        = RIP_PORT_FLAG_PROTECTED | RIP_PORT_FLAG_FULLSCREEN;
        p0->vp_x0        = 0;
        p0->vp_y0        = 0;
        p0->vp_x1        = 639;
        p0->vp_y1        = 399;   /* card pixel coords (EGA 349 -> display 399) */
        p0->draw_color   = 15;    /* white -- matches s->draw_color default */
        p0->fill_color   = 15;
        p0->fill_pattern = 1;     /* solid */
        p0->back_color   = 0;
        p0->write_mode   = 0;     /* COPY */
        p0->line_thick   = 1;
        p0->font_size    = 1;
        p0->font_hjust   = 0;
        p0->font_vjust   = 0;
        p0->font_attrib  = 0;
        p0->font_ext_id  = 0;
        p0->font_ext_attr = 0;
        p0->font_ext_size = 0;
        p0->alpha        = 35;    /* fully opaque */
    }
    s->active_port = 0;
}

/* Codex FIX 1: Backward-compat wrapper — existing call sites continue to work.
 * New code should call rip_init_first() at boot and rip_session_reset() on
 * disconnect rather than calling rip_init() on every protocol switch. */
void rip_init(rip_state_t *s) {
    rip_init_first(s);
}

/* Codex FIX 1: Protocol-switch activation.
 * Called by the compositor every time it switches to RIPscrip (including
 * the first time).  Restores the EGA hardware palette and marks the
 * framebuffer dirty so the BBS screen is repainted.  Does NOT memset or
 * touch session state — clipboard, mouse regions, text variables, and the
 * PSRAM arena are all preserved across a temporary switch to VT100 etc.
 *
 * Codex FIX 5: query_pending / query_var_name are also left untouched here,
 * so a pending $QUERY$ round-trip that was interrupted by a protocol switch
 * resumes correctly when RIPscrip is reactivated. */
void rip_activate(rip_state_t *s) {
    if (!s) return;
    g_rip_state = s;

    /* Restore BBS-customized palette (or EGA defaults on first activation).
     * rip_save_palette() captured saved_palette_rgb565 before the previous
     * deactivation; rip_apply_palette() writes it back to hardware. */
    rip_apply_palette();

    /* Re-apply the active session's drawing and clip state. */
    apply_session_draw_state(s);
}

/* Codex FIX 3: Session disconnect reset.
 * Call this when the BBS connection is dropped (NOT on a protocol switch).
 * Reclaims the PSRAM arena, clears mouse regions, text variables, query
 * state, icon request queue, and upload staging — all the per-session data
 * that must not carry over to the next BBS connection.
 *
 * The PSRAM arena block itself is NOT freed (it stays reserved for the
 * next session); psram_arena_reset() just rewinds the bump pointer.
 *
 * Codex FIX 4: Calls rip_icon_clear_requests() to flush the pending icon
 * file request queue so the next BBS doesn't see stale requests.
 *
 * Codex FIX 5: Clears query_pending / query_var_name so an unanswered
 * $QUERY$ from the disconnected session cannot affect the next session. */
void rip_session_reset(rip_state_t *s) {
    if (!s) return;
    g_rip_state = s;

    /* Reclaim all PSRAM arena allocations (clipboard pixels, cached icon
     * pixels, upload staging buffer) from the disconnected session. */
    psram_arena_reset(&s->psram_arena);

    /* Rebind the icon cache to the freshly-reset arena.  This also clears
     * the runtime cache so stale pixel pointers into the old arena are gone. */
    rip_icon_set_arena(&s->icon_state, &s->psram_arena);

    /* Codex FIX 4: Flush the pending icon file request queue. */
    rip_icon_clear_requests(&s->icon_state);

    /* Clear upload staging pointer — it pointed into the now-reset arena. */
    s->upload_buf = NULL;
    rip_upload_reset(s);

    /* Clear mouse regions from the previous session. */
    memset(s->mouse_regions, 0, sizeof(s->mouse_regions));
    s->num_mouse_regions = 0;

    /* Clear text block state. */
    s->text_block.active = false;

    /* Codex FIX 5: Clear query/prompt metadata — an unanswered $QUERY$
     * from the disconnected session must not carry over to the next BBS. */
    s->query_pending = false;
    memset(s->query_var_name,  0, sizeof(s->query_var_name));
    memset(s->query_response,  0, sizeof(s->query_response));
    s->query_response_len = 0;

    /* Clear clipboard — pixel data was in the arena, now invalid. */
    s->clipboard.data  = NULL;
    s->clipboard.valid = false;
    s->clipboard.width = 0;
    s->clipboard.height = 0;
    memset(s->icon_slots, 0, sizeof(s->icon_slots));
    memset(s->icon_slot_valid, 0, sizeof(s->icon_slot_valid));
    s->icon_style_active = false;
    s->icon_style_x0 = 0;
    s->icon_style_y0 = 0;
    s->icon_style_x1 = 0;
    s->icon_style_y1 = 0;
    s->icon_style_style = 0;
    s->icon_style_align = 0;
    s->icon_style_scale = 0;

    /* Clear application variables. */
    memset(s->app_vars, 0, sizeof(s->app_vars));
    memset(s->user_var_names, 0, sizeof(s->user_var_names));
    memset(s->user_var_values, 0, sizeof(s->user_var_values));
    s->user_var_count = 0;

    /* Reset scene/protocol mode metadata for the next BBS session. */
    s->header_type = 0;
    s->header_id = 0;
    s->header_flags = 0;
    s->resolution_mode = 0;
    s->coordinate_size = 2;
    s->coordinate_res = 0;
    s->color_mode = 0;
    s->color_bits = 0;
    s->filled_borders_enabled = true;

    /* Reset stream preprocessor state. */
    s->preproc_state = 0;
    s->preproc_len = 0;
    s->preproc_suppress = false;
    s->preproc_depth = 0;
    s->preproc_overflow = 0;
    memset(s->preproc_parent_suppress, 0, sizeof(s->preproc_parent_suppress));
    memset(s->preproc_branch_active, 0, sizeof(s->preproc_branch_active));
    memset(s->preproc_branch_taken, 0, sizeof(s->preproc_branch_taken));

    /* Reset ripscrip2 overflow state. */
    ripscrip2_init(&s->rip2_state);

    /* Reset parser and drawing defaults for the next session. */
    s->state = RIP_ST_IDLE;
    s->cmd_len = 0;
    s->cmd_char = '\0';
    clear_levels(s);
    s->line_cont = false;
    s->last_char = 0;
    s->esc_detect = 0;
    s->utf8_pipe_pending = false;
    s->draw_x = 0;
    s->draw_y = 0;
    s->draw_color = 15;
    s->back_color = 0;
    s->write_mode = 0;
    s->line_style = 0;
    s->line_thick = 1;
    s->fill_pattern = 1;
    s->fill_color = 15;
    s->font_id = 0;
    s->font_dir = 0;
    s->font_size = 1;
    s->font_hjust = 0;
    s->font_vjust = 0;
    s->font_attrib = 0;
    s->font_ext_id = 0;
    s->font_ext_attr = 0;
    s->font_ext_size = 0;
    s->tw_x0 = 0;
    s->tw_y0 = 0;
    s->tw_x1 = 639;
    s->tw_y1 = 349;
    s->tw_wrap = 0;
    s->tw_font_size = 0;
    s->tw_cur_x = 0;
    s->tw_cur_y = 0;
    s->tw_active = false;
    s->rip_has_drawn = false;
    s->cursor_repositioned = false;
    s->refresh_suppress = false;

    /* Reset Drawing Port table — deallocate all ports except port 0.
     * Port 0 gets its state refreshed to defaults; other slots are cleared. */
    memset(&s->ports[0], 0, sizeof(rip_port_t));
    for (int i = 1; i < RIP_MAX_PORTS; i++)
        memset(&s->ports[i], 0, sizeof(rip_port_t));
    {
        rip_port_t *p0 = &s->ports[0];
        p0->allocated    = true;
        p0->flags        = RIP_PORT_FLAG_PROTECTED | RIP_PORT_FLAG_FULLSCREEN;
        p0->vp_x0        = 0;   p0->vp_y0 = 0;
        p0->vp_x1        = 639; p0->vp_y1 = 399;
        p0->draw_color   = 15;
        p0->fill_color   = 15;
        p0->fill_pattern = 1;
        p0->back_color   = 0;
        p0->write_mode   = 0;
        p0->line_thick   = 1;
        p0->font_size    = 1;
        p0->font_hjust   = 0;
        p0->font_vjust   = 0;
        p0->font_attrib  = 0;
        p0->font_ext_id  = 0;
        p0->font_ext_attr = 0;
        p0->font_ext_size = 0;
        p0->alpha        = 35;
    }
    s->active_port = 0;
    s->vp_x0 = 0; s->vp_y0 = 0;
    s->vp_x1 = 639; s->vp_y1 = 399;
    apply_session_draw_state(s);
}

/* ══════════════════════════════════════════════════════════════════
 * BGI FILL STYLE → CARD FILL PATTERN MAPPING
 *
 * BGI fill styles (used by RIPscrip 'S' command):
 *   0=EMPTY 1=SOLID 2=LINE 3=LTSLASH 4=SLASH 5=BKSLASH
 *   6=LTBKSLASH 7=HATCH 8=XHATCH 9=INTERLEAVE 10=WIDE_DOT 11=CLOSE_DOT
 *   12=USER
 *
 * Card fill engine uses: 0=solid, 1-7=predefined, 8=user pattern.
 * BGI 0 (EMPTY) needs special handling — don't fill at all.
 * ══════════════════════════════════════════════════════════════════ */

/* Map BGI fill_style to card pattern_id. Returns -1 for EMPTY_FILL.
 *
 * BGI styles (Borland Graphics Interface):
 *   0 EMPTY 1 SOLID 2 LINE 3 LTSLASH 4 SLASH 5 BKSLASH
 *   6 LTBKSLASH 7 HATCH 8 XHATCH 9 INTERLEAVE 10 WIDE_DOT 11 CLOSE_DOT 12 USER
 *
 * Card fill_patterns[] (drawing.c): 0=solid 1=50%checker 2=diag\ 3=diag/
 *   4=horizontal 5=vertical 6=hatch 7=lightdiag 8=interleave 9=widedot
 *   10=closedot.  Slot 11 = user_pattern[].
 *
 * Previous mapping (bgi_style-1) was incorrect — BGI 2 LINE is supposed
 * to be horizontal lines but mapped to the 50% checker.  This table maps
 * each BGI style to the closest visually-matching card pattern.
 *
 * Exposed via include/ripscrip.h so tests and ripscrip2.c can share it. */
int8_t rip_bgi_fill_to_card(uint8_t bgi_style) {
    switch (bgi_style) {
        case 0:  return -1;  /* EMPTY  — caller skips fill */
        case 1:  return 0;   /* SOLID  → solid */
        case 2:  return 4;   /* LINE   → horizontal */
        case 3:  return 7;   /* LTSLASH→ light diagonal (sparse /) */
        case 4:  return 3;   /* SLASH  → diagonal / */
        case 5:  return 2;   /* BKSLASH→ diagonal \ */
        case 6:  return 2;   /* LTBKSLASH→ diagonal \ (no lighter variant) */
        case 7:  return 6;   /* HATCH  → cross-hatch */
        case 8:  return 1;   /* XHATCH → 50% checker (closest dense X feel) */
        case 9:  return 8;   /* INTERLEAVE → CC/33 interleave */
        case 10: return 9;   /* WIDE_DOT */
        case 11: return 10;  /* CLOSE_DOT */
        case 12: return 11;  /* USER → user_pattern */
        default: return 0;   /* unknown → solid */
    }
}

/* Internal alias retained for the rest of this TU. */
static int8_t bgi_fill_to_card(uint8_t bgi_style) {
    return rip_bgi_fill_to_card(bgi_style);
}

/* ══════════════════════════════════════════════════════════════════
 * TEXT ESCAPE PROCESSING — \! \| \\ inline escapes
 *
 * RIPscrip text parameters use backslash escapes:
 *   \! = literal '!'   \| = literal '|'   \\ = literal '\'
 * Returns unescaped length (always <= input length, in-place safe).
 * ══════════════════════════════════════════════════════════════════ */

static int unescape_text(const char *src, int len, char *dst) {
    int j = 0;
    for (int i = 0; i < len; i++) {
        if (src[i] == '\\' && i + 1 < len) {
            char next = src[i + 1];
            if (next == '!' || next == '|' || next == '\\') {
                dst[j++] = next;
                i++; /* skip escaped char */
                continue;
            }
        }
        dst[j++] = src[i];
    }
    return j;
}

/* ══════════════════════════════════════════════════════════════════
 * TEXT VARIABLE EXPANSION — $VARIABLE$ substitution
 *
 * DLL ground truth (rip_textvars.c): text output layer scans for $VAR$
 * delimiters and substitutes registered variable values before rendering.
 * Recognized variables:
 *   $DATE$        — current date  (MM/DD/YY)
 *   $TIME$        — current time  (HH:MM)
 *   $USER$        — empty string  (no login name on embedded card)
 *   $PROT$        — resolution mode: "0"=EGA, "1"=VGA (DLL GFXSTYLE field)
 *   $APP0$-$APP9$ — application-defined variables (rip_state_t.app_vars)
 * Unknown variables are passed through unchanged (including $…$ delimiters).
 * ══════════════════════════════════════════════════════════════════ */

/* Expand $VARIABLE$ references in text buffer.
 * in/in_len: source (need not be NUL-terminated).
 * out: output buffer (NUL-terminated on return).
 * max_out: total capacity of out including NUL terminator.
 * Returns number of characters written (excluding NUL). */
static int rip_expand_variables(rip_state_t *s,
                                const char *in, int in_len,
                                char *out, int max_out) {
    int o = 0;
    int i = 0;

    while (i < in_len && o < max_out - 1) {
        if (in[i] != '$') {
            out[o++] = in[i++];
            continue;
        }

        /* Scan forward for matching closing '$' */
        int j = i + 1;
        while (j < in_len && in[j] != '$') j++;

        if (j >= in_len) {
            /* No closing '$' found — treat as literal and move on */
            out[o++] = in[i++];
            continue;
        }

        /* Variable name occupies in[i+1 .. j-1] */
        const char *vname = in + i + 1;
        int vlen = j - i - 1;
        char val[64];
        int vval_len = -1; /* -1 = unrecognized, emit literal */

        if (vlen == 4 && memcmp(vname, "DATE", 4) == 0) {
            /* $DATE$ — FIX V1: use host-supplied date (CB_GET_TIME equivalent).
             * The IIgs reads the ProDOS MLI GET_TIME clock and ships it via
             * CMD_SYNC_DATE at connect time.  Fall back to the RP2350 RTC
             * (time()/localtime()) only when the host has not synced yet.
             * Boundary note: calling time() here would use the card's own RTC
             * which may drift from the BBS host clock — the callback model
             * fixes this by making the IIgs the sole clock authority. */
            if (s->host_date[0] != '\0') {
                vval_len = (int)rip_strnlen(s->host_date, sizeof(s->host_date) - 1);
                if (vval_len > (int)sizeof(val) - 1) vval_len = (int)sizeof(val) - 1;
                memcpy(val, s->host_date, (size_t)vval_len);
            } else {
                /* Host not yet synced — fall back to RP2350 RTC */
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%02d/%02d/%02d",
                                    tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
                if (vval_len < 0) vval_len = 0;
            }
        } else if (vlen == 4 && memcmp(vname, "TIME", 4) == 0) {
            /* $TIME$ — FIX V1: use host-supplied time (CB_GET_TIME equivalent).
             * Falls back to RP2350 RTC when host has not synced yet. */
            if (s->host_time[0] != '\0') {
                vval_len = (int)rip_strnlen(s->host_time, sizeof(s->host_time) - 1);
                if (vval_len > (int)sizeof(val) - 1) vval_len = (int)sizeof(val) - 1;
                memcpy(val, s->host_time, (size_t)vval_len);
            } else {
                /* Host not yet synced — fall back to RP2350 RTC */
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%02d:%02d",
                                    tm->tm_hour, tm->tm_min);
                if (vval_len < 0) vval_len = 0;
            }
        } else if (vlen == 4 && memcmp(vname, "USER", 4) == 0) {
            /* $USER$ — no login name on embedded card; substitute empty string */
            val[0] = '\0';
            vval_len = 0;
        } else if (vlen == 4 && memcmp(vname, "PROT", 4) == 0) {
            /* $PROT$ — negotiated resolution mode (DLL GFXSTYLE resolution_mode).
             * 0=EGA(640x350), 1=VGA(640x480), 2=SVGA, 3=XGA, 4=HIGH. */
            vval_len = snprintf(val, sizeof(val), "%u", s->resolution_mode);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 9 && memcmp(vname, "COLORMODE", 9) == 0) {
            /* $COLORMODE$ returns 0 in palette-mapping mode, or the RGB
             * bits/component value (1..8) in direct RGB mode. */
            unsigned mode = (s->color_mode == 0) ? 0u : (unsigned)s->color_bits;
            vval_len = snprintf(val, sizeof(val), "%u", mode);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 9 && memcmp(vname, "COORDSIZE", 9) == 0) {
            vval_len = snprintf(val, sizeof(val), "%u", s->coordinate_size);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 9 && memcmp(vname, "ISPALETTE", 9) == 0) {
            val[0] = '1';
            vval_len = 1;
        } else if (vlen == 4 && vname[0] == 'A' && vname[1] == 'P' &&
                   vname[2] == 'P' && vname[3] >= '0' && vname[3] <= '9') {
            /* $APP0$-$APP9$ — application-defined variables */
            int idx = vname[3] - '0';
            vval_len = (int)rip_strnlen(s->app_vars[idx], sizeof(s->app_vars[0]));
            if (vval_len > (int)sizeof(val) - 1) vval_len = (int)sizeof(val) - 1;
            memcpy(val, s->app_vars[idx], (size_t)vval_len);

        /* FIX M3: Sound text variables (CB_PLAY_SOUND callback equivalent).
         * In RIPSCRIP.DLL the host filled the sound callback slot and the DLL
         * called it when these variables were expanded.  On the A2GSPU we
         * cannot play audio on the card; instead we signal the IIgs:
         *   $BEEP$  — push BEL (0x07) through TX FIFO; IIgs bridge loop calls
         *             the IIgs BELL toolbox routine on receipt.
         *   Others  — push CMD_PLAY_SOUND marker (0x3D) + sound-token string
         *             + NUL so the IIgs can dispatch to DOC sound chip (future).
         * All sound variables expand to the empty string in the text stream. */
        } else if (vlen == 4 && memcmp(vname, "BEEP", 4) == 0) {
            /* $BEEP$ — BEL character; IIgs bridge loop handles audible beep */
            card_tx_push("\x07", 1);
            val[0] = '\0';
            vval_len = 0;
        } else if (vlen == 4 && memcmp(vname, "BLIP", 4) == 0) {
            /* $BLIP$ — short click tone; send CMD_PLAY_SOUND token to IIgs */
            card_tx_push("\x3D" "BLIP\0", 6);  /* marker + "BLIP" + NUL */
            val[0] = '\0';
            vval_len = 0;
        } else if (vlen == 5 && memcmp(vname, "ALARM", 5) == 0) {
            /* $ALARM$ — alarm tone sequence */
            card_tx_push("\x3D" "ALARM\0", 7);
            val[0] = '\0';
            vval_len = 0;
        } else if (vlen == 6 && memcmp(vname, "PHASER", 6) == 0) {
            /* $PHASER$ — phaser sweep tone */
            card_tx_push("\x3D" "PHASER\0", 8);
            val[0] = '\0';
            vval_len = 0;
        } else if (vlen == 5 && memcmp(vname, "MUSIC", 5) == 0) {
            /* $MUSIC$ — background music cue */
            card_tx_push("\x3D" "MUSIC\0", 7);
            val[0] = '\0';
            vval_len = 0;

        /* FIX TX1: $RAND$ — pseudo-random number.
         * ripTextVarEngine @ 0x026218 calls rand() here; we use the same
         * Knuth/POSIX LCG (multiplier 1103515245, addend 12345) so the
         * generated sequence is compatible with the DLL ground truth. */
        } else if (vlen == 4 && memcmp(vname, "RAND", 4) == 0) {
            s->rand_state = s->rand_state * 1103515245u + 12345u;
            vval_len = snprintf(val, sizeof(val), "%u",
                                (unsigned)((s->rand_state >> 16) & 0x7FFFu));
            if (vval_len < 0) vval_len = 0;

        /* FIX TX2: $RIPVER$ — protocol version string.
         * DLL ripTextVarEngine returns "RIPSCRIP030001" for v3.0.
         * A2GSPU reports "RIPSCRIP031001" — v3.1 with A2GSPU extensions. */
        } else if (vlen == 6 && memcmp(vname, "RIPVER", 6) == 0) {
            vval_len = snprintf(val, sizeof(val), "RIPSCRIP031001");
            if (vval_len < 0) vval_len = 0;

        /* FIX TX3: $REFRESH$ — re-enable and trigger a full screen refresh.
         * DLL: clears the no-refresh flag and calls ripInvalidateAll().
         * A2GSPU: clear refresh_suppress and mark full framebuffer dirty. */
        } else if (vlen == 7 && memcmp(vname, "REFRESH", 7) == 0) {
            s->refresh_suppress = false;
            draw_mark_all_dirty();   /* equivalent to ripInvalidateAll() */
            val[0] = '\0';
            vval_len = 0;

        /* FIX TX4: $NOREFRESH$ — suppress automatic screen refresh.
         * DLL: sets the no-refresh flag so that intermediate drawing steps
         * don't cause visible flicker during multi-command scene build. */
        } else if (vlen == 9 && memcmp(vname, "NOREFRESH", 9) == 0) {
            s->refresh_suppress = true;
            val[0] = '\0';
            vval_len = 0;

        /* FIX TX5: $TEXTDATA$ — contents of the active bounded text buffer.
         * DLL: returns the accumulated text from the '"' (ICON_QUERY) bounded
         * text parameter.  A2GSPU: return empty string (no bounded text buffer
         * maintained on the card side; data is rendered immediately on receipt). */
        } else if (vlen == 8 && memcmp(vname, "TEXTDATA", 8) == 0) {
            val[0] = '\0';
            vval_len = 0;

        /* FIX TX6: $YEAR$ — 4-digit current year.
         * DLL ripTextVarEngine @ 0x026218, handler @ 0x0390CA.
         * Use host_date if synced (IIgs ProDOS clock); fall back to RP2350 RTC. */
        } else if (vlen == 4 && memcmp(vname, "YEAR", 4) == 0) {
            if (s->host_date[0] != '\0') {
                /* host_date is "MM/DD/YY" — year is the last two digits.
                 * Expand to 4-digit year by assuming 2000+ (BBS era is over). */
                int yy = 0;
                const char *p_slash2 = s->host_date + 6;  /* points past "MM/DD/" */
                if (p_slash2[0] >= '0' && p_slash2[0] <= '9' &&
                    p_slash2[1] >= '0' && p_slash2[1] <= '9') {
                    yy = (p_slash2[0] - '0') * 10 + (p_slash2[1] - '0');
                }
                vval_len = snprintf(val, sizeof(val), "%04d", 2000 + yy);
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%04d", 1900 + tm->tm_year);
            }
            if (vval_len < 0) vval_len = 0;

        /* FIX TX7: $WOYM$ — ISO week-of-year (Monday start), 2-digit string.
         * DLL ripTextVarEngine @ 0x026218, handler @ 0x0390B0 ("WOYM"). */
        } else if (vlen == 4 && memcmp(vname, "WOYM", 4) == 0) {
            int week = 0;
            if (s->host_date[0] != '\0') {
                int year;
                int month;
                int day;
                if (rip_parse_host_date(s->host_date, &year, &month, &day))
                    week = rip_iso_week(year, month, day);
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                week = rip_iso_week(1900 + tm->tm_year,
                                    tm->tm_mon + 1,
                                    tm->tm_mday);
            }
            if (week < 1) week = 1;
            if (week > 53) week = 53;
            vval_len = snprintf(val, sizeof(val), "%02d", week);
            if (vval_len < 0) vval_len = 0;

        /* FIX TX8: $COMPAT$ — compatibility level string.
         * DLL: reports a numeric mode flag used by host to detect capability.
         * A2GSPU: return "1" (basic compatibility, level 1 extensions present). */
        } else if (vlen == 6 && memcmp(vname, "COMPAT", 6) == 0) {
            vval_len = snprintf(val, sizeof(val), "1");
            if (vval_len < 0) vval_len = 0;

        /* FIX TX9: $MKILL$ — kill/clear all mouse fields via text variable.
         * DLL ripTextVarEngine: calling $MKILL$ is equivalent to !|1K|.
         * A2GSPU: zero the mouse region table immediately. */
        } else if (vlen == 5 && memcmp(vname, "MKILL", 5) == 0) {
            s->num_mouse_regions = 0;
            val[0] = '\0';
            vval_len = 0;

        /* FIX TX10: $COPY$ — set write mode to COPY(0) via text variable.
         * DLL: switches GDI ROP back to COPY mode (ripTextVarEngine). */
        } else if (vlen == 4 && memcmp(vname, "COPY", 4) == 0) {
            s->write_mode = 0;
            draw_set_write_mode(0);
            val[0] = '\0';
            vval_len = 0;

        /* FIX TX11: $COFF$ — cursor off (hide hardware cursor).
         * DLL: calls cursor hide callback.  A2GSPU: no hardware cursor;
         * no-op but recognised so the variable doesn't pass through as literal. */
        } else if (vlen == 4 && memcmp(vname, "COFF", 4) == 0) {
            val[0] = '\0';
            vval_len = 0;

        } else if (vlen == 5 && memcmp(vname, "RESET", 5) == 0) {
            rip_reset_windows_state(s, NULL);
            val[0] = '\0';
            vval_len = 0;

        /* $ABORT$ — abort the current RIPscrip scene (reset state).
         * DLL: sets abort flag; parser discards remaining stream bytes.
         * A2GSPU: reset the FSM to IDLE so the next !| starts fresh. */
        } else if (vlen == 5 && memcmp(vname, "ABORT", 5) == 0) {
            s->state = RIP_ST_IDLE;
            clear_levels(s);
            s->cmd_len = 0;
            val[0] = '\0';
            vval_len = 0;

        /* §A2G2: Layout / introspection variables ----------------- */
        } else if (vlen == 2 && memcmp(vname, "CX", 2) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d", s->draw_x);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 2 && memcmp(vname, "CY", 2) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d", s->draw_y);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 3 && memcmp(vname, "VPW", 3) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)(s->vp_x1 - s->vp_x0 + 1));
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 3 && memcmp(vname, "VPH", 3) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)(s->vp_y1 - s->vp_y0 + 1));
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 4 && memcmp(vname, "VPCX", 4) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)((s->vp_x0 + s->vp_x1) / 2));
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 4 && memcmp(vname, "VPCY", 4) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)((s->vp_y0 + s->vp_y1) / 2));
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 4 && memcmp(vname, "CCOL", 4) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)(s->draw_color & 0x0F));
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 5 && memcmp(vname, "CFCOL", 5) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)(s->fill_color & 0x0F));
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 5 && memcmp(vname, "CBCOL", 5) == 0) {
            vval_len = snprintf(val, sizeof(val), "%d",
                                (int)(s->back_color & 0x0F));
            if (vval_len < 0) vval_len = 0;

        /* §A2G2: Time component variables ------------------------- */
        } else if (vlen == 4 && memcmp(vname, "HOUR", 4) == 0) {
            if (s->host_time[0] >= '0' && s->host_time[0] <= '9' &&
                s->host_time[1] >= '0' && s->host_time[1] <= '9') {
                val[0] = s->host_time[0]; val[1] = s->host_time[1];
                vval_len = 2;
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%02d", tm->tm_hour);
                if (vval_len < 0) vval_len = 0;
            }
        } else if (vlen == 3 && memcmp(vname, "MIN", 3) == 0) {
            if (s->host_time[3] >= '0' && s->host_time[3] <= '9' &&
                s->host_time[4] >= '0' && s->host_time[4] <= '9') {
                val[0] = s->host_time[3]; val[1] = s->host_time[4];
                vval_len = 2;
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%02d", tm->tm_min);
                if (vval_len < 0) vval_len = 0;
            }
        } else if (vlen == 3 && memcmp(vname, "SEC", 3) == 0) {
            /* host_time is HH:MM only; SEC always reads from RTC. */
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            vval_len = snprintf(val, sizeof(val), "%02d", tm->tm_sec);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 3 && memcmp(vname, "DOW", 3) == 0) {
            int year, month, day, dow;
            if (s->host_date[0] != '\0' &&
                rip_parse_host_date(s->host_date, &year, &month, &day)) {
                dow = rip_weekday_monday0(year, month, day);
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                /* tm_wday: 0=Sunday..6=Saturday. Convert to Monday=0. */
                dow = (tm->tm_wday + 6) % 7;
            }
            vval_len = snprintf(val, sizeof(val), "%d", dow);
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 3 && memcmp(vname, "DOM", 3) == 0) {
            int year, month, day;
            if (s->host_date[0] != '\0' &&
                rip_parse_host_date(s->host_date, &year, &month, &day)) {
                vval_len = snprintf(val, sizeof(val), "%02d", day);
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%02d", tm->tm_mday);
            }
            if (vval_len < 0) vval_len = 0;
        } else if (vlen == 5 && memcmp(vname, "MONTH", 5) == 0) {
            int year, month, day;
            if (s->host_date[0] != '\0' &&
                rip_parse_host_date(s->host_date, &year, &month, &day)) {
                vval_len = snprintf(val, sizeof(val), "%02d", month);
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                vval_len = snprintf(val, sizeof(val), "%02d", tm->tm_mon + 1);
            }
            if (vval_len < 0) vval_len = 0;

        /* §A2G2: EGA color-name aliases — expand to 2-digit MegaNum
         * suitable as a |c, |S, |k, |a argument.  Each name maps to
         * its EGA palette index (0-15).  Names are 16 chars max. */
        } else if (vlen >= 3 && vlen <= 13) {
            static const struct { const char *name; uint8_t len; uint8_t idx; }
                color_names[] = {
                    { "BLACK",        5, 0x0 },
                    { "BLUE",         4, 0x1 },
                    { "GREEN",        5, 0x2 },
                    { "CYAN",         4, 0x3 },
                    { "RED",          3, 0x4 },
                    { "MAGENTA",      7, 0x5 },
                    { "BROWN",        5, 0x6 },
                    { "LIGHTGRAY",    9, 0x7 },
                    { "DARKGRAY",     8, 0x8 },
                    { "LIGHTBLUE",    9, 0x9 },
                    { "LIGHTGREEN",  10, 0xA },
                    { "LIGHTCYAN",    9, 0xB },
                    { "LIGHTRED",     8, 0xC },
                    { "LIGHTMAGENTA",12, 0xD },
                    { "YELLOW",       6, 0xE },
                    { "WHITE",        5, 0xF },
                };
            bool matched = false;
            for (size_t ci = 0; ci < sizeof(color_names)/sizeof(color_names[0]); ci++) {
                if (color_names[ci].len == (uint8_t)vlen &&
                    memcmp(vname, color_names[ci].name, (size_t)vlen) == 0) {
                    /* MegaNum encoding: index 0-15 → "00".."0F" */
                    val[0] = '0';
                    val[1] = (color_names[ci].idx < 10)
                             ? (char)('0' + color_names[ci].idx)
                             : (char)('A' + color_names[ci].idx - 10);
                    vval_len = 2;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                /* Not a color name — fall through to user-var lookup */
                int uidx = rip_user_var_find(s, vname, vlen);
                if (uidx >= 0) {
                    vval_len = (int)rip_strnlen(s->user_var_values[uidx],
                                                sizeof(s->user_var_values[uidx]));
                    if (vval_len > (int)sizeof(val) - 1)
                        vval_len = (int)sizeof(val) - 1;
                    memcpy(val, s->user_var_values[uidx], (size_t)vval_len);
                }
            }

        } else {
            int uidx = rip_user_var_find(s, vname, vlen);
            if (uidx >= 0) {
                vval_len = (int)rip_strnlen(s->user_var_values[uidx],
                                            sizeof(s->user_var_values[uidx]));
                if (vval_len > (int)sizeof(val) - 1)
                    vval_len = (int)sizeof(val) - 1;
                memcpy(val, s->user_var_values[uidx], (size_t)vval_len);
            }
        }

        if (vval_len >= 0) {
            /* Recognized variable — substitute its value */
            int copy = vval_len;
            if (o + copy > max_out - 1) copy = max_out - 1 - o;
            memcpy(out + o, val, (size_t)copy);
            o += copy;
            i = j + 1; /* advance past closing '$' */
        } else {
            /* Unrecognized — emit literal '$' and retry from i+1 */
            out[o++] = in[i++];
        }
    }

    out[o] = '\0';
    return o;
}

/* ══════════════════════════════════════════════════════════════════
 * TEXT WINDOW PASSTHROUGH — render text within RIP text window bounds
 * ══════════════════════════════════════════════════════════════════ */

static void rip_tw_putchar(rip_state_t *s, uint8_t ch) {
    int16_t tw_x1_s = s->tw_x1;
    int16_t tw_y1_s = scale_y1(s->tw_y1);
    const int16_t char_w = 8;
    const int16_t char_h = 16;

    if (ch == '\r') {
        s->tw_cur_x = s->tw_x0;
        return;
    }
    if (ch == '\n') {
        s->tw_cur_y += char_h;
        /* Scroll if past bottom of text window */
        if (s->tw_cur_y + char_h > tw_y1_s) {
            int16_t tw_y0_s = scale_y(s->tw_y0);
            int16_t tw_w = (int16_t)(tw_x1_s - s->tw_x0 + 1);
            int16_t tw_h = (int16_t)(tw_y1_s - tw_y0_s + 1);
            draw_copy_rect(s->tw_x0, tw_y0_s + char_h,
                           s->tw_x0, tw_y0_s,
                           tw_w,
                           (int16_t)(tw_h - char_h));
            /* Clear the last line */
            draw_set_color(0);
            draw_rect(s->tw_x0, (int16_t)(tw_y1_s - char_h + 1),
                      tw_w, char_h, true);
            draw_set_color(s->palette[s->draw_color & 0x0F]);
            s->tw_cur_y = tw_y1_s - char_h;
        }
        return;
    }
    if (ch == '\b') {
        if (s->tw_cur_x >= s->tw_x0 + char_w)
            s->tw_cur_x -= char_w;
        return;
    }
    if (ch == '\t') {
        int spaces = 8 - ((s->tw_cur_x - s->tw_x0) / char_w % 8);
        for (int i = 0; i < spaces; i++)
            rip_tw_putchar(s, ' ');
        return;
    }
    if (ch < 0x20) return; /* ignore other control chars */

    /* Word wrap: if next char would go past right edge, newline first */
    if (s->tw_wrap && s->tw_cur_x + char_w > tw_x1_s) {
        s->tw_cur_x = s->tw_x0;
        rip_tw_putchar(s, '\n');
    }

    /* Draw the character.  L17: previously passed NULL font, which made
     * draw_text early-return — every byte routed to the text window
     * passthrough was invisible.  Use cp437_8x16 like every other
     * bitmap-font draw site. */
    uint8_t tc = s->palette[s->draw_color & 0x0F];
    draw_text(s->tw_cur_x, s->tw_cur_y, (const char *)&ch, 1,
              cp437_8x16, 16u, tc, 0xFF);
    s->tw_cur_x += char_w;

    /* Wrap if past right edge (non-wrap mode: just clip) */
    if (s->tw_cur_x > tw_x1_s) {
        if (s->tw_wrap) {
            s->tw_cur_x = s->tw_x0;
            rip_tw_putchar(s, '\n');
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 * MOUSE HIT-TEST — dispatches click to matching region
 * ══════════════════════════════════════════════════════════════════ */

void rip_mouse_event_state(rip_state_t *s, int16_t x, int16_t y, bool clicked) {
    if (!s || !clicked) return;

    for (int i = (int)s->num_mouse_regions - 1; i >= 0; i--) {
        rip_mouse_region_t *r = &s->mouse_regions[i];

        /* DLL: field must have MF_ACTIVE(0x04) set to be hit-testable.
         * rip_mouse.c field record +0x20, pFieldBuf+0x20 |= MF_ACTIVE */
        if (!(r->flags & RIP_MF_ACTIVE)) continue;
        /* L10: TOGGLE regions remain hit-testable regardless of
         * r->active because that field tracks the toggled "checked"
         * state, not the live/dead state.  For non-TOGGLE regions,
         * r->active=false means the region was retired (e.g., by a
         * one-shot click) and should not respond. */
        if (!(r->flags & RIP_MF_TOGGLE) && !r->active) continue;

        if (x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1) {

            /* MF_RADIO(0x20): deselect all other regions in same group
             * before activating this one.  DLL: ripCmd_MouseRegion flags&2 → MF_RADIO */
            if (r->flags & RIP_MF_RADIO) {
                for (int j = 0; j < s->num_mouse_regions; j++) {
                    if (j != i)
                        s->mouse_regions[j].active = false;
                }
            }

            /* MF_TOGGLE(0x40): flip active state on each click rather than
             * triggering the host command immediately.
             * DLL: ripCmd_MouseRegion flags&4 → MF_TOGGLE */
            if (r->flags & RIP_MF_TOGGLE) {
                r->active = !r->active;
                /* Visual feedback: XOR-invert the region to show toggle state */
                draw_set_write_mode(DRAW_MODE_XOR);
                draw_set_color(0xFF);
                draw_rect(r->x0, r->y0,
                          (int16_t)(r->x1 - r->x0 + 1),
                          (int16_t)(r->y1 - r->y0 + 1), true);
                draw_set_write_mode(s->write_mode);
                draw_set_color(s->palette[s->draw_color & 0x0F]);
                return;
            }

            /* MF_SEND_CHAR(0x08): send the hotkey character rather than
             * the host command string.
             * DLL: ripCmd_MouseRegion flags&1 → MF_SEND_CHAR; hotkey stored at +0x2B */
            if ((r->flags & RIP_MF_SEND_CHAR) && r->hotkey != 0) {
                char hk = (char)r->hotkey;
                card_tx_push(&hk, 1);
                card_tx_push("\r", 1);
                return;
            }

            /* Default: send the host command string */
            if (r->text_len > 0) {
                card_tx_push(r->text, r->text_len);
                card_tx_push("\r", 1);
            }

            /* Deactivate region after click (one-shot button behavior) */
            r->active = false;
            return; /* first match wins */
        }
    }
}

void rip_mouse_event_ext(int16_t x, int16_t y, bool clicked) {
    rip_mouse_event_state(g_rip_state, x, y, clicked);
}

/* ══════════════════════════════════════════════════════════════════
 * FILE UPLOAD — receive BMP/ICN data from host for PSRAM caching
 * ══════════════════════════════════════════════════════════════════ */

void rip_file_upload_begin_state(rip_state_t *s, uint8_t name_len) {
    if (!s) return;
    if (!s->upload_buf)
        s->upload_buf = (uint8_t *)psram_arena_alloc(&s->psram_arena,
                                                     FILE_UPLOAD_MAX);
    rip_upload_reset(s);
    s->upload_name_remaining = name_len;
    s->upload_name_overflow = (name_len >= sizeof(s->upload_name));
    s->upload_reading_name = (name_len > 0);
    /* Name bytes follow as FILE_UPLOAD_DATA writes */
}

void rip_file_upload_byte_state(rip_state_t *s, uint8_t b) {
    if (!s) return;
    if (s->upload_reading_name) {
        if (s->upload_name_len < (int)sizeof(s->upload_name) - 1) {
            s->upload_name[s->upload_name_len++] = (char)b;
            s->upload_name[s->upload_name_len] = '\0';
        } else {
            s->upload_name_overflow = true;
        }
        if (s->upload_name_remaining > 0)
            s->upload_name_remaining--;
        if (s->upload_name_remaining == 0) {
            s->upload_reading_name = false; /* name complete, data follows */
        }
        return;
    }
    if (s->upload_buf && s->upload_pos < FILE_UPLOAD_MAX)
        s->upload_buf[s->upload_pos++] = b;
}

void rip_file_upload_end_state(rip_state_t *s) {
    if (!s) return;
    if (!s->upload_buf || s->upload_pos < 4 ||
        s->upload_reading_name || s->upload_name_remaining != 0) {
        rip_upload_reset(s);
        return;
    }

    /* RAF archive support — requires rip_raf.h (not in standalone RIPlib) */
#ifdef RIPLIB_HAS_RAF
    /* Try RAF archive first ("SQSH" magic at byte 0x10 in the 0x64-byte header).
     * DLL ground truth: ripResFileReadIndex (RVA 0x0648B9) validates the magic
     * at buf[0x10] and decodes each 0x13-byte index entry via XOR (sub_0756C4).
     * When the host transfers a .RAF archive, we unpack every member into the
     * PSRAM icon cache so subsequent LOAD_ICON commands find them immediately. */
    if (s->upload_pos >= 0x64 + 4 &&
        (uint32_t)(s->upload_buf[0x10]       |
                  ((uint32_t)s->upload_buf[0x11] << 8) |
                  ((uint32_t)s->upload_buf[0x12] << 16) |
                  ((uint32_t)s->upload_buf[0x13] << 24)) == RAF_MAGIC_SQSH) {

        raf_archive_t raf;
        if (raf_open(&raf, s->upload_buf, (uint32_t)s->upload_pos,
                     &s->psram_arena)) {

            /* Allocate a decompression scratch buffer from the arena.
             * 64 KB covers the largest icon likely to appear in a RAF archive.
             * DLL: rafDecompressEntry uses a 0x400-byte ring + 0x200 input chunk;
             * we decompress into a flat buffer and hand off to the existing BMP/ICN
             * parsers. (RVA 0x064D68 rafDecompressEntry, RVA 0x06522A rafInflateBlock) */
            const uint32_t RAF_SCRATCH_MAX = 64u * 1024u;
            uint8_t *scratch = (uint8_t *)malloc(RAF_SCRATCH_MAX);

            if (scratch) {
                for (uint16_t mi = 0; mi < raf.count; mi++) {
                    const raf_index_entry_t *me = &raf.entries[mi];

                    uint32_t nbytes = raf_decompress(&raf, me,
                                                     scratch, RAF_SCRATCH_MAX);
                    if (nbytes < 4) continue;

                    /* Route to BMP or ICN parser by magic/heuristic.
                     * DLL: ripLoadResource copies raw data and the caller
                     * (e.g. ripImageLoad) dispatches by file extension / magic. */
                    if (scratch[0] == 'B' && scratch[1] == 'M') {
                        rip_icon_cache_bmp_replace(&s->icon_state,
                                                   me->name, (int)strlen(me->name),
                                                   scratch, (int)nbytes);
                    } else if (nbytes >= 6) {
                        rip_cache_icn_if_valid(s, me->name, (int)strlen(me->name),
                                               scratch, (int)nbytes);
                    }
                }
                free(scratch);
            }
            /* raf.entries lives in the arena — no explicit free needed. */
        }
        rip_upload_reset(s);
        return;
    }
#endif /* RIPLIB_HAS_RAF */

    if (s->upload_name_overflow ||
        !rip_filename_is_safe(s->upload_name, s->upload_name_len)) {
        rip_upload_reset(s);
        return;
    }

    /* Try BMP first (check 'BM' magic) */
    if (s->upload_buf[0] == 'B' && s->upload_buf[1] == 'M') {
        rip_icon_cache_bmp_replace(&s->icon_state,
                                   s->upload_name, s->upload_name_len,
                                   s->upload_buf, s->upload_pos);
    } else {
        rip_cache_icn_if_valid(s, s->upload_name, s->upload_name_len,
                               s->upload_buf, s->upload_pos);
    }

    rip_upload_reset(s);
}

void rip_file_upload_begin(uint8_t name_len) {
    rip_file_upload_begin_state(g_rip_state, name_len);
}

void rip_file_upload_byte(uint8_t b) {
    rip_file_upload_byte_state(g_rip_state, b);
}

void rip_file_upload_end(void) {
    rip_file_upload_end_state(g_rip_state);
}

/* ══════════════════════════════════════════════════════════════════
 * PRE-PROCESSOR EXPRESSION EVALUATOR
 *
 * Used by the <<IF expr>> directive.  Expands $VARIABLE$ references in
 * the expression, then checks for comparison operators.
 *
 * DLL ground truth: ripTextVarEngine @ 0x026218 expands variables before
 * the pre-processor evaluates branch conditions — expand first, compare second.
 *
 * Operator precedence (first match wins):
 *   !=   — string inequality
 *   =    — string equality
 *   >    — integer greater-than
 *   <    — integer less-than
 *   (none) — boolean: non-empty and not literal "0"
 * ══════════════════════════════════════════════════════════════════ */

static bool eval_if_expr(rip_state_t *s, const char *expr) {
    char expanded[128];
    rip_expand_variables(s, expr, (int)strlen(expr), expanded, sizeof(expanded));

    /* Check 2-char operators (!=, >=, <=) before 1-char (=, >, <) so
     * that "5>=5" is parsed as ">=" rather than splitting on '='. */
    char *op = strstr(expanded, "!=");
    if (op) {
        *op = '\0';
        return strcmp(expanded, op + 2) != 0;  /* string inequality */
    }
    op = strstr(expanded, ">=");
    if (op) {
        *op = '\0';
        return atoi(expanded) >= atoi(op + 2);
    }
    op = strstr(expanded, "<=");
    if (op) {
        *op = '\0';
        return atoi(expanded) <= atoi(op + 2);
    }
    op = strchr(expanded, '=');
    if (op) {
        *op = '\0';
        return strcmp(expanded, op + 1) == 0;  /* string equality */
    }
    op = strchr(expanded, '>');
    if (op) {
        *op = '\0';
        return atoi(expanded) > atoi(op + 1);
    }
    op = strchr(expanded, '<');
    if (op) {
        *op = '\0';
        return atoi(expanded) < atoi(op + 1);
    }

    /* Boolean: non-empty and not literal "0" */
    return expanded[0] != '\0' && !(expanded[0] == '0' && expanded[1] == '\0');
}

static void preproc_restore_suppress(rip_state_t *s) {
    uint8_t idx;

    if (!s) return;
    if (s->preproc_overflow > 0) {
        s->preproc_suppress = true;
        return;
    }
    if (s->preproc_depth == 0) {
        s->preproc_suppress = false;
        return;
    }

    idx = (uint8_t)(s->preproc_depth - 1);
    s->preproc_suppress = s->preproc_parent_suppress[idx] ||
                          !s->preproc_branch_active[idx];
}

static void preproc_push_if(rip_state_t *s, const char *expr) {
    bool parent_suppress;
    bool branch_active = false;
    uint8_t idx;

    if (s->preproc_overflow > 0) {
        s->preproc_overflow++;
        s->preproc_suppress = true;
        return;
    }
    if (s->preproc_depth >= RIP_PREPROC_MAX_DEPTH) {
        s->preproc_overflow = 1;
        s->preproc_suppress = true;
        return;
    }

    parent_suppress = s->preproc_suppress;
    if (!parent_suppress)
        branch_active = eval_if_expr(s, expr);

    idx = s->preproc_depth++;
    s->preproc_parent_suppress[idx] = parent_suppress;
    s->preproc_branch_active[idx] = branch_active;
    s->preproc_branch_taken[idx] = branch_active;
    s->preproc_suppress = parent_suppress || !branch_active;
}

static void preproc_handle_else(rip_state_t *s) {
    uint8_t idx;

    if (s->preproc_depth == 0) return;
    if (s->preproc_overflow > 0) {
        s->preproc_suppress = true;
        return;
    }

    idx = (uint8_t)(s->preproc_depth - 1);
    if (s->preproc_parent_suppress[idx]) {
        s->preproc_branch_active[idx] = false;
    } else if (s->preproc_branch_taken[idx]) {
        s->preproc_branch_active[idx] = false;
    } else {
        s->preproc_branch_active[idx] = true;
        s->preproc_branch_taken[idx] = true;
    }
    s->preproc_suppress = s->preproc_parent_suppress[idx] ||
                          !s->preproc_branch_active[idx];
}

static void preproc_handle_endif(rip_state_t *s) {
    if (s->preproc_overflow > 0) {
        s->preproc_overflow--;
        preproc_restore_suppress(s);
        return;
    }
    if (s->preproc_depth == 0) return;

    s->preproc_depth--;
    preproc_restore_suppress(s);
}

static void preproc_finalize_directive(rip_state_t *s) {
    const char *dir;

    if (s->preproc_len < (int)sizeof(s->preproc_buf))
        s->preproc_buf[s->preproc_len] = '\0';
    else
        s->preproc_buf[sizeof(s->preproc_buf) - 1] = '\0';

    dir = s->preproc_buf;
    if (strncmp(dir, "IF ", 3) == 0 || strcmp(dir, "IF") == 0) {
        const char *expr = (dir[2] == ' ') ? dir + 3 : "";
        preproc_push_if(s, expr);
    } else if (strcmp(dir, "ELSE") == 0) {
        preproc_handle_else(s);
    } else if (strcmp(dir, "ENDIF") == 0) {
        preproc_handle_endif(s);
    } else if (strncmp(dir, "DEBUG ", 6) == 0 || strcmp(dir, "DEBUG") == 0) {
        /* §A2G2: <<DEBUG msg>> — push "0x3E DEBUG: <msg>\r" to TX so the
         * host can log it.  Suppressed by parent IF/ELSE branches so that
         * <<IF false>>...<<DEBUG ...>>...<<ENDIF>> stays quiet. */
        if (!s->preproc_suppress) {
            const char *msg = (dir[5] == ' ') ? dir + 6 : "";
            int msg_len = (int)strlen(msg);
            char buf[160];
            int n = snprintf(buf, sizeof(buf), "%cDEBUG: %s\r",
                             (char)0x3E, msg);
            (void)msg_len;
            if (n > 0 && n < (int)sizeof(buf))
                card_tx_push(buf, n);
        }
    }

    s->preproc_len = 0;
}

/* ══════════════════════════════════════════════════════════════════
 * COMMAND EXECUTION
 * ══════════════════════════════════════════════════════════════════ */

static void apply_draw_state(rip_state_t *s) {
    draw_set_color(s->palette[s->draw_color & 0x0F]);
    draw_set_write_mode(s->write_mode);
}

/* Unescape, variable-expand, justify, and render a text parameter at the
 * session's current draw position, advancing the cursor by the rendered
 * width.  Shared by RIP_TEXT ('T') and RIP_TEXT_XY ('@') so that both
 * commands honor the same backslash escapes, variable substitution,
 * justification flags, and BGI vs bitmap font selection. */
static void rip_render_text(rip_state_t *s, const char *raw, int raw_len) {
    char tbuf[256];
    char vbuf[256];
    int tlen;

    if (raw_len <= 0) return;
    tlen = unescape_text(raw, raw_len, tbuf);
    tlen = rip_expand_variables(s, tbuf, tlen, vbuf, sizeof(vbuf));
    if (tlen <= 0) return;

    uint8_t tc = s->palette[s->draw_color & 0x0F];
    uint8_t fid = s->font_id;
    uint8_t fscale = s->font_size ? s->font_size : 1;
    int16_t tx = s->draw_x, ty = s->draw_y;

    bool stroke = (fid > 0 && fid < BGI_FONT_COUNT && bgi_fonts_loaded &&
                   bgi_fonts[fid].strokes);
    int16_t tw = stroke
                 ? bgi_font_string_width(&bgi_fonts[fid], vbuf, tlen, fscale)
                 : (int16_t)(tlen * 8);
    int16_t th = stroke ? (int16_t)(bgi_fonts[fid].top * fscale) : 16;

    /* Apply horizontal/vertical justification (DLL: 0=left/bottom,
     * 1=center, 2=right/top, 3=baseline). */
    if (s->font_hjust == 1)      tx = (int16_t)(tx - tw / 2);
    else if (s->font_hjust == 2) tx = (int16_t)(tx - tw);
    if (s->font_vjust == 0)      ty = (int16_t)(ty - th);
    else if (s->font_vjust == 1) ty = (int16_t)(ty - th / 2);

    int16_t adv;
    if (stroke) {
        adv = bgi_font_draw_string_ex(&bgi_fonts[fid], tx, ty, vbuf, tlen,
                                       fscale, tc, s->font_dir, s->font_attrib);
    } else {
        draw_text(tx, ty, vbuf, tlen, cp437_8x16, 16, tc, 0xFF);
        adv = tw;
    }
    if (s->font_dir == 0) s->draw_x = (int16_t)(s->draw_x + adv);
    else                  s->draw_y = (int16_t)(s->draw_y + adv);
}

static void rip_render_text_box(rip_state_t *s,
                                int16_t x0, int16_t y0,
                                int16_t x1, int16_t y1,
                                uint8_t flags,
                                const char *raw, int raw_len) {
    draw_clip_state_t saved_clip;
    int16_t cx0;
    int16_t cy0;
    int16_t cx1;
    int16_t cy1;
    uint8_t old_hjust;
    uint8_t old_vjust;

    if (!s || !raw || raw_len <= 0)
        return;
    if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }

    draw_save_clip(&saved_clip);
    cx0 = x0 > saved_clip.x0 ? x0 : saved_clip.x0;
    cy0 = y0 > saved_clip.y0 ? y0 : saved_clip.y0;
    cx1 = x1 < saved_clip.x1 ? x1 : saved_clip.x1;
    cy1 = y1 < saved_clip.y1 ? y1 : saved_clip.y1;
    if (cx0 > cx1 || cy0 > cy1) {
        draw_restore_clip(&saved_clip);
        return;
    }

    old_hjust = s->font_hjust;
    old_vjust = s->font_vjust;

    s->font_hjust = 0;
    if (flags & 0x02u) s->font_hjust = 1;
    if (flags & 0x04u) s->font_hjust = 2;

    s->font_vjust = 2; /* top, matching a bounded text box default */
    if (flags & 0x10u) s->font_vjust = 1;
    if (flags & 0x20u) s->font_vjust = 2;
    if (flags & 0x40u) s->font_vjust = 3;

    if (s->font_hjust == 1)
        s->draw_x = (int16_t)(x0 + (x1 - x0 + 1) / 2);
    else if (s->font_hjust == 2)
        s->draw_x = (int16_t)(x1 + 1);
    else
        s->draw_x = x0;

    if (s->font_vjust == 0)
        s->draw_y = (int16_t)(y1 + 1);
    else if (s->font_vjust == 1)
        s->draw_y = (int16_t)(y0 + (y1 - y0 + 1) / 2);
    else
        s->draw_y = y0;

    draw_set_clip(cx0, cy0, cx1, cy1);
    rip_render_text(s, raw, raw_len);
    draw_restore_clip(&saved_clip);

    s->font_hjust = old_hjust;
    s->font_vjust = old_vjust;
}

static void apply_session_draw_state(rip_state_t *s) {
    int8_t card_pat = bgi_fill_to_card(s->fill_pattern);

    draw_set_clip(s->vp_x0, s->vp_y0, s->vp_x1, s->vp_y1);
    draw_set_pos(s->draw_x, s->draw_y);
    draw_set_line_style(s->line_style, s->line_thick);
    /* The 2nd arg becomes g_fill_color in drawing.c, used by fill_span
     * for the OFF bits of patterned fills.  Per BGI/RIP semantics that
     * is back_color (set by 'k'), with fill_color (set by 'S') used
     * for the ON bits via the foreground g_color.  See M14. */
    draw_set_fill_style((card_pat >= 0) ? (uint8_t)card_pat : 0,
                        s->palette[s->back_color & 0x0F]);
    apply_draw_state(s);
}

static bool rip_begin_filled_border(rip_state_t *s, uint8_t *saved_mode) {
    if (!s || !saved_mode || !s->filled_borders_enabled)
        return false;
    *saved_mode = s->write_mode;
    draw_set_write_mode(DRAW_MODE_COPY);
    draw_set_color(s->palette[s->draw_color & 0x0F]);
    return true;
}

static void rip_end_filled_border(rip_state_t *s, uint8_t saved_mode) {
    if (!s)
        return;
    draw_set_write_mode(saved_mode);
}

static void rip_reset_windows_state(rip_state_t *s, comp_context_t *c) {
    if (!s)
        return;

    /* Windows + viewport -> full defaults */
    s->tw_x0 = 0; s->tw_y0 = 0;
    s->tw_x1 = 639; s->tw_y1 = 349;
    s->tw_wrap = 0; s->tw_font_size = 0;
    s->tw_active = false;
    set_session_viewport(s, 0, 0, 639, 399);

    /* Drawing state -> defaults */
    s->draw_color = 15;
    s->draw_x = 0; s->draw_y = 0;
    s->write_mode = DRAW_MODE_COPY;
    s->line_style = 0; s->line_thick = 1;
    s->fill_pattern = 1; s->fill_color = 15;
    s->back_color = 0;
    s->font_id = 0; s->font_ext_id = 0;
    s->font_dir = 0; s->font_size = 1;
    s->font_hjust = 0; s->font_vjust = 0;
    s->font_attrib = 0;
    s->resolution_mode = 0;
    s->coordinate_size = 2;
    s->coordinate_res = 0;
    s->color_mode = 0;
    s->color_bits = 0;
    s->filled_borders_enabled = true;

    for (int i = 0; i < 16; i++) {
        s->palette[i] = palette_slot(i);
        palette_write_rgb565(palette_slot(i), ega_default_rgb565[i]);
    }

    s->num_mouse_regions = 0;
    memset(&s->button_style, 0, sizeof(s->button_style));
    s->clipboard.valid = false;
    s->clipboard.width = 0;
    s->clipboard.height = 0;
    memset(s->icon_slot_valid, 0, sizeof(s->icon_slot_valid));
    s->icon_style_active = false;
    s->icon_style_style = 0;
    s->icon_style_align = 0;
    s->icon_style_scale = 0;
    s->text_block.active = false;

    s->rip_has_drawn = false;
    s->cursor_repositioned = false;
    /* §A2G2: clear the state stack so subsequent | ~ pops cannot
     * surface state from a prior scene. */
    s->state_stack_depth = 0;
    draw_set_write_mode(DRAW_MODE_COPY);
    draw_set_line_style(0xFF, 1);
    draw_set_fill_style(0, s->palette[s->back_color]);
    draw_set_color(s->palette[s->draw_color & 0x0F]);
    draw_fill_screen(s->palette[s->back_color]);
    if (c)
        comp_set_cursor(c, 0, 0);
}

static void execute_rip_command(rip_state_t *s, void *ctx) {
    comp_context_t *c = (comp_context_t *)ctx;
    const char *p = s->cmd_buf;
    int len = s->cmd_len;

    /* Mark that RIP commands have drawn — prevents ANSI ESC[2J fallback
     * from clearing the framebuffer after the menu is rendered. */
    s->rip_has_drawn = true;

    /* Apply current drawing state */
    apply_draw_state(s);

    if (s->is_level3) {
        /* Level 3 commands (prefixed with '3') — RIPscrip 3.0 extensions.
         * DLL command table: 5 entries at level 3 (entries 125-129 of 129
         * total).  Command letters and argument counts are not publicly
         * documented and no real-world BBS is known to send them.
         *
         * INTENTIONAL: accept and silently discard so a stray Level 3
         * command does not poison the stream and trigger ERROR_RECOVERY,
         * which would consume the next several !|... frames as resync. */
        (void)s->cmd_char;
        return;
    }

    if (s->is_level2) {
        /* Level 2 commands (prefixed with '2') — RIPscrip 2.0 / port extensions.
         * Pass both the raw buffer (for port commands that need 1-digit/4-digit
         * MegaNum fields) and the pre-decoded mega2 params (for legacy handlers). */
        int16_t params[16];
        int nparams = 0;
        for (int i = 0; i + 1 < len && nparams < 16; i += 2)
            params[nparams++] = mega2(p + i);
        ripscrip2_execute(&s->rip2_state, s, ctx, s->cmd_char,
                          p, len, params, nparams);
        return;
    }

    if (s->is_level1) {
        /* Level 1 commands (prefixed with '1') */
        switch (s->cmd_char) {

        /* ── Mouse regions ─────────────────────────────────────── */
        case 'K': /* RIP_KILL_MOUSE — clear all mouse regions */
            s->num_mouse_regions = 0;
            break;
        case 'M': /* RIP_MOUSE — define mouse region
                   * Params: num:2, x0:2, y0:2, x1:2, y1:2, hotkey:2, flags:1, res:4, text
                   * (12 bytes of fixed params, then text for host command)
                   *
                   * DLL field record layout (rip_mouse.c +0x20 flags byte):
                   *   flags bit 0 → MF_SEND_CHAR(0x08)
                   *   flags bit 1 → MF_RADIO(0x20)
                   *   flags bit 2 → MF_TOGGLE(0x40)
                   *   always set  → MF_ACTIVE(0x04)  — DLL: *pFlagByte |= MF_ACTIVE */
            if (len >= 13 && s->num_mouse_regions < RIP_MAX_MOUSE_REGIONS) {
                rip_mouse_region_t *r = &s->mouse_regions[s->num_mouse_regions];
                r->x0 = mega2(p + 2);  r->y0 = scale_y(mega2(p + 4));
                r->x1 = mega2(p + 6);  r->y1 = scale_y1(mega2(p + 8));
                r->hotkey = (uint8_t)(mega2(p + 10) & 0xFF); /* DLL: hotkey at +0x2B */
                /* Translate RIPscrip flags (spec bits 0-2) to DLL flag byte bits */
                {
                    int spec_flags = mega_digit(p[12]);
                    r->flags = RIP_MF_ACTIVE;                            /* always active */
                    if (spec_flags & 1) r->flags |= RIP_MF_SEND_CHAR;   /* send hotkey char */
                    if (spec_flags & 2) r->flags |= RIP_MF_RADIO;       /* radio group */
                    if (spec_flags & 4) r->flags |= RIP_MF_TOGGLE;      /* toggle */
                }
                /* Skip reserved bytes (spec says 4 reserved after flags digit) */
                int text_start = 17; /* num:2 + x0:2 + y0:2 + x1:2 + y1:2 + hotkey:2 + flags:1 + res:4 */
                int tlen = len - text_start;
                if (tlen < 0) tlen = 0;
                if (tlen > 127) tlen = 127;
                if (tlen > 0) memcpy(r->text, p + text_start, (size_t)tlen);
                r->text_len = (uint8_t)tlen;
                r->icon_path[0] = '\0';
                r->active = true;
                r->hover  = false;
                s->num_mouse_regions++;
            }
            break;

        /* ── Button style + buttons ────────────────────────────── */
        case 'B': /* RIP_BUTTON_STYLE — define style for subsequent buttons.
                   * v1.54 spec: !|1B = RIP_BUTTON_STYLE. The DLL internal function
                   * name (ripCmd_Button) is misleading — the command letter is 'B'.
                   *
                   * wid:2 hgt:2 orient:2 flags:4 bevsize:2 dfore:2 dback:2
                   * bright:2 dark:2 surface:2 grp_no:2 flags2:2 uline:2 corner:2 res:6
                   * Total: 36 chars (30 meaningful + 6 reserved) */
            if (len >= 30) {
                rip_button_style_t *bs = &s->button_style;
                bs->width      = mega2(p);
                bs->height     = mega2(p + 2);
                bs->orient     = (uint8_t)mega2(p + 4);
                bs->flags      = (uint16_t)mega4(p + 6); /* 4-digit MegaNum */
                bs->bev_size   = (uint8_t)mega2(p + 10);
                bs->dfore      = (uint8_t)(mega2(p + 12) & 0x0F);
                bs->dback      = (uint8_t)(mega2(p + 14) & 0x0F);
                bs->bright     = (uint8_t)(mega2(p + 16) & 0x0F);
                bs->dark       = (uint8_t)(mega2(p + 18) & 0x0F);
                bs->surface    = (uint8_t)(mega2(p + 20) & 0x0F);
                bs->grp_no     = (uint8_t)mega2(p + 22);
                bs->flags2     = (uint16_t)mega2(p + 24);
                bs->uline_col  = (uint8_t)(mega2(p + 26) & 0x0F);
                bs->corner_col = (uint8_t)(mega2(p + 28) & 0x0F);
            }
            break;
        case 'U': /* RIP_BUTTON — create button instance (draw + register mouse region).
                   * v1.54 spec: !|1U = RIP_BUTTON. The DLL internal function
                   * name (ripCmd_MouseRegion) is misleading — the command letter is 'U'.
                   * x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:1 res:1 text
                   * text format: host_command<>display_label (or just text for both) */
            if (len >= 12) {
                int16_t bx0 = mega2(p), by0 = scale_y(mega2(p + 2));
                int16_t bx1 = mega2(p + 4), by1 = scale_y1(mega2(p + 6));
                rip_button_style_t *bs = &s->button_style;
                uint8_t surf = s->palette[bs->surface & 0x0F];
                uint8_t hi   = s->palette[bs->bright & 0x0F];
                uint8_t dk   = s->palette[bs->dark & 0x0F];
                int16_t bw = (int16_t)(bx1 - bx0 + 1);
                int16_t bh = (int16_t)(by1 - by0 + 1);
                int16_t bev = bs->bev_size ? (int16_t)bs->bev_size : 2;

                /* Surface fill */
                draw_set_color(surf);
                draw_rect(bx0, by0, bw, bh, true);

                /* Bevel rendering — corrected corner geometry (Fix 5).
                 * Each pass draws one pixel inset from the last.  Adjacent
                 * horizontal and vertical segments must not share a corner pixel
                 * or the wrong color bleeds into the corner.  Convention:
                 *   Highlight owns top-left corners; shadow owns bottom-right.
                 *
                 *   Highlight top:  x=[bx0+i .. bx1-i],     y=by0+i
                 *   Highlight left: x=bx0+i, y=[by0+i+1 .. by1-i-1]  (skip top corner)
                 *   Shadow bottom:  x=[bx0+i+1 .. bx1-i],   y=by1-i  (skip left corner)
                 *   Shadow right:   x=bx1-i, y=[by0+i+1 .. by1-i-1]  (skip top corner)
                 */
                draw_set_color(hi);
                for (int16_t i = 0; i < bev; i++) {
                    draw_hline((int16_t)(bx0 + i), (int16_t)(by0 + i),
                               (int16_t)(bw - 2 * i - 1)); /* top, full */
                    draw_vline((int16_t)(bx0 + i), (int16_t)(by0 + i + 1),
                               (int16_t)(bh - 2 * i - 2)); /* left, skip top corner */
                }
                /* Shadow (bottom + right) */
                draw_set_color(dk);
                for (int16_t i = 0; i < bev; i++) {
                    draw_hline((int16_t)(bx0 + i + 1), (int16_t)(by1 - i),
                               (int16_t)(bw - 2 * i - 1)); /* bottom, skip left corner */
                    draw_vline((int16_t)(bx1 - i), (int16_t)(by0 + i + 1),
                               (int16_t)(bh - 2 * i - 2)); /* right, skip top corner */
                }

                /* Parse text: icon_file<>text_label<>host_command
                 * Per RIPscrip 1.54 spec, three segments separated by <>:
                 *   [icon][<>label[<>host_cmd]]
                 * If no icon (plain button): <>label<>host or <>label */
                int text_off = 12; /* after x0:2+y0:2+x1:2+y1:2+hotkey:2+flags:1+res:1 */
                const char *text_raw = p + text_off;
                int text_len = len - text_off;
                /* Find all <> separators (up to 2) */
                int sep[2] = {-1, -1};
                int nsep = 0;
                for (int i = 0; i + 1 < text_len && nsep < 2; i++) {
                    if (text_raw[i] == '<' && text_raw[i + 1] == '>') {
                        sep[nsep++] = i;
                        i++; /* skip '>' */
                    }
                }
                /* Extract label and host command from segments */
                const char *icon_text = text_raw;
                int icon_len = (nsep > 0) ? sep[0] : text_len;
                const char *label_text = text_raw;
                int label_len = text_len;
                const char *host_text = NULL;
                int host_len = 0;
                if (nsep == 2) {
                    /* icon<>label<>host */
                    label_text = text_raw + sep[0] + 2;
                    label_len = sep[1] - sep[0] - 2;
                    host_text = text_raw + sep[1] + 2;
                    host_len = text_len - sep[1] - 2;
                } else if (nsep == 1) {
                    /* icon<>label (no host) or label<>host */
                    label_text = text_raw + sep[0] + 2;
                    label_len = text_len - sep[0] - 2;
                } else {
                    /* No separator — text is both icon/label */
                    label_text = text_raw;
                    label_len = text_len;
                }
                /* Icon lookup and blit */
                bool icon_drawn = false;
                int16_t label_y = (int16_t)(by0 + (bh - 16) / 2); /* default: label centered */
                if (icon_len > 0) {
                    char icon_name[RIP_FILE_NAME_MAX + 1];
                    int nlen = icon_len < RIP_FILE_NAME_MAX ? icon_len : RIP_FILE_NAME_MAX;
                    memcpy(icon_name, icon_text, (size_t)nlen);
                    icon_name[nlen] = '\0';

                    rip_icon_t icon;
                    if (rip_icon_lookup(&s->icon_state, icon_name, nlen, &icon)) {
                        int16_t ix = (int16_t)(bx0 + (bw - (int16_t)icon.width) / 2);
                        int16_t iy = (int16_t)(by0 + (bh - (int16_t)icon.height) / 2);
                        if (label_len > 0) {
                            /* Icon in upper half; label drawn below */
                            iy = (int16_t)(by0 + bev + 2);
                            label_y = (int16_t)(iy + (int16_t)icon.height + 2);
                        }
                        draw_restore_region(ix, iy, (int16_t)icon.width,
                                            (int16_t)icon.height, icon.pixels);
                        icon_drawn = true;
                    } else {
                        /* Icon not in cache — queue request for file transfer */
                        rip_icon_request_file(&s->icon_state, icon_name, nlen);
                    }
                }
                (void)icon_drawn;

                /* Draw display label centered (or below icon if icon present) */
                if (label_len > 0) {
                    char lbuf[128];
                    int llen = unescape_text(label_text, label_len > 127 ? 127 : label_len, lbuf);
                    if (llen > 0) {
                        uint8_t tc = s->palette[bs->dfore & 0x0F];
                        int16_t tx = (int16_t)(bx0 + (bw - llen * 8) / 2);
                        draw_text(tx, label_y, lbuf, llen, cp437_8x16, 16u, tc, 0xFF);
                    }
                }
                /* Register mouse region for button click.
                 * DLL: ripCmd_MouseRegion always sets MF_ACTIVE(0x04) on the field
                 * record (rip_mouse.c: *pFlagByte |= MF_ACTIVE).
                 * Buttons use plain string send (not MF_SEND_CHAR). */
                if (host_len > 0 && s->num_mouse_regions < RIP_MAX_MOUSE_REGIONS) {
                    rip_mouse_region_t *r = &s->mouse_regions[s->num_mouse_regions];
                    r->x0 = bx0; r->y0 = by0; r->x1 = bx1; r->y1 = by1;
                    r->flags = RIP_MF_ACTIVE; /* always active per DLL ground truth */
                    r->hotkey = 0;
                    r->icon_path[0] = '\0';
                    r->hover = false;
                    if (host_len > 127) host_len = 127;
                    memcpy(r->text, host_text, (size_t)host_len);
                    r->text_len = (uint8_t)host_len;
                    r->active = true;
                    s->num_mouse_regions++;
                }
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
            break;

        /* ── Clipboard (GET_IMAGE / PUT_IMAGE) ─────────────────── */
        case 'C': /* RIP_GET_IMAGE — copy screen region to clipboard
                   * x0:2 y0:2 x1:2 y1:2 res:1 */
            if (len >= 9) {
                int16_t gx0 = mega2(p), gy0 = scale_y(mega2(p + 2));
                int16_t gx1 = mega2(p + 4), gy1 = scale_y1(mega2(p + 6));
                int16_t gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1;
                (void)rip_clipboard_capture(s, gx0, gy0, gw, gh);
            }
            break;
        case 'P': /* RIP_PUT_IMAGE — paste clipboard to screen
                   * x:2 y:2 mode:2 res:1 */
            if (len >= 5 && s->clipboard.valid && s->clipboard.data) {
                int16_t px = mega2(p), py = scale_y(mega2(p + 2));
                /* mode at p+4: 0=COPY, 1=XOR, 2=OR, 3=AND, 4=NOT */
                uint8_t mode = (len >= 6) ? mega2(p + 4) : 0;
                if (mode > 4) mode = 0;
                rip_blit_pixels(s, px, py, s->clipboard.data,
                                (uint16_t)s->clipboard.width,
                                (uint16_t)s->clipboard.height,
                                s->clipboard.width, s->clipboard.height, mode);
            }
            break;

        /* ── Text blocks (BEGIN_TEXT / REGION_TEXT / END_TEXT) ──── */
        case 'T': /* RIP_BEGIN_TEXT — define text region
                   * x0:2 y0:2 x1:2 y1:2 res:2 */
            if (len >= 10) {
                s->text_block.x0 = mega2(p);
                s->text_block.y0 = scale_y(mega2(p + 2));
                s->text_block.x1 = mega2(p + 4);
                s->text_block.y1 = scale_y1(mega2(p + 6));
                s->text_block.cur_y = s->text_block.y0;
                s->text_block.active = true;
            }
            break;
        case 't': /* RIP_REGION_TEXT — one line in text block (Level 1).
                   * justify:1, text.
                   * L16: previously passed NULL font to draw_text, which made
                   * draw_text early-return — every byte in the bitmap path
                   * was silently dropped.  Also missed $variable expansion.
                   * Now matches the Level 0 't' code path. */
            if (s->text_block.active && len >= 1) {
                int tstart = 1;
                if (tstart < len) {
                    char tbuf[256];
                    char vbuf[256];
                    int tlen = unescape_text(p + tstart, len - tstart, tbuf);
                    tlen = rip_expand_variables(s, tbuf, tlen, vbuf, sizeof(vbuf));
                    uint8_t tc = s->palette[s->draw_color & 0x0F];
                    draw_text(s->text_block.x0, s->text_block.cur_y,
                              vbuf, tlen, cp437_8x16, 16, tc, 0xFF);
                    s->text_block.cur_y += 16;
                }
            }
            break;
        case 'E': /* RIP_END_TEXT */
            s->text_block.active = false;
            break;

        /* ── Region copy ───────────────────────────────────────── */
        case 'G': /* RIP_COPY_REGION — x0:2 y0:2 x1:2 y1:2 res:2 dest_x:2 dest_y:2
                   * DLL ground truth (ripscrip_text.asm): 8 args — full source rect
                   * plus independent destination coordinates (dest_x, dest_y).
                   * Previous code hardcoded dest_x = rx0 (source X); fixed here. */
            if (len >= 14) {
                int16_t rx0 = mega2(p), ry0 = scale_y(mega2(p + 2));
                int16_t rx1 = mega2(p + 4), ry1 = scale_y1(mega2(p + 6));
                /* p+8: res (2 chars, skip) */
                int16_t dest_x = mega2(p + 10);             /* destination X */
                int16_t dest_y = scale_y(mega2(p + 12));    /* destination Y */
                int16_t rw = rx1 - rx0 + 1, rh = ry1 - ry0 + 1;
                if (rw > 0 && rh > 0)
                    draw_copy_rect(rx0, ry0, dest_x, dest_y, rw, rh);
            }
            break;

        /* ── Icon loading ──────────────────────────────────────── */
        case 'I': /* RIP_LOAD_ICON — x:2 y:2 mode:2 clipboard:1 res:2 filename */
            if (len >= 9) {
                int16_t ix = mega2(p), iy = scale_y(mega2(p + 2));
                uint8_t mode = (uint8_t)(mega2(p + 4) & 0xFF);
                bool copy_to_clipboard = (mega_digit(p[6]) != 0);
                /* mode at p+4, clipboard at p+6, res at p+7:8 */
                int fname_start = 9;
                int fname_len = len - fname_start;
                if (fname_len > 0) {
                    const char *path = p + fname_start;
                    if (!rip_filename_is_safe(path, fname_len))
                        break;
                    if (mode > DRAW_MODE_NOT)
                        mode = DRAW_MODE_COPY;

                    rip_icon_t icon;
                    if (rip_icon_lookup(&s->icon_state, path, fname_len, &icon)) {
                        rip_draw_icon_pixels(s, ix, iy, icon.pixels,
                                             icon.width, icon.height,
                                             0, 0, mode);
                        if (copy_to_clipboard)
                            (void)rip_clipboard_store_pixels(s, icon.pixels,
                                                             icon.width,
                                                             icon.height);
                    } else {
                        /* Icon not found — queue file request + draw placeholder */
                        rip_icon_request_file(&s->icon_state, path, fname_len);
                        draw_set_color(s->palette[8]);
                        draw_rect(ix, iy, 32, 32, false);
                        draw_set_color(s->palette[s->draw_color & 0x0F]);
                    }
                }
            }
            break;
        case 'W': /* RIP_WRITE_ICON — cache current clipboard under a filename */
            if (len > 0 && s->clipboard.valid && s->clipboard.data) {
                const char *name = p;
                int name_len = len;
                if (name_len > 2 && p[0] == '0' && p[1] == '0') {
                    name = p + 2;
                    name_len -= 2;
                }
                (void)rip_cache_clipboard_as_icon(s, name, name_len, NULL);
            }
            break;

        /* ── Audio playback commands ───────────────────────────── */
        case 'A': /* RIP_PLAY_AUDIO — play audio file.
                   * DLL: calls ripAudioPlay() which invokes CB_PLAY_SOUND.
                   * Format: mode:2 res:2 filename (free text, pipe-terminated).
                   * A2GSPU: push CMD_PLAY_SOUND marker (0x3D) + filename + NUL
                   * via TX FIFO; IIgs bridge loop dispatches to DOC sound chip. */
            if (len >= 4) {
                const char *fname = p + 4;
                int fname_len = len - 4;
                if (fname_len > 0 && fname_len <= 64) {
                    char snd_buf[70];
                    snd_buf[0] = (char)0x3D; /* CMD_PLAY_SOUND marker */
                    int copy = fname_len < 68 ? fname_len : 68;
                    memcpy(snd_buf + 1, fname, (size_t)copy);
                    snd_buf[1 + copy] = '\0';
                    card_tx_push(snd_buf, 2 + copy);
                }
            }
            break;

        case 'Z': /* RIP_PLAY_MIDI — MIDI file playback.
                   * DLL ground truth: sends to the Windows MIDI sequencer via
                   * CB_PLAY_MIDI.  Format: mode:2 res:2 filename.
                   * A2GSPU: same TX FIFO path as RIP_PLAY_AUDIO. */
            if (len >= 4) {
                const char *fname = p + 4;
                int fname_len = len - 4;
                if (fname_len > 0 && fname_len <= 64) {
                    char snd_buf[70];
                    snd_buf[0] = (char)0x3D; /* CMD_PLAY_SOUND marker */
                    int copy = fname_len < 68 ? fname_len : 68;
                    memcpy(snd_buf + 1, fname, (size_t)copy);
                    snd_buf[1 + copy] = '\0';
                    card_tx_push(snd_buf, 2 + copy);
                }
            }
            break;

        /* ── Image display style ────────────────────────────────── */
        case 'S': /* RIP_IMAGE_STYLE — set icon/image display mode.
                   * DLL GFXSTYLE.imageStyle field (rip_defaults.c).
                   * Format: mode:2 (0=stretch, 1=tile, 2=center, 3=proportional)
                   * Stored in s->image_style; honoured by subsequent icon blits. */
            if (len >= 2)
                s->image_style = (uint8_t)(mega2(p) & 0x03);
            break;

        /* ── Icon search path ────────────────────────────────────── */
        case 'N': /* RIP_SET_ICON_DIR — set icon search directory.
                   * DLL stores in RIPINST.IconPath (rip_instance.c init cascade
                   * step 11, "IconPath" string at 0x10026218 region).
                   * Format: res:2 path (free text).
                   * A2GSPU: store the path tag; consult on future icon requests. */
            if (len >= 2) {
                int plen = len - 2;
                if (plen >= (int)sizeof(s->icon_dir))
                    plen = (int)sizeof(s->icon_dir) - 1;
                memcpy(s->icon_dir, p + 2, (size_t)plen);
                s->icon_dir[plen] = '\0';
            }
            break;

        /* ── Font loading ───────────────────────────────────────── */
        case 'O': /* RIP_FONT_LOAD — load BGI/RFF font from file.
                   * DLL: loads font into the per-instance font table.
                   * A2GSPU: built-in BGI fonts are pre-compiled in flash.
                   * If the requested CHR name matches one, make it active;
                   * otherwise queue a file request for the host side. */
            if (len > 0) {
                int off = 0;
                int fid;
                if (len > 2 && p[0] == '0' && p[1] == '0')
                    off = 2;
                fid = rip_font_id_from_name(p + off, len - off);
                if (fid >= 0 && fid < BGI_FONT_COUNT) {
                    s->font_id = (uint8_t)fid;
                    s->font_ext_id = (uint8_t)fid;
                } else if (rip_filename_is_safe(p + off, len - off)) {
                    rip_icon_request_file(&s->icon_state, p + off, len - off);
                }
            }
            break;

        /* ── Extended query routing ─────────────────────────────── */
        case 'Q': /* RIP_QUERY_EXT — extended query command.
                   * DLL: routes to the same handler as the ESC-char (0x1B)
                   * QUERY command but with extended flags.  Format: flags:3 res:2 varname.
                   * A2GSPU: route to the same handler inline — identical logic. */
            if (len >= 5) {
                const char *vname = p + 5;
                int vlen = len - 5;
                char resp[64];
                int rlen = 0;
                /* $APPn$ query — mirrors the ESC-char QUERY case above */
                if (vlen >= 6 && vname[0] == '$' && vname[1] == 'A' &&
                    vname[2] == 'P' && vname[3] == 'P' &&
                    vname[4] >= '0' && vname[4] <= '9' && vname[5] == '$') {
                    int idx = vname[4] - '0';
                    rlen = (int)rip_strnlen(s->app_vars[idx], sizeof(s->app_vars[0]));
                    if (rlen > 0)
                        card_tx_push(s->app_vars[idx], rlen);
                    else
                        (void)rip_query_prompt_begin(s, vname, vlen);
                } else {
                    char key[RIP_USER_VAR_NAME_MAX + 1];
                    if (rip_var_name_copy(vname, vlen, key, sizeof(key))) {
                        int uidx = rip_user_var_find(s, key, (int)strlen(key));
                        if (uidx >= 0) {
                            rlen = (int)rip_strnlen(s->user_var_values[uidx],
                                                    sizeof(s->user_var_values[uidx]));
                            if (rlen > 0)
                                card_tx_push(s->user_var_values[uidx], rlen);
                        } else {
                            (void)rip_query_prompt_begin(s, vname, vlen);
                        }
                    } else {
                        (void)resp; (void)rlen;
                        card_tx_push("\r", 1);
                    }
                }
            }
            break;

        /* ── Extended viewport with scale ───────────────────────── */
        case 'V': /* RIP_SET_VIEWPORT_EXT — viewport + scale factor.
                   * DLL: sets both the clipping rectangle and a zoom/scale
                   * factor for subsequent coordinate calculations.
                   * Format: x0:2 y0:2 x1:2 y1:2 scale:1
                   * A2GSPU: set viewport rect (same as 'v'), store scale field.
                   * The fixed 640×400 framebuffer cannot actually scale, but
                   * we store the field so future capability queries are correct. */
            if (len >= 9) {
                int16_t vx0 = mega2(p), vy0 = mega2(p + 2);
                int16_t vx1 = mega2(p + 4), vy1 = mega2(p + 6);
                clamp_ega_rect(&vx0, &vy0, &vx1, &vy1);
                set_session_viewport(s, vx0, scale_y(vy0),
                                        vx1, scale_y1(vy1));
                s->viewport_scale = (uint8_t)mega_digit(p[8]);
            }
            break;

        /* ── Extended clipboard operations ──────────────────────── */
        case 'X': /* RIP_CLIPBOARD_OP — extended clipboard operations.
                   * DLL: provides compound clipboard operations (blend, mask,
                   * flip, rotate) beyond the basic GET/PUT_IMAGE pair.
                   * Format: op:2 [params vary by op].
                   * Embedded fallback ops:
                   *   0=clear, 1=flip horizontal, 2=flip vertical,
                   *   3=rotate 180, 4=invert palette bytes,
                   *   5=capture x0/y0/x1/y1, 6=paste x/y/mode. */
            if (len >= 2) {
                uint8_t op = (uint8_t)(mega2(p) & 0xFF);
                if (op == 0) {
                    s->clipboard.valid = false;
                    s->clipboard.width = 0;
                    s->clipboard.height = 0;
                } else if (s->clipboard.valid && s->clipboard.data &&
                           op >= 1 && op <= 4) {
                    int16_t cw = s->clipboard.width;
                    int16_t ch = s->clipboard.height;
                    if (op == 1 || op == 3) {
                        for (int16_t yy = 0; yy < ch; yy++) {
                            uint8_t *row = s->clipboard.data + (size_t)yy * (uint16_t)cw;
                            for (int16_t xx = 0; xx < cw / 2; xx++) {
                                uint8_t t = row[xx];
                                row[xx] = row[cw - 1 - xx];
                                row[cw - 1 - xx] = t;
                            }
                        }
                    }
                    if (op == 2 || op == 3) {
                        for (int16_t yy = 0; yy < ch / 2; yy++) {
                            uint8_t *top = s->clipboard.data + (size_t)yy * (uint16_t)cw;
                            uint8_t *bot = s->clipboard.data + (size_t)(ch - 1 - yy) * (uint16_t)cw;
                            for (int16_t xx = 0; xx < cw; xx++) {
                                uint8_t t = top[xx];
                                top[xx] = bot[xx];
                                bot[xx] = t;
                            }
                        }
                    }
                    if (op == 4) {
                        size_t n = (size_t)(uint16_t)cw * (size_t)(uint16_t)ch;
                        for (size_t i = 0; i < n; i++)
                            s->clipboard.data[i] = (uint8_t)~s->clipboard.data[i];
                    }
                } else if (op == 5 && len >= 10) {
                    int16_t x0 = mega2(p + 2);
                    int16_t y0 = scale_y(mega2(p + 4));
                    int16_t x1 = mega2(p + 6);
                    int16_t y1 = scale_y1(mega2(p + 8));
                    (void)rip_clipboard_capture(s, x0, y0,
                                                (int16_t)(x1 - x0 + 1),
                                                (int16_t)(y1 - y0 + 1));
                } else if (op == 6 && len >= 8 &&
                           s->clipboard.valid && s->clipboard.data) {
                    int16_t x = mega2(p + 2);
                    int16_t y = scale_y(mega2(p + 4));
                    uint8_t mode = (uint8_t)(mega2(p + 6) & 0xFF);
                    if (mode > DRAW_MODE_NOT) mode = DRAW_MODE_COPY;
                    rip_blit_pixels(s, x, y, s->clipboard.data,
                                    (uint16_t)s->clipboard.width,
                                    (uint16_t)s->clipboard.height,
                                    s->clipboard.width, s->clipboard.height,
                                    mode);
                }
            }
            break;

        /* ── Scene / file operations (no filesystem) ──────────── */
        case 'R': /* RIP_READ_SCENE — request scene file from host side */
            if (len > 0 && rip_filename_is_safe(p, len))
                rip_icon_request_file(&s->icon_state, p, len);
            break;
        case 'F': /* RIP_FILE_QUERY — mode:2 res:4 filename
                   * E5: DLL extended response format (rip_images.c):
                   *   "1 filename size timestamp\r"  (file present, with metadata)
                   *   "1\r"                          (file present, no metadata)
                   *   "0\r"                          (file absent)
                   * size is decimal pixel-data byte count; timestamp 0 (unknown). */
            if (len >= 6) {
                int mode = mega2(p);
                /* Null-terminate filename for response formatting */
                char fname_buf[64];
                const char *fname = p + 6;
                int flen = len - 6;
                if (flen <= 0) break;
                if (!rip_filename_is_safe(fname, flen)) {
                    card_tx_push("0\r", 2);
                    break;
                }
                if (flen > (int)sizeof(fname_buf) - 1)
                    flen = (int)sizeof(fname_buf) - 1;
                memcpy(fname_buf, fname, (size_t)flen);
                fname_buf[flen] = '\0';

                rip_icon_t icon;
                if (rip_icon_lookup(&s->icon_state, fname, flen, &icon)) {
                    /* E5: Extended response — include pixel-data size when available.
                     * width * height is the raw 8bpp pixel byte count, which matches
                     * what the BBS uses to decide whether to re-send the file. */
                    if (icon.width > 0 && icon.height > 0) {
                        char resp[128];
                        unsigned long file_size = (unsigned long)icon.width * icon.height;
                        snprintf(resp, sizeof(resp), "1 %s %lu 0\r",
                                 fname_buf, file_size);
                        card_tx_push(resp, (int)strlen(resp));
                    } else {
                        /* Icon present but no dimension metadata — abbreviated form */
                        card_tx_push("1\r", 2);
                    }
                } else {
                    card_tx_push("0\r", 2);
                    rip_icon_request_file(&s->icon_state, fname, flen);
                }
                (void)mode; /* mode byte reserved for future size/date filtering */
            }
            break;

        /* ── Text variables (DEFINE / QUERY) ──────────────────── */
        case 'D': /* RIP_DEFINE — define text variable
                   * flags:3 res:2 text (format: varname[,width]:?prompt?[default])
                   * For $APP0$-$APP9$: store default value in app_vars[].
                   * For other variables: display the prompt/default as text. */
            if (len > 0) {
                /* Skip flags:3 + res:2, process remaining text */
                int tstart = (len >= 5) ? 5 : 0;
                if (tstart < len) {
                    char tbuf[128];
                    int tlen = unescape_text(p + tstart, len - tstart > 127 ? 127 : len - tstart, tbuf);
                    int eq = -1;
                    bool handled_define = false;
                    for (int i = 0; i < tlen; i++) {
                        if (tbuf[i] == '=') { eq = i; break; }
                        if (tbuf[i] == '?') break;
                    }
                    if (eq > 0) {
                        handled_define = rip_user_var_set(s, tbuf, eq,
                                                          tbuf + eq + 1,
                                                          tlen - eq - 1);
                    }
                    /* Find default value between last pair of ? marks */
                    const char *display = tbuf;
                    int dlen = tlen;
                    /* Look for ?prompt?default — extract default */
                    int q1 = -1, q2 = -1;
                    for (int i = 0; i < tlen; i++) {
                        if (tbuf[i] == '?') {
                            if (q1 < 0) q1 = i;
                            else { q2 = i; break; }
                        }
                    }
                    if (q2 > 0 && q2 + 1 < tlen) {
                        display = tbuf + q2 + 1;
                        dlen = tlen - q2 - 1;
                    }

                    /* L18: Only run the legacy "$APPn$:?prompt?default" /
                     * "$NAME$" parsing if the "name=value" path above didn't
                     * already store the variable.  Without this guard the
                     * $APPn$ branch would re-overwrite app_vars[idx] with
                     * the raw, unparsed "$APPn$=value" string. */
                    int colon = -1;
                    for (int i = 0; i < tlen; i++) {
                        if (tbuf[i] == ':' || tbuf[i] == '?') { colon = i; break; }
                    }
                    int name_end = (colon >= 0) ? colon : tlen;
                    if (!handled_define) {
                        if (name_end >= 6 && tbuf[0] == '$' && tbuf[1] == 'A' &&
                            tbuf[2] == 'P' && tbuf[3] == 'P' &&
                            tbuf[4] >= '0' && tbuf[4] <= '9' && tbuf[5] == '$') {
                            /* Store default value into app_vars[idx] */
                            int idx = tbuf[4] - '0';
                            int vlen = dlen < 31 ? dlen : 31;
                            memcpy(s->app_vars[idx], display, (size_t)vlen);
                            s->app_vars[idx][vlen] = '\0';
                            handled_define = true;
                        } else if (name_end >= 3 && tbuf[0] == '$' &&
                                   tbuf[name_end - 1] == '$') {
                            handled_define = rip_user_var_set(s, tbuf, name_end,
                                                              display, dlen);
                        }
                    }
                    if (!handled_define) {
                        char rawbuf[128];
                        int rawlen = unescape_text(p, len > 127 ? 127 : len,
                                                   rawbuf);
                        int raw_eq = -1;
                        for (int i = 0; i < rawlen; i++) {
                            if (rawbuf[i] == '=') { raw_eq = i; break; }
                            if (rawbuf[i] == '?') break;
                        }
                        if (raw_eq > 0) {
                            handled_define = rip_user_var_set(s, rawbuf, raw_eq,
                                                              rawbuf + raw_eq + 1,
                                                              rawlen - raw_eq - 1);
                        }
                    }
                    if (!handled_define) {
                        /* L17: same NULL-font silent-drop bug as L14/L16.
                         * Render the prompt with cp437_8x16. */
                        if (dlen > 0) {
                            uint8_t tc = s->palette[s->draw_color & 0x0F];
                            draw_text(s->draw_x, s->draw_y, display, dlen,
                                      cp437_8x16, 16, tc, 0xFF);
                        }
                    }
                }
            }
            break;
        case 0x1B: /* RIP_QUERY (ESC char) — query/send text variable.
                    * Format: flags:3 res:2 varname
                    * Recognized variables: $APP0$-$APP9$, $OVERFLOW$,
                    *   $OVERFLOW(NEXT)$, $OVERFLOW(PREV)$, $OVERFLOW(RESET)$
                    *
                    * FIX M4: CB_INPUT_TEXT callback equivalent.
                    * When a RIP_QUERY targets a $APPn$ variable AND the variable
                    * is empty (BBS wants user to fill it in), instead of returning
                    * the empty string we start a card→IIgs→card round-trip:
                    *   1. Push CMD_QUERY_PROMPT marker (0x3E) + prompt text + NUL
                    *      via TX FIFO so the IIgs can display an input form.
                    *   2. Set query_pending and record the target variable name.
                    *   3. Do NOT send a response to the BBS yet; wait for the IIgs
                    *      to send CMD_QUERY_RESPONSE bytes (handled in main.c).
                    * If the variable already has content, return it immediately
                    * (BBS is just querying a stored value, not requesting input). */
            if (len >= 5) {
                const char *vname = p + 5;
                int vlen = len - 5;
                char resp[64];
                int rlen = 0;

                /* $APPn$ — return stored application variable, or request input */
                if (vlen >= 6 && vname[0] == '$' && vname[1] == 'A' &&
                    vname[2] == 'P' && vname[3] == 'P' &&
                    vname[4] >= '0' && vname[4] <= '9' && vname[5] == '$') {
                    int idx = vname[4] - '0';
                    rlen = (int)rip_strnlen(s->app_vars[idx], sizeof(s->app_vars[0]));
                    if (rlen == 0 && rip_query_prompt_begin(s, vname, vlen)) {
                        /* Do not push a response to the BBS yet */
                        rlen = -1;  /* sentinel: skip card_tx_push below */
                    } else {
                        memcpy(resp, s->app_vars[idx], (size_t)rlen);
                    }

                /* $OVERFLOW(RESET)$ — reset to first page */
                } else if (vlen >= 18 &&
                    memcmp(vname, "$OVERFLOW(RESET)$", 17) == 0) {
                    s->rip2_state.overflow_page = 0;
                    rlen = 0;

                /* $OVERFLOW(NEXT)$ — advance one page */
                } else if (vlen >= 17 &&
                    memcmp(vname, "$OVERFLOW(NEXT)$", 16) == 0) {
                    if (s->rip2_state.overflow_page + 1 < s->rip2_state.overflow_total)
                        s->rip2_state.overflow_page++;
                    rlen = 0;

                /* $OVERFLOW(PREV)$ — back one page */
                } else if (vlen >= 17 &&
                    memcmp(vname, "$OVERFLOW(PREV)$", 16) == 0) {
                    if (s->rip2_state.overflow_page > 0)
                        s->rip2_state.overflow_page--;
                    rlen = 0;

                /* $OVERFLOW$ — return "page/total" string */
                } else if (vlen >= 11 &&
                    memcmp(vname, "$OVERFLOW$", 10) == 0) {
                    rlen = snprintf(resp, sizeof(resp), "%u/%u",
                                   s->rip2_state.overflow_page + 1,
                                   s->rip2_state.overflow_total);
                    if (rlen < 0) rlen = 0;

                /* Fix SV-1/S1: $FILEDEL$ — intentionally not implemented.
                 * Remote file deletion is a security vulnerability. Log and ignore. */
                } else if (vlen >= 10 &&
                    memcmp(vname, "$FILEDEL$", 9) == 0) {
                    /* Received $FILEDEL$ — silently ignore, do not delete anything */
                    rlen = 0;

                /* Fix SV-2/S2: $GOTOURL$ — do not launch processes.
                 * Display the URL in the status area only; never exec/ShellExecute. */
                } else if (vlen >= 10 &&
                    memcmp(vname, "$GOTOURL$", 9) == 0) {
                    /* URL follows after the variable name — display as status text */
                    /* No process launch; just acknowledge with zero-length response */
                    rlen = 0;

                } else {
                    char key[RIP_USER_VAR_NAME_MAX + 1];
                    if (rip_var_name_copy(vname, vlen, key, sizeof(key))) {
                        int uidx = rip_user_var_find(s, key, (int)strlen(key));
                        if (uidx >= 0) {
                            rlen = (int)rip_strnlen(s->user_var_values[uidx],
                                                    sizeof(s->user_var_values[uidx]));
                            if (rlen > (int)sizeof(resp))
                                rlen = (int)sizeof(resp);
                            memcpy(resp, s->user_var_values[uidx], (size_t)rlen);
                        } else if (rip_query_prompt_begin(s, vname, vlen)) {
                            rlen = -1;
                        } else {
                            rlen = 0;
                        }
                    } else {
                        rlen = 0;
                    }
                }

                /* rlen == -1 means query_pending was set; don't respond to BBS yet. */
                if (rlen > 0)
                    card_tx_push(resp, rlen);
            }
            break;

        default:
            break;
        }
        return;
    }

    /* Level 0 commands */
    switch (s->cmd_char) {

    /* ── Drawing state ───────────────────────────────────────── */
    case 'c': /* RIP_COLOR */
        if (len >= 2) s->draw_color = mega2(p) & 0x0F;
        break;
    case 'S': /* v1.54 spec: 'S' = RIP_FILL_STYLE — pattern:2 color:2 */
        if (len >= 4) {
            s->fill_pattern = (uint8_t)mega2(p);
            s->fill_color = (uint8_t)(mega2(p + 2) & 0x0F);
            int8_t card_pat = bgi_fill_to_card(s->fill_pattern);
            if (card_pat >= 0)
                draw_set_fill_style((uint8_t)card_pat, s->palette[s->back_color]);
        }
        break;
    case '=': /* RIP_LINE_STYLE: style:2, user_pat:4, thick:2 */
        if (len >= 2) {
            s->line_style = (uint8_t)mega2(p);
            if (len >= 8) {
                int16_t thick = scale_y(mega2(p + 6)); /* Fix B6: scale thickness to card Y */
                if (thick < 1) thick = 1;
                if (thick > 255) thick = 255;
                s->line_thick = (uint8_t)thick;
            }
            uint8_t pat = 0xFF;
            switch (s->line_style) {
            case 0: pat = 0xFF; break; /* solid */
            case 1: pat = 0x33; break; /* dotted */
            case 2: pat = 0xE7; break; /* center */
            case 3: pat = 0x1F; break; /* dashed */
            case 4: /* user-defined: 16-bit pattern from mega4 → 8-bit */
                if (len >= 6) pat = (uint8_t)(mega4(p + 2) >> 8);
                break;
            }
            draw_set_line_style(pat, s->line_thick);
        }
        break;
    case 'W': /* v1.54 spec: 'W' = RIP_WRITE_MODE — mode:2 */
        if (len >= 2) {
            uint8_t wm = (uint8_t)mega2(p);
            if (wm > 4) wm = 0;
            s->write_mode = wm;
            draw_set_write_mode(wm);
        }
        break;
    case 'Y': /* RIP_FONT_STYLE — font:2 dir:2 size:2 flags:2 */
        if (len >= 6) {
            uint8_t fid = (uint8_t)mega2(p);
            uint8_t fdir = (uint8_t)mega2(p + 2);
            uint8_t fsize = (uint8_t)mega2(p + 4);
            /* Validate: dir 0-2 (v3.1 adds dir=2 CCW), size 1-10 */
            if (fdir > 2) break;
            if (fsize < 1 || fsize > 10) break;
            s->font_id = fid;
            s->font_dir = fdir;
            s->font_size = fsize;
            /* v3.0 extension: justification flags in arg[3] (reserved in v1.54) */
            if (len >= 8) {
                uint8_t flags = (uint8_t)mega2(p + 6);
                s->font_hjust = 0;
                if (flags & 0x02) s->font_hjust = 1; /* center */
                if (flags & 0x04) s->font_hjust = 2; /* right */
                s->font_vjust = 0;
                if (flags & 0x10) s->font_vjust = 1; /* center */
                if (flags & 0x20) s->font_vjust = 2; /* top */
                if (flags & 0x40) s->font_vjust = 3; /* baseline */
            }
        }
        break;

    /* ── Cursor / position ───────────────────────────────────── */
    case 'm': /* RIP_MOVE */
        if (len >= 4) {
            s->draw_x = mega2(p);
            s->draw_y = scale_y(mega2(p + 2));
        }
        break;
    case 'g': /* RIP_GOTOXY (text cursor) */
        if (len >= 4) {
            comp_set_cursor(c, mega2(p), mega2(p + 2));
        }
        break;
    case 'H': /* RIP_HOME */
        comp_set_cursor(c, 0, 0);
        break;

    /* ── Screen operations ───────────────────────────────────── */
    case '*': /* RIP_RESET_WINDOWS — full state reset per spec */
        rip_reset_windows_state(s, c);
        break;
    case 'e': /* v1.54 spec §3.1: '|e' = RIP_ERASE_WINDOW — clear text window to background,
               * move cursor to upper-left of text window. IcyTerm: EraseWindow (0 args).
               * Note: previous "Fix B5" had this backwards; 'e' is always text-window clear. */
        comp_clear_screen(c, 2);
        break;
    case 'E': /* v1.54 spec §3.2: '|E' = RIP_ERASE_VIEW — clear graphics viewport
               * to background color (RIP_BACK_COLOR).  Previously hard-coded to
               * palette index 0 which only matches the default back_color. */
        {
            uint8_t bg = s->palette[s->back_color & 0x0F];
            uint8_t fg = s->palette[s->draw_color & 0x0F];
            draw_set_color(bg);
            draw_rect(s->vp_x0, s->vp_y0,
                      (int16_t)(s->vp_x1 - s->vp_x0 + 1),
                      (int16_t)(s->vp_y1 - s->vp_y0 + 1), true);
            draw_set_color(fg);
        }
        break;
    case '>': /* v1.54 spec §3.3: '|>' = RIP_ERASE_EOL — erase from text cursor to end of line.
               * IcyTerm: EraseEOL (0 args).
               * Note: previous code had '>' as NoMore and '#' as EraseWindow — both wrong. */
        comp_clear_line(c, 0);
        break;
    case 'w': /* RIP_TEXT_WINDOW — pixel coordinates for text region */
        if (len >= 10) {
            int16_t tw_x0 = mega2(p);
            int16_t tw_y0 = mega2(p + 2);
            int16_t tw_x1 = mega2(p + 4);
            int16_t tw_y1 = mega2(p + 6);
            clamp_ega_rect(&tw_x0, &tw_y0, &tw_x1, &tw_y1);
            s->tw_x0 = tw_x0;
            s->tw_y0 = tw_y0;
            s->tw_x1 = tw_x1;
            s->tw_y1 = tw_y1;
            s->tw_wrap = (uint8_t)mega_digit(p[8]);
            s->tw_font_size = (uint8_t)mega_digit(p[9]);
            /* Initialize cursor to top-left of text window (scaled) */
            s->tw_cur_x = s->tw_x0;
            s->tw_cur_y = scale_y(s->tw_y0);
            /* Non-default text window → route passthrough text to draw_text.
             * Heuristic: any rect different from the full screen counts as
             * "active".  A BBS that explicitly sets the full-screen rect
             * will look identical to "no text window" — acceptable since
             * both paths route to the same renderer. */
            s->tw_active = (s->tw_x0 != 0 || s->tw_y0 != 0 ||
                            s->tw_x1 != 639 || s->tw_y1 != 349);
        }
        break;
    case 'v': /* RIP_VIEWPORT */
        if (len >= 8) {
            int16_t vx0 = mega2(p), vy0 = mega2(p + 2);
            int16_t vx1 = mega2(p + 4), vy1 = mega2(p + 6);
            clamp_ega_rect(&vx0, &vy0, &vx1, &vy1);
            set_session_viewport(s, vx0, scale_y(vy0),
                                    vx1, scale_y1(vy1));
        }
        break;

    /* ── Palette ─────────────────────────────────────────────── */
    case 'Q': /* RIP_SET_PALETTE — 16 entries, values are EGA 64-color indices.
               * EGA indices map to framebuffer values 240-255 (see rip_init_first). */
        if (len >= 32) {
            for (int i = 0; i < 16; i++) {
                uint8_t ega64 = mega2(p + i * 2) & 0x3F;
                palette_write_rgb565(palette_slot(i), ega64_to_rgb565(ega64));
            }
        }
        break;
    case 'a': /* RIP_ONE_PALETTE — set one entry to EGA 64-color index */
        if (len >= 4) {
            uint8_t idx = mega2(p) & 0x0F;
            uint8_t ega64 = mega2(p + 2) & 0x3F;
            palette_write_rgb565(palette_slot(idx), ega64_to_rgb565(ega64));
        }
        break;

    /* ── Pixel ───────────────────────────────────────────────── */
    /* DLL command table entry 16: '@' = RIP_PIXEL (2 args: XY,XY).
     * Previous A2GSPU incorrectly mapped 'X' to RIP_PIXEL — 'X' is not in
     * the DLL command table.  '@' is the correct command letter. */

    /* ── Line ────────────────────────────────────────────────── */
    case 'L': /* RIP_LINE */
        if (len >= 8) {
            int16_t x0 = mega2(p), y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4), y1 = scale_y(mega2(p + 6));
            if (s->line_thick > 1)
                draw_thick_line(x0, y0, x1, y1);
            else
                draw_line(x0, y0, x1, y1);
        }
        break;

    /* ── Rectangle ───────────────────────────────────────────── */
    case 'R': /* RIP_RECTANGLE (outline) */
        if (len >= 8) {
            int16_t x0 = mega2(p), y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4), y1 = scale_y1(mega2(p + 6));
            draw_rect(x0, y0,
                      (int16_t)(x1 - x0 + 1),
                      (int16_t)(y1 - y0 + 1), false);
        }
        break;
    case 'B': /* RIP_BAR (filled rectangle, no border) — uses fill style */
        if (len >= 8 && s->fill_pattern != 0) {
            int16_t x0 = mega2(p), y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4), y1 = scale_y1(mega2(p + 6));
            draw_set_color(s->palette[s->fill_color & 0x0F]);
            draw_rect(x0, y0,
                      (int16_t)(x1 - x0 + 1),
                      (int16_t)(y1 - y0 + 1), true);
            draw_set_color(s->palette[s->draw_color & 0x0F]);
        }
        break;

    /* ── Circle ──────────────────────────────────────────────── */
    case 'C': /* RIP_CIRCLE */
        if (len >= 6) {
            draw_circle(mega2(p), scale_y(mega2(p + 2)),
                        scale_y(mega2(p + 4)), false);
        }
        break;

    /* ── Ellipse ─────────────────────────────────────────────── */
    case 'O': /* v1.54 spec: '|O' = RIP_OVAL — elliptical arc from st_ang to end_ang.
               * Args: cx:2 cy:2 st_ang:2 end_ang:2 x_rad:2 y_rad:2 (6 params, 12 chars).
               * IcyTerm: Oval { x, y, st_ang, end_ang, x_rad, y_rad } — parse_params.rs line 277.
               * Note: previous code had O/o swapped, mapping O to filled-oval (wrong). */
        /* fall through — 'O' and 'V' share an implementation */
    case 'V': /* RIP_OVAL_ARC — same field layout and renderer as 'O'. */
        if (len >= 12) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t rx = mega2(p + 8), ry = scale_y(mega2(p + 10));
            draw_elliptical_arc(cx, cy, rx, ry, sa, ea);
        }
        break;
    case 'o': /* v1.54 spec: '|o' = RIP_FILLED_OVAL — filled ellipse (full 360°).
               * Args: cx:2 cy:2 x_rad:2 y_rad:2 (4 params, 8 chars). No angle args.
               * IcyTerm: FilledOval { x, y, x_rad, y_rad } — parse_params.rs line 248.
               * Note: previous code had O/o swapped, mapping o to oval-arc (wrong). */
        if (len >= 8) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t rx = mega2(p + 4), ry_s = scale_y(mega2(p + 6));
            uint8_t border_mode;
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_ellipse(cx, cy, rx, ry_s, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_ellipse(cx, cy, rx, ry_s, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* ── Arc ──────────────────────────────────────────────────── */
    case 'A': /* RIP_ARC — DLL scales radius via ripScaleCoordY (EGA 350→400) */
        if (len >= 10) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t r = scale_y(mega2(p + 8));
            draw_arc(cx, cy, r, sa, ea);
        }
        break;
    /* 'V' was previously a duplicate handler for RIP_OVAL_ARC.
     * Merged with 'O' above via case fall-through. */

    /* ── Pie slices ──────────────────────────────────────────── */
    case 'I': /* RIP_PIE_SLICE — outline in draw_color, fill in fill_color.
               * DLL scales radius via ripScaleCoordY (EGA 350→400).
               *
               * Order matters: draw_pie(fill=true) paints the sector with
               * g_color over the arc/radii it drew first, so we must fill
               * before outlining or the outline gets wiped.  The optional
               * outline is controlled by RIP_SET_BORDER and drawn in COPY. */
        if (len >= 10) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t r  = scale_y(mega2(p + 8));
            uint8_t border_mode;
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_pie(cx, cy, r, sa, ea, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_pie(cx, cy, r, sa, ea, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;
    case 'i': /* RIP_OVAL_PIE_SLICE — fill before outline (see 'I' comment). */
        if (len >= 12) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t rx = mega2(p + 8), ry_s = scale_y(mega2(p + 10));
            uint8_t border_mode;
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_elliptical_pie(cx, cy, rx, ry_s, sa, ea, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_elliptical_pie(cx, cy, rx, ry_s, sa, ea, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* ── Bezier ──────────────────────────────────────────────── */
    case 'Z': /* RIP_BEZIER — x0:2 y0:2 x1:2 y1:2 x2:2 y2:2 x3:2 y3:2 steps:2
               * DLL command table (ripscrip_text.asm): 9 args. The 9th argument
               * (steps) is a curve quality / subdivision depth hint. draw_bezier()
               * uses a fixed subdivision depth internally; we read steps here so
               * the parser correctly consumes the full 18-char parameter field.
               * If draw_bezier is later extended to accept a step count, pass it. */
        if (len >= 16) {
            /* steps available at p+16 when len >= 18; reserved for future use */
            /* int steps = (len >= 18) ? mega2(p + 16) : 8; */
            draw_bezier(
                mega2(p),      scale_y(mega2(p + 2)),
                mega2(p + 4),  scale_y(mega2(p + 6)),
                mega2(p + 8),  scale_y(mega2(p + 10)),
                mega2(p + 12), scale_y(mega2(p + 14)));
        }
        break;

    /* ── Polygon / Polyline ──────────────────────────────────── */
    case 'P': /* RIP_POLYGON (outline) */
    case 'p': /* RIP_FILL_POLYGON */
    case 'l': /* RIP_POLYLINE */ {
        if (len >= 6) {
            int npts = mega2(p);
            /* Cap at 64 points to keep `pts[]` on the stack and out of the
             * malloc fallback path inside draw_polygon. */
            if (npts < 2 || npts > 64) break;
            if (len < 2 + npts * 4) break;
            int16_t pts[128]; /* max 64 points × 2 coords */
            for (int i = 0; i < npts; i++) {
                pts[i * 2]     = mega2(p + 2 + i * 4);
                pts[i * 2 + 1] = scale_y(mega2(p + 4 + i * 4));
            }
            if (s->cmd_char == 'l') {
                draw_polyline(pts, npts);
            } else if (s->cmd_char == 'p') {
                uint8_t border_mode;
                if (s->fill_pattern != 0) {
                    draw_set_color(s->palette[s->fill_color & 0x0F]);
                    draw_polygon(pts, npts, true);
                }
                if (rip_begin_filled_border(s, &border_mode)) {
                    draw_polygon(pts, npts, false);
                    rip_end_filled_border(s, border_mode);
                } else {
                    draw_set_color(s->palette[s->draw_color & 0x0F]);
                }
            } else {
                draw_polygon(pts, npts, false);
            }
        }
        break;
    }

    /* ── Flood fill ──────────────────────────────────────────── */
    case 'F': /* v1.54 spec: '|F' = RIP_FILL — flood fill from (x,y) until hitting border color.
               * Args: x:2 y:2 border:2 (3 params, 6 chars).
               * IcyTerm: Fill { x, y, border } — command.rs line 190, parse_params.rs line 245
               *   (0, b'F') → parse_base36_complete(..., 5) = 6 digits = 3 two-digit params.
               * Previous "Fix B3" used 0 args flooding at draw cursor (DLL internal behavior,
               * not the wire protocol). Spec and IcyTerm both confirm 3 args. */
        if (len >= 6) {
            int16_t fx = mega2(p), fy = scale_y(mega2(p + 2));
            int16_t border_idx = mega2(p + 4) & 0x0F;
            draw_set_color(s->palette[s->fill_color & 0x0F]);
            draw_flood_fill(fx, fy, s->palette[border_idx]);
            draw_set_color(s->palette[s->draw_color & 0x0F]);
        }
        break;

    /* ── Text ────────────────────────────────────────────────── */
    case 'T': /* RIP_TEXT — draw text at current position, advance draw_x */
        if (len > 0)
            rip_render_text(s, p, len);
        break;
    /* v1.54 spec: '@' = RIP_TEXT_XY — draw text at pixel position. */
    case '@': /* RIP_TEXT_XY — x:2 y:2 text */
        if (len >= 4) {
            s->draw_x = mega2(p);
            s->draw_y = scale_y(mega2(p + 2));
            rip_render_text(s, p + 4, len - 4);
        }
        break;
    /* v1.54 spec: 'X' = RIP_PIXEL — draw single pixel at (x,y). */
    case 'X': /* RIP_PIXEL — x:2 y:2 */
        if (len >= 4) {
            draw_pixel(mega2(p), scale_y(mega2(p + 2)));
        }
        break;
    /* v1.54 spec: 't' = RIP_REGION_TEXT — display a line of text in a
     * previously defined text region (Level 0, used with 'T' begin/end).
     * L16: also missed $variable expansion before this fix. */
    case 't': /* RIP_REGION_TEXT — justify:1 text */
        if (s->text_block.active && len >= 1) {
            int tstart = 1;
            if (tstart < len) {
                char tbuf_t[256];
                char vbuf_t[256];
                int tlen_t = unescape_text(p + tstart, len - tstart, tbuf_t);
                tlen_t = rip_expand_variables(s, tbuf_t, tlen_t, vbuf_t, sizeof(vbuf_t));
                uint8_t tc_t = s->palette[s->draw_color & 0x0F];
                draw_text(s->text_block.x0, s->text_block.cur_y,
                          vbuf_t, tlen_t, cp437_8x16, 16, tc_t, 0xFF);
                s->text_block.cur_y += 16;
            }
        }
        break;

    /* ── Fill style + custom fill pattern ───────────────────── */
    case 's': /* v1.54 spec: '|s' = RIP_FILL_PATTERN — custom 8×8 fill pattern + color.
               * Args: c1:2 c2:2 c3:2 c4:2 c5:2 c6:2 c7:2 c8:2 col:2 (9 params, 18 chars).
               * IcyTerm: FillPattern { c1..c8, col } — parse_params.rs line 323:
               *   (0, b's') → parse_base36_complete(..., 17) = 18 digits = 9 two-digit params.
               * 's' is strictly this command; dual-dispatch by len was wrong. */
        if (len >= 18) {
            uint8_t pat[8];
            for (int i = 0; i < 8; i++)
                pat[i] = (uint8_t)mega2(p + i * 2);
            draw_set_user_fill_pattern(pat);
            s->fill_color = mega2(p + 16) & 0x0F;
            s->fill_pattern = 12; /* BGI USER_FILL */
            draw_set_fill_style(11, s->palette[s->back_color]);
        }
        break;

    /* ── Scene control ───────────────────────────────────────── */
    case '#': /* v1.54 spec §3.4: '|#' = RIP_NO_MORE — end of RIPscrip scene.
               * IcyTerm: NoMore (0 args). BBS sends 3+ consecutive '#' commands for noise immunity.
               * Note: previous code had '#' as EraseWindow and '>' as NoMore — both wrong.
               * Correct mapping confirmed by v1.54 spec and IcyTerm command.rs:NoMore → "|#". */
        /* No-op: scene terminator; mouse regions already activated incrementally */
        break;

    /* §A2G2: state push/pop — backward-compatible QoL extension. */
    case '^': /* RIP_PUSH_STATE — snapshot drawing/cursor/viewport state.
               * Stack is bounded to RIP_STATE_STACK_MAX frames; overflow
               * silently drops the push (matches the "ignore unknown
               * params" precedent for graceful degradation). */
        if (s->state_stack_depth < RIP_STATE_STACK_MAX) {
            uint8_t i = s->state_stack_depth;
            s->state_stack[i].draw_color   = s->draw_color;
            s->state_stack[i].back_color   = s->back_color;
            s->state_stack[i].fill_color   = s->fill_color;
            s->state_stack[i].fill_pattern = s->fill_pattern;
            s->state_stack[i].line_style   = s->line_style;
            s->state_stack[i].line_thick   = s->line_thick;
            s->state_stack[i].write_mode   = s->write_mode;
            s->state_stack[i].font_id      = s->font_id;
            s->state_stack[i].font_size    = s->font_size;
            s->state_stack[i].font_dir     = s->font_dir;
            s->state_stack[i].font_attrib  = s->font_attrib;
            s->state_stack[i].draw_x       = s->draw_x;
            s->state_stack[i].draw_y       = s->draw_y;
            s->state_stack[i].vp_x0        = s->vp_x0;
            s->state_stack[i].vp_y0        = s->vp_y0;
            s->state_stack[i].vp_x1        = s->vp_x1;
            s->state_stack[i].vp_y1        = s->vp_y1;
            s->state_stack_depth++;
        }
        break;

    case '~': /* RIP_POP_STATE — restore the most recently pushed state.
               * Pop on empty stack is a no-op (also matches the
               * graceful-degradation precedent). */
        if (s->state_stack_depth > 0) {
            uint8_t i = (uint8_t)(--s->state_stack_depth);
            s->draw_color   = s->state_stack[i].draw_color;
            s->back_color   = s->state_stack[i].back_color;
            s->fill_color   = s->state_stack[i].fill_color;
            s->fill_pattern = s->state_stack[i].fill_pattern;
            s->line_style   = s->state_stack[i].line_style;
            s->line_thick   = s->state_stack[i].line_thick;
            s->write_mode   = s->state_stack[i].write_mode;
            s->font_id      = s->state_stack[i].font_id;
            s->font_size    = s->state_stack[i].font_size;
            s->font_dir     = s->state_stack[i].font_dir;
            s->font_attrib  = s->state_stack[i].font_attrib;
            s->draw_x       = s->state_stack[i].draw_x;
            s->draw_y       = s->state_stack[i].draw_y;
            s->vp_x0        = s->state_stack[i].vp_x0;
            s->vp_y0        = s->state_stack[i].vp_y0;
            s->vp_x1        = s->state_stack[i].vp_x1;
            s->vp_y1        = s->state_stack[i].vp_y1;
            /* Apply restored draw state immediately so subsequent draws
             * pick up the popped color / write mode / line style. */
            apply_draw_state(s);
            draw_set_clip(s->vp_x0, s->vp_y0, s->vp_x1, s->vp_y1);
        }
        break;

    /* ── E1: Undocumented command stubs ─────────────────────────
     * These command letters appear in the DLL command-letter accept
     * table (ripscrip_text.asm).  Recognising them prevents
     * ERROR_RECOVERY from consuming the rest of the frame when a
     * BBS sends one of these commands.  Full implementation deferred.
     * ─────────────────────────────────────────────────────────── */
    /* -- Filled circle (v2.0+) ----------------------------------------- */
    /* DLL command table entry 29: 'G' = RIP_FILLED_CIRCLE (3 args: XY,XY,XY) */
    case 'G': /* RIP_FILLED_CIRCLE -- cx:2 cy:2 radius:2.
               * DLL scales radius via ripScaleCoordY (EGA 350→400). */
        if (len >= 6) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t r = scale_y(mega2(p + 4));
            uint8_t border_mode;
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_circle(cx, cy, r, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_circle(cx, cy, r, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* -- Rounded rectangle (v2.0+) --------------------------------------- */
    /* DLL command table entry 64: 'U' = RIP_ROUNDED_RECT (5 args: XY,XY,XY,XY,XY) */
    case 'U': /* RIP_ROUNDED_RECT -- x0:2 y0:2 x1:2 y1:2 radius:2 */
        if (len >= 10) {
            int16_t x0 = mega2(p),      y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4),  y1 = scale_y1(mega2(p + 6));
            int16_t r  = scale_y(mega2(p + 8));
            draw_rounded_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, r, false);
        }
        break;

    /* DLL command table entry 65: 'u' = RIP_FILLED_ROUNDED_RECT (5 args: XY,XY,XY,XY,XY) */
    case 'u': /* RIP_FILLED_ROUNDED_RECT -- x0:2 y0:2 x1:2 y1:2 radius:2 */
        if (len >= 10) {
            int16_t x0 = mega2(p),      y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4),  y1 = scale_y1(mega2(p + 6));
            int16_t r  = scale_y(mega2(p + 8));
            uint8_t border_mode;
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_rounded_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, r, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_rounded_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, r, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* -- Background color (v2.0+) ---------------------------------------- */
    /* DLL command table entry 43: 'k' = RIP_BACK_COLOR (1 arg: COL) */
    case 'k': /* RIP_BACK_COLOR -- color:1.
               * Per BGI/RIP semantics, back_color is the OFF-bit color in
               * patterned fills and the clear color for RIP_ERASE_VIEW.
               * Push the new value into the draw layer so subsequent fills
               * pick it up without waiting for the next 'S'/'s'/'D'. */
        if (len >= 1) {
            s->back_color = mega_digit(p[0]) & 0x0F;
            int8_t card_pat = bgi_fill_to_card(s->fill_pattern);
            draw_set_fill_style((card_pat >= 0) ? (uint8_t)card_pat : 0,
                                s->palette[s->back_color & 0x0F]);
        }
        break;

    /* -- Save icon (v2.0+) ----------------------------------------------- */
    /* DLL command table entry 40: 'J' = RIP_SAVE_ICON (1 arg: 2-digit slot) */
    case 'J': /* RIP_SAVE_ICON -- slot:2 */
        if (len >= 2)
            (void)rip_save_clipboard_slot(s, (uint16_t)mega2(p));
        break;

    /* -- Scroll region (v2.0+) ------------------------------------------- */
    /* DLL command table entry 7: '+' = RIP_SCROLL (7 args: XY,XY,XY,XY,2,2,2) */
    case '+': /* RIP_SCROLL -- x0:2 y0:2 x1:2 y1:2 dx:2 dy:2 fill_col:2 */
        if (len >= 14) {
            int16_t sx0 = mega2(p),      sy0 = scale_y(mega2(p + 2));
            int16_t sx1 = mega2(p + 4),  sy1 = scale_y1(mega2(p + 6));
            int16_t dx  = mega2(p + 8);
            int16_t dy  = scale_y(mega2(p + 10));
            int16_t fc  = mega2(p + 12) & 0x0F;
            int16_t rw  = sx1 - sx0 + 1, rh = sy1 - sy0 + 1;
            if (rw > 0 && rh > 0) {
                draw_copy_rect(sx0, sy0, sx0 + dx, sy0 + dy, rw, rh);
                /* Clear the exposed strip(s) left behind by the scroll.
                 * Clamp strip extent to the source rect so a delta larger
                 * than the rect (|dx|>=rw or |dy|>=rh) clears just the
                 * source rect rather than spilling outside. */
                draw_set_color(s->palette[fc]);
                if (dy > 0) {
                    int16_t h = dy < rh ? dy : rh;
                    draw_rect(sx0, sy0, rw, h, true);
                } else if (dy < 0) {
                    int16_t h = -dy < rh ? -dy : rh;
                    draw_rect(sx0, (int16_t)(sy1 - h + 1), rw, h, true);
                }
                if (dx > 0) {
                    int16_t w = dx < rw ? dx : rw;
                    draw_rect(sx0, sy0, w, rh, true);
                } else if (dx < 0) {
                    int16_t w = -dx < rw ? -dx : rw;
                    draw_rect((int16_t)(sx1 - w + 1), sy0, w, rh, true);
                }
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* -- Copy region (v2.0+) --------------------------------------------- */
    /* DLL command table entry 8: ',' = RIP_COPY_REGION (10 args: XY*10) */
    case ',': /* RIP_COPY_REGION -- sx0:2 sy0:2 sx1:2 sy1:2 dx:2 dy:2 dx1:2 dy1:2 res:2 res:2 */
        if (len >= 12) {
            int16_t sx0 = mega2(p),      sy0 = scale_y(mega2(p + 2));
            int16_t sx1 = mega2(p + 4),  sy1 = scale_y1(mega2(p + 6));
            int16_t dx0 = mega2(p + 8),  dy0 = scale_y(mega2(p + 10));
            int16_t rw  = sx1 - sx0 + 1, rh  = sy1 - sy0 + 1;
            int16_t dw = rw, dh = rh;
            if (len >= 16) {
                int16_t dx1 = mega2(p + 12);
                int16_t dy1 = scale_y1(mega2(p + 14));
                if (!(dx1 == 0 && dy1 == 0)) {
                    if (dx0 > dx1) { int16_t t = dx0; dx0 = dx1; dx1 = t; }
                    if (dy0 > dy1) { int16_t t = dy0; dy0 = dy1; dy1 = t; }
                    dw = (int16_t)(dx1 - dx0 + 1);
                    dh = (int16_t)(dy1 - dy0 + 1);
                }
            }
            rip_copy_screen_region_scaled(s, sx0, sy0, rw, rh,
                                          dx0, dy0, dw, dh, DRAW_MODE_COPY);
        }
        break;

    /* -- Extended positioned text (v2.0+) -------------------------------- */
    /* DLL command table entry 9: '-' = RIP_TEXT_XY_EXT (5 args: XY,XY,XY,XY,2 + text).
     * Route through the shared text renderer inside the supplied box so
     * escapes, variables, clipping, and justification stay consistent. */
    case '-': /* RIP_TEXT_XY_EXT -- x0:2 y0:2 x1:2 y1:2 flags:2 text */
        if (len >= 10) {
            int16_t bx0 = mega2(p),     by0 = scale_y(mega2(p + 2));
            int16_t bx1 = mega2(p + 4), by1 = scale_y1(mega2(p + 6));
            uint8_t flags = (uint8_t)(mega2(p + 8) & 0xFF);
            rip_render_text_box(s, bx0, by0, bx1, by1, flags, p + 10, len - 10);
        }
        break;

    /* -- Header (v2.0+) -------------------------------------------------- */
    /* DLL command table entry 32: 'h' = RIP_HEADER (3 args: 2,4,2) */
    case 'h': /* RIP_HEADER -- type:2 id:4 flags:2 */
        /* Scene metadata; no visible output.  A header that resets the
         * environment also restores filled-object borders by spec. */
        if (len >= 8) {
            s->header_type = (uint8_t)(mega2(p) & 0xFF);
            s->header_id = (uint32_t)mega4(p + 2);
            s->header_flags = (uint8_t)(mega2(p + 6) & 0xFF);
            s->filled_borders_enabled = true;
        }
        break;

    /* -- Coordinate size (v2.0+) ----------------------------------------- */
    /* DLL command table entry 49: 'n' = RIP_SET_COORDINATE_SIZE (2 args: 1,3) */
    case 'n': /* RIP_SET_COORDINATE_SIZE -- byte_size:1 res:3 */
        /* Track the requested coordinate byte width.  The renderer keeps
         * using its fixed framebuffer coordinate transform, but $COORDSIZE$
         * and subsequent state snapshots now reflect the negotiated size. */
        if (len >= 4) {
            uint8_t size = (uint8_t)mega_digit(p[0]);
            if (size >= 2 && size <= 5)
                s->coordinate_size = size;
            s->coordinate_res = (uint32_t)mega3(p + 1);
        }
        break;

    /* -- Color mode (v2.0+) ---------------------------------------------- */
    /* DLL command table entry 46: 'M' = RIP_SET_COLOR_MODE (2 args: 1,1) */
    case 'M': /* RIP_SET_COLOR_MODE -- mode:1 bits:1 */
        /* mode=0: palette mapping. mode!=0: direct RGB with bits/component.
         * The card remains palette-backed, but mode tracking is observable
         * through $COLORMODE$ and future color-parameter handlers. */
        if (len >= 2) {
            uint8_t mode = (uint8_t)mega_digit(p[0]);
            uint8_t bits = (uint8_t)mega_digit(p[1]);
            s->color_mode = mode;
            if (mode == 0) {
                s->color_bits = 0;
            } else {
                if (bits < 1) bits = 1;
                if (bits > 8) bits = 8;
                s->color_bits = bits;
            }
        }
        break;

    /* -- Filled-object border control (v2.A3+) ---------------------------- */
    /* DLL command table entry 48: 'N' = RIP_SET_BORDER (1 arg: 2-digit) */
    case 'N': /* RIP_SET_BORDER -- borders:2, 00=off, nonzero=on */
        if (len >= 2)
            s->filled_borders_enabled = (mega2(p) != 0);
        break;

    /* -- Poly-Bezier (v2.0+) --------------------------------------------- */
    /* DLL command table entry 77: 'z' = RIP_POLY_BEZIER (nsegs:2 nsteps:2, then XY pairs) */
    case 'z': /* RIP_POLY_BEZIER -- nsegs:2 nsteps:2 [XY x 4 per segment] */
        /* Multi-segment cubic Bezier.  nsteps = subdivision hint (ignored).
         * Each segment is 4 XY pairs = 16 MegaNum chars. */
        if (len >= 4) {
            int nsegs  = mega2(p);
            int offset = 4; /* skip nsegs:2 + nsteps:2 */
            for (int seg = 0; seg < nsegs && offset + 16 <= len; seg++) {
                int16_t bx0 = mega2(p + offset),       by0 = scale_y(mega2(p + offset + 2));
                int16_t bx1 = mega2(p + offset + 4),   by1 = scale_y(mega2(p + offset + 6));
                int16_t bx2 = mega2(p + offset + 8),   by2 = scale_y(mega2(p + offset + 10));
                int16_t bx3 = mega2(p + offset + 12),  by3 = scale_y(mega2(p + offset + 14));
                draw_bezier(bx0, by0, bx1, by1, bx2, by2, bx3, by3);
                offset += 16;
            }
        }
        break;

    /* -- Group markers (v2.0+) ------------------------------------------- */
    /* DLL command table entry 4/5: '(' / ')' RIP_GROUP_BEGIN / END.
     *
     * The wire protocol takes 0 args, so there is no group identifier we
     * could match against a client-side cache.  Real "skip if cached"
     * grouping in the original DLL was driven by an out-of-band header/
     * checksum table that RIPlib does not expose.  We therefore accept
     * the markers (so a stream that bracket-wraps frames doesn't fall
     * into ERROR_RECOVERY) and otherwise treat them as no-ops; every
     * group is rendered every time.  Wire-format compatible, no
     * bandwidth optimization. */
    case '(':
    case ')':
        break;

    /* -- Undocumented commands (confirmed in DLL binary) ----------------- */

    /* DLL command table entry 1: '"' = RIP_BOUNDED_TEXT (5 args: XY,XY,XY,XY,2 + text) */
    case '"': /* RIP_BOUNDED_TEXT -- x0:2 y0:2 x1:2 y1:2 flags:2 text */
        /* Renders text within a bounding rectangle with word-wrap.
         * DLL rejects the 8x8 bitmap font (no character metrics).
         * We use the active font and perform simple greedy word-wrap. */
        if (len >= 10) {
            int16_t bx0 = mega2(p),      by0 = scale_y(mega2(p + 2));
            int16_t bx1 = mega2(p + 4),  by1 = scale_y1(mega2(p + 6));
            const char *tp = p + 10;
            int tlen = len - 10;
            if (tlen > 0 && bx1 > bx0 && by1 > by0) {
                char tbuf[256];
                char vbuf[256];
                draw_clip_state_t saved_clip;
                int16_t cx0, cy0, cx1, cy1;
                int outlen = unescape_text(tp, tlen > 255 ? 255 : tlen, tbuf);
                outlen = rip_expand_variables(s, tbuf, outlen, vbuf, sizeof(vbuf));
                uint8_t tc = s->palette[s->draw_color & 0x0F];
                int char_w = 8, char_h = 16;
                int cur_x = bx0, cur_y = by0;
                int i = 0;
                draw_save_clip(&saved_clip);
                cx0 = bx0 > saved_clip.x0 ? bx0 : saved_clip.x0;
                cy0 = by0 > saved_clip.y0 ? by0 : saved_clip.y0;
                cx1 = bx1 < saved_clip.x1 ? bx1 : saved_clip.x1;
                cy1 = by1 < saved_clip.y1 ? by1 : saved_clip.y1;
                if (cx0 <= cx1 && cy0 <= cy1) {
                    draw_set_clip(cx0, cy0, cx1, cy1);
                    while (i < outlen && cur_y + char_h <= by1) {
                        int word_end = i;
                        while (word_end < outlen && vbuf[word_end] != ' ') word_end++;
                        int word_w = (word_end - i) * char_w;
                        if (cur_x + word_w > bx1 && cur_x > bx0) {
                            cur_x = bx0;
                            cur_y += char_h;
                            if (cur_y + char_h > by1) break;
                        }
                        draw_text(cur_x, cur_y, vbuf + i, word_end - i,
                                  cp437_8x16, char_h, tc, 0xFF);
                        cur_x += word_w;
                        i = word_end;
                        if (i < outlen && vbuf[i] == ' ') {
                            cur_x += char_w;
                            i++;
                        }
                    }
                }
                draw_restore_clip(&saved_clip);
            }
        }
        break;

    /* DLL binary: '[' = RIP_FILLED_POLYGON_EXT (args: XY,XY,XY,XY,2,2,2) */
    case '[': /* RIP_FILLED_POLYGON_EXT -- x0:2 y0:2 x1:2 y1:2 mode:2 p1:2 p2:2 */
        /* Extended filled polygon with rendering mode override.
         * Simplified to a two-corner filled rectangle using current fill state. */
        if (len >= 8) {
            int16_t ex0 = mega2(p),     ey0 = scale_y(mega2(p + 2));
            int16_t ex1 = mega2(p + 4), ey1 = scale_y1(mega2(p + 6));
            uint8_t border_mode;
            /* mode:2 at p+8, param1:2 at p+10, param2:2 at p+12 -- ignored */
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_rect(ex0, ey0, ex1 - ex0 + 1, ey1 - ey0 + 1, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_rect(ex0, ey0, ex1 - ex0 + 1, ey1 - ey0 + 1, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* DLL binary: ']' = RIP_POLYLINE_EXT (args: XY,XY,XY,XY,2,2,2) */
    case ']': /* RIP_POLYLINE_EXT -- x0:2 y0:2 x1:2 y1:2 mode:2 p1:2 p2:2 */
        /* Extended polyline with rendering mode override.
         * Simplified to a line segment between the two corner points. */
        if (len >= 8) {
            int16_t ex0 = mega2(p),     ey0 = scale_y(mega2(p + 2));
            int16_t ex1 = mega2(p + 4), ey1 = scale_y(mega2(p + 6));
            /* mode:2 at p+8 -- ignored, current write mode applies */
            if (s->line_thick > 1)
                draw_thick_line(ex0, ey0, ex1, ey1);
            else
                draw_line(ex0, ey0, ex1, ey1);
        }
        break;

    /* DLL binary: '_' = RIP_DRAW_TO (6 args: XY,XY,2,2,XY,XY) */
    case '_': /* RIP_DRAW_TO -- x0:2 y0:2 mode:2 param:2 x1:2 y1:2 */
        /* Cursor move with optional line draw.
         * mode=0: move only, mode!=0: draw line then move. */
        if (len >= 12) {
            int16_t nx0  = mega2(p),     ny0 = scale_y(mega2(p + 2));
            int16_t draw = mega2(p + 4); /* mode:2 */
            /* param:2 at p+6 -- style hint, ignored */
            int16_t nx1  = mega2(p + 8), ny1 = scale_y(mega2(p + 10));
            if (draw) {
                if (s->line_thick > 1)
                    draw_thick_line(nx0, ny0, nx1, ny1);
                else
                    draw_line(nx0, ny0, nx1, ny1);
            }
            s->draw_x = nx1;
            s->draw_y = ny1;
        }
        break;

    /* DLL binary: 0x60 (backtick) = RIP_COMPOSITE_ICON (11 args: XY x 10, 1) */
    case 0x60: /* RIP_COMPOSITE_ICON -- 5 src/dst rect pairs (XY x 10) + mode:1 */
        /* Multi-region screen compositing: 5 rect pairs blit source regions
         * to destination regions using the specified raster op.  Historical
         * docs disagree on whether later entries are 12-byte rect records or
         * 8-byte point pairs; accept complete rect records and use the first
         * rect size for trailing point pairs. */
        if (len >= 12) {
            int offset = 0;
            int pairs = 0;
            int mode_pos = (len >= 41) ? 40 : len;
            uint8_t mode = (len >= 41) ? (uint8_t)mega_digit(p[40])
                                       : s->write_mode;
            int16_t cx0 = mega2(p),      cy0 = scale_y(mega2(p + 2));
            int16_t cx1 = mega2(p + 4),  cy1 = scale_y1(mega2(p + 6));
            int16_t cw  = cx1 - cx0 + 1, ch  = cy1 - cy0 + 1;
            if (mode > DRAW_MODE_NOT)
                mode = DRAW_MODE_COPY;
            while (offset + 12 <= mode_pos && pairs < 5) {
                int16_t sx0 = mega2(p + offset);
                int16_t sy0 = scale_y(mega2(p + offset + 2));
                int16_t sx1 = mega2(p + offset + 4);
                int16_t sy1 = scale_y1(mega2(p + offset + 6));
                int16_t dx = mega2(p + offset + 8);
                int16_t dy = scale_y(mega2(p + offset + 10));
                int16_t sw = (int16_t)(sx1 - sx0 + 1);
                int16_t sh = (int16_t)(sy1 - sy0 + 1);
                rip_copy_screen_region_scaled(s, sx0, sy0, sw, sh,
                                              dx, dy, sw, sh, mode);
                cw = sw;
                ch = sh;
                offset += 12;
                pairs++;
            }
            while (offset + 8 <= mode_pos && pairs < 5 && cw > 0 && ch > 0) {
                int16_t sx = mega2(p + offset);
                int16_t sy = scale_y(mega2(p + offset + 2));
                int16_t dx = mega2(p + offset + 4);
                int16_t dy = scale_y(mega2(p + offset + 6));
                rip_copy_screen_region_scaled(s, sx, sy, cw, ch,
                                              dx, dy, cw, ch, mode);
                offset += 8;
                pairs++;
            }
        }
        break;

    /* DLL binary: '{' = RIP_ANIMATION_FRAME (6 args: XY x 6 = 3 vertex pairs) */
    case '{': /* RIP_ANIMATION_FRAME -- 3 coordinate pairs */
        /* Draws filled polygon interior AND outline in one operation
         * (DLL calls GDI Polygon + Polyline in sequence).
         * With 3 points this is a filled+outlined triangle. */
        if (len >= 12) {
            int16_t pts[6];
            uint8_t border_mode;
            pts[0] = mega2(p);     pts[1] = scale_y(mega2(p + 2));
            pts[2] = mega2(p + 4); pts[3] = scale_y(mega2(p + 6));
            pts[4] = mega2(p + 8); pts[5] = scale_y(mega2(p + 10));
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_polygon(pts, 3, true);
            }
            if (rip_begin_filled_border(s, &border_mode)) {
                draw_polygon(pts, 3, false);
                rip_end_filled_border(s, border_mode);
            } else {
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* ── Kill mouse fields in region (Level 0) ───────────────── */
    /* DLL command table entry 42: 'K' = RIP_KILL_MOUSE_FIELDS (4 args: XY,XY,XY,XY).
     * Level 0 'K' removes all mouse regions whose rect intersects (x0,y0)-(x1,y1).
     * Level 1 'K' (above) kills ALL regions unconditionally. */
    case 'K': /* RIP_KILL_MOUSE_FIELDS — x0:2 y0:2 x1:2 y1:2 */
        if (len >= 8) {
            int16_t kx0 = mega2(p),     ky0 = scale_y(mega2(p + 2));
            int16_t kx1 = mega2(p + 4), ky1 = scale_y1(mega2(p + 6));
            uint16_t dst = 0;
            for (uint16_t i = 0; i < s->num_mouse_regions; i++) {
                rip_mouse_region_t *r = &s->mouse_regions[i];
                /* Keep region if it does NOT intersect the kill rectangle */
                if (r->x1 < kx0 || r->x0 > kx1 ||
                    r->y1 < ky0 || r->y0 > ky1) {
                    if (dst != i)
                        s->mouse_regions[dst] = *r;
                    dst++;
                }
            }
            s->num_mouse_regions = dst;
        }
        break;

    /* ── Fill pattern data block ─────────────────────────────── */
    /* DLL command table entry 23: 'D' = RIP_FILL_PATTERN (var, data block).
     * Level 0 'D' supplies a custom 8×8 fill pattern as raw bytes.
     * Level 1 'D' (above) is RIP_DEFINE — distinct command. */
    case 'D': /* RIP_FILL_PATTERN — 8×2-digit pattern bytes + color:2 */
        if (len >= 16) {
            uint8_t pat[8];
            for (int i = 0; i < 8; i++)
                pat[i] = (uint8_t)(mega2(p + i * 2));
            draw_set_user_fill_pattern(pat);
            if (len >= 18)
                s->fill_color = mega2(p + 16) & 0x0F;
            s->fill_pattern = 12; /* BGI USER_FILL */
            draw_set_fill_style(11, s->palette[s->back_color]);
        }
        break;

    /* ── Get image (clipboard capture) ──────────────────────── */
    /* DLL command table entry 13: '<' = RIP_GET_IMAGE (var, data block).
     * Captures a screen region into the clipboard for later PUT_IMAGE.
     * Minimum data block: x0:2 y0:2 x1:2 y1:2 (8 MegaNum chars). */
    case '<': /* RIP_GET_IMAGE — x0:2 y0:2 x1:2 y1:2 */
        if (len >= 8) {
            int16_t gx0 = mega2(p),     gy0 = scale_y(mega2(p + 2));
            int16_t gx1 = mega2(p + 4), gy1 = scale_y1(mega2(p + 6));
            int16_t gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1;
            (void)rip_clipboard_capture(s, gx0, gy0, gw, gh);
        }
        break;

    /* ── Icon display style (v2.0+) ──────────────────────────── */
    /* DLL command table entry 3: '&' = RIP_ICON_STYLE (5 args: XY,XY,2,2,2). */
    case '&': /* RIP_ICON_STYLE — x0:2 y0:2 x1:2 y1:2 style:2 align:2 scale:2 */
        if (len >= 14) {
            int16_t x0 = mega2(p),     y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4), y1 = scale_y1(mega2(p + 6));
            if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
            if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
            s->icon_style_x0 = x0;
            s->icon_style_y0 = y0;
            s->icon_style_x1 = x1;
            s->icon_style_y1 = y1;
            s->icon_style_style = (uint8_t)(mega2(p + 8) & 0x03);
            s->icon_style_align = (uint8_t)(mega2(p + 10) & 0x03);
            s->icon_style_scale = (uint8_t)(mega2(p + 12) & 0xFF);
            s->icon_style_active = true;
        } else {
            s->icon_style_active = false;
        }
        break;

    /* ── Stamp icon from slot (v2.0+) ───────────────────────── */
    /* DLL command table entry 10: '.' = RIP_STAMP_ICON (6 args: XY×6). */
    case '.': /* RIP_STAMP_ICON — slot:2 x:2 y:2 w:2 h:2 flags:2 */
        if (len >= 6) {
            uint16_t slot = (uint16_t)mega2(p);
            int16_t dx = mega2(p + 2);
            int16_t dy = scale_y(mega2(p + 4));
            int16_t dw = 0;
            int16_t dh = 0;
            rip_icon_t icon;
            bool have_icon = false;

            if (len >= 10) {
                dw = mega2(p + 6);
                dh = scale_y(mega2(p + 8));
            }

            if (slot < RIP_ICON_SLOT_MAX && s->icon_slot_valid[slot]) {
                icon = s->icon_slots[slot];
                have_icon = true;
            } else if (s->clipboard.valid && s->clipboard.data) {
                icon.pixels = s->clipboard.data;
                icon.width = (uint16_t)s->clipboard.width;
                icon.height = (uint16_t)s->clipboard.height;
                have_icon = true;
            }

            if (have_icon) {
                rip_draw_icon_pixels(s, dx, dy, icon.pixels,
                                     icon.width, icon.height,
                                     dw, dh, s->write_mode);
            }
        }
        break;

    /* ── Extended mouse region (v2.0+) ──────────────────────── */
    /* DLL command table entry 11: ':' = RIP_MOUSE_REGION_EXT (11 args: XY×11). */
    case ':': /* RIP_MOUSE_REGION_EXT — x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:2 res×5 */
        if (len >= 22 && s->num_mouse_regions < RIP_MAX_MOUSE_REGIONS) {
            rip_mouse_region_t *r = &s->mouse_regions[s->num_mouse_regions];
            memset(r, 0, sizeof(*r));
            r->x0     = mega2(p);
            r->y0     = scale_y(mega2(p + 2));
            r->x1     = mega2(p + 4);
            r->y1     = scale_y1(mega2(p + 6));
            r->hotkey = (uint8_t)(mega2(p + 8) & 0xFF);
            r->flags  = (uint8_t)(mega2(p + 10) & 0xFF) | RIP_MF_ACTIVE;
            r->active = true;
            s->num_mouse_regions++;
        }
        break;

    /* ── Extended button (v2.0+) ─────────────────────────────── */
    /* DLL command table entry 12: ';' = RIP_BUTTON_EXT (7 args: XY,XY,2,XY,XY,2,2). */
    case ';': /* RIP_BUTTON_EXT — x0:2 y0:2 x1:2 y1:2 style:2 lx:2 ly:2 rx:2 ry:2 flags:2 tidx:2 */
        if (len >= 14 && s->num_mouse_regions < RIP_MAX_MOUSE_REGIONS) {
            rip_mouse_region_t *r = &s->mouse_regions[s->num_mouse_regions];
            memset(r, 0, sizeof(*r));
            r->x0     = mega2(p);
            r->y0     = scale_y(mega2(p + 2));
            r->x1     = mega2(p + 4);
            r->y1     = scale_y1(mega2(p + 6));
            /* style:2 at p+8; label xy at p+10/p+12; flags:2 at p+14 */
            r->flags  = RIP_MF_ACTIVE;
            r->active = true;
            draw_rect(r->x0, r->y0,
                      r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, false);
            s->num_mouse_regions++;
        }
        break;

    /* ── Extended text window (v2.0+) ───────────────────────── */
    /* DLL command table entry 20: 'b' = RIP_EXT_TEXT_WINDOW
     * (9 args: XY,XY,XY,XY,2,2,1,4,3). */
    case 'b': /* RIP_EXT_TEXT_WINDOW — x0:2 y0:2 x1:2 y1:2 fore:2 back:2 font:1 size:4 flags:3 */
        if (len >= 18) {
            int16_t tw_x0 = mega2(p);
            int16_t tw_y0 = mega2(p + 2);
            int16_t tw_x1 = mega2(p + 4);
            int16_t tw_y1 = mega2(p + 6);
            clamp_ega_rect(&tw_x0, &tw_y0, &tw_x1, &tw_y1);
            s->tw_x0        = tw_x0;
            s->tw_y0        = tw_y0;
            s->tw_x1        = tw_x1;
            s->tw_y1        = tw_y1;
            s->etw_fore_col = mega2(p + 8)  & 0x0F;
            s->etw_back_col = mega2(p + 10) & 0x0F;
            s->etw_font_id  = mega_digit(p[12]);
            /* size:4 at p+13 — stored in font_ext_size; flags:3 at p+17 reserved */
            s->font_ext_size = (uint32_t)(mega4(p + 13));
            s->tw_cur_x = s->tw_x0;
            s->tw_cur_y = scale_y(s->tw_y0);
            s->tw_active = true;
        }
        break;

    /* ── Extended font style (v2.0+) ────────────────────────── */
    /* DLL command table entry 24: 'd' = RIP_EXT_FONT_STYLE (3 args: 2,1,4). */
    case 'd': /* RIP_EXT_FONT_STYLE — font_id:2 attr:1 size:4 */
        if (len >= 7) {
            s->font_ext_id   = (uint8_t)(mega2(p) & 0x0F);
            s->font_ext_attr = (uint8_t)(mega_digit(p[2]));
            s->font_ext_size = (uint32_t)(mega4(p + 3));
            /* Immediately apply the selected font ID */
            if (s->font_ext_id < BGI_FONT_COUNT)
                s->font_id = s->font_ext_id;
        }
        break;

    /* ── Font attribute flags (v2.0+) ───────────────────────── */
    /* DLL command table entry 28: 'f' = RIP_FONT_ATTRIB (2 args: XY,XY
     * interpreted as two 2-digit flag fields). */
    case 'f': /* RIP_FONT_ATTRIB — attrib:2 reserved:2 */
        /* bit0=bold, bit1=italic, bit2=underline, bit3=shadow.
         * Stored in s->font_attrib and applied by the BGI stroke renderer. */
        if (len >= 2)
            s->font_attrib = (uint8_t)(mega2(p) & 0x0F);
        break;

    }
}

/* ══════════════════════════════════════════════════════════════════
 * BYTE PROCESSING — 14-state FSM (DLL has 13; A2GSPU adds LEVEL3)
 *
 * DLL ground truth: ripParseStateMachine @ 0x10039E90
 * Jump table: 0x1003AB9C  (states 0-12, stored at pContext+0x00)
 * State 13 (LEVEL3_LETTER) is an A2GSPU addition for the '3' prefix.
 * prevState saved at pContext+0x04 for line-continuation restore.
 * lastChar  saved at pContext+0x9F for '!' line-boundary detection.
 * ══════════════════════════════════════════════════════════════════ */

void rip_process(rip_state_t *s, void *ctx, uint8_t ch) {
    comp_context_t *c = (comp_context_t *)ctx;
    g_rip_state = s;

    /* DLL: suppress DEL (0x7F) only.  High bytes (0x80-0xFE) must pass through
     * for CP437 box-drawing characters in ANSI passthrough mode.  The original
     * DLL filter (ch >= 0x7F) was for a Windows environment where high bytes
     * were handled by the GDI text renderer; on our framebuffer we need them.
     * UTF-8 → CP437 decoding is handled at the telnet receive level
     * (a2gspu_emu_telnet_poll) before bytes reach this parser. */
    if (ch == 0x7F) return;

reprocess:
    switch (s->state) {

    /* ── State 0: IDLE ──────────────────────────────────────────
     * Scanning for '!' to begin a RIPscrip escape sequence.
     * DLL handler @ 0x1003A5CA.
     * Also handles: SOH swallow, ESC[! probe response,
     * text-window routing, and ANSI passthrough.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_IDLE:
        /* E4: <<IF>>/<<ELSE>>/<<ENDIF>> stream-level pre-processor.
         * Evaluated before all other IDLE-state logic so that suppressed
         * content (preproc_suppress==true) is swallowed before it can
         * reach the VT100 passthrough or the '!' detector.
         *
         * State machine for the << … >> wrapper:
         *   preproc_state 0 = normal — watch for first '<'
         *   preproc_state 1 = got one '<' — watch for second '<'
         *   preproc_state 2 = inside <<…>> — collect directive bytes
         *   preproc_state 3 = saw one '>' inside <<…>> — confirm closing >>
         *
         * Directive evaluation is intentionally minimal: the DLL supports
         * only simple string comparisons of $VARIABLE$ values, so we do
         * the same. Unknown directives are ignored. */
        if (s->preproc_state == 0 && ch == '<') {
            s->preproc_state = 1;
            return; /* swallow first '<', wait for second */
        }
        if (s->preproc_state == 1) {
            if (ch == '<') {
                /* Got <<  — start collecting directive bytes */
                s->preproc_state = 2;
                s->preproc_len   = 0;
                return;
            }
            /* False alarm — was a lone '<'; emit it and re-process ch */
            s->preproc_state = 0;
            if (!s->preproc_suppress) {
                if (s->tw_active) rip_tw_putchar(s, '<');
                else              comp_passthrough_vt100(c, '<');
            }
            /* Fall through to normal processing of ch below */
        }
        if (s->preproc_state == 2) {
            if (ch == '\r' || ch == '\n') {
                s->preproc_state = 0;
                s->preproc_len = 0;
            } else if (ch == '>') {
                s->preproc_state = 3;
                return;
            } else {
                /* Bail out on malformed oversized directives rather than
                 * wedging the parser until a later literal >> appears. */
                if (s->preproc_len >= (int)sizeof(s->preproc_buf) - 1) {
                    s->preproc_state = 0;
                    s->preproc_len = 0;
                    return;
                }
                s->preproc_buf[s->preproc_len++] = (char)ch;
                return;
            }
        }
        if (s->preproc_state == 3) {
            if (ch == '\r' || ch == '\n') {
                s->preproc_state = 0;
                s->preproc_len = 0;
            } else if (ch == '>') {
                s->preproc_state = 0;
                preproc_finalize_directive(s);
                return;
            } else {
                if (s->preproc_len >= (int)sizeof(s->preproc_buf) - 2) {
                    s->preproc_state = 0;
                    s->preproc_len = 0;
                    return;
                }
                s->preproc_buf[s->preproc_len++] = '>';
                s->preproc_buf[s->preproc_len++] = (char)ch;
                s->preproc_state = 2;
                return;
            }
        }

        /* When suppressing, swallow all output bytes except '<' (which could
         * start a new << sequence via preproc_state machinery above). */
        if (s->preproc_suppress && ch != '<') return;

        /* DLL: swallow SOH (0x01) — "check_soh" handler */
        if (ch == 0x01) return;

        /* When RIP graphics have been drawn and we return to ANSI
         * passthrough, position the VT100 cursor near the bottom so
         * the BBS status bar renders below the graphics area. One-shot. */
        if (s->rip_has_drawn && !s->cursor_repositioned && ch != '!') {
            s->cursor_repositioned = true;
            comp_set_cursor(c, 0, 23);
        }

        /* ESC[! auto-detect: BBSes send ESC[! to probe for RIPscrip
         * capability.  Track the 3-byte sequence; respond with the
         * v1.54 identification string when confirmed. */
        if (ch == 0x1B) {
            s->esc_detect = 1;
            break;
        } else if (s->esc_detect == 1 && ch == '[') {
            s->esc_detect = 2;
            break;
        } else if (s->esc_detect == 2 && ch == '!') {
            /* ESC[! confirmed — reply with RIPSCRIP<ver><vendor>
             * 015400 = version 1.54, vendor 0, sub 0 */
            s->esc_detect = 0;
            card_tx_push("RIPSCRIP015400\n", 15);
            break;
        } else if (s->esc_detect > 0) {
            /* Not ESC[! — flush deferred bytes to VT100 then continue */
            uint8_t ed = s->esc_detect;
            s->esc_detect = 0;
            if (ed >= 1) comp_passthrough_vt100(c, 0x1B);
            if (ed >= 2) comp_passthrough_vt100(c, '[');
        }

        if (ch == '!') {
            /* DLL: check lastChar (+0x9F) for line boundary.
             * '!' triggers RIPscrip only after CR, LF, FF, or start-of-stream.
             * RELAXED: Also accept after ESC sequence terminators (letters after
             * CSI params) since BBSes may send !| immediately after ANSI codes
             * on the same line (e.g., ESC[2J!|*|). */
            s->state = RIP_ST_GOT_BANG;
        } else if (s->tw_active) {
            rip_tw_putchar(s, ch);
            s->last_char = ch;
        } else {
            /* Pass through to VT100. Use comp_passthrough_vt100, NOT
             * comp_write_raw — the latter re-enters via the RIPscrip hook. */
            comp_passthrough_vt100(c, ch);
            s->last_char = ch;
        }
        break;

    /* ── State 1: GOT_BANG ──────────────────────────────────────
     * Received '!'.  Waiting for '|' to confirm "!|" escape.
     * DLL handler @ 0x1003A628.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_GOT_BANG:
        if (ch == '|') {
            s->cmd_len   = 0;
            s->cmd_char  = 0;
            clear_levels(s);
            s->state = RIP_ST_COMMAND;
        } else {
            /* False alarm — emit '!' then re-process current byte in IDLE */
            comp_passthrough_vt100(c, '!');
            s->state = RIP_ST_IDLE;
            goto reprocess;
        }
        break;

    /* ── State 2: CMD_LETTER ────────────────────────────────────
     * Collecting command identifier and parameters after "!|".
     * DLL handler @ 0x10039EB8.
     *
     * DLL dispatch order:
     *   ch >= 0x7F  → ignore  (filtered at function entry)
     *   ch == '\\'  → prevState=state; state=LINE_CONT(5)
     *   ch CR/LF/FF → dispatch; return to IDLE
     *   ch == '|'   → dispatch; reset for next command in frame
     *   cmd_char==0, '1'-'9' → level-prefix states 10/11
     *   cmd_char==0, letter  → record cmd_char, stay here
     *   cmd_char==0, other   → ERROR_RECOVERY(12)
     *   cmd_char!=0          → accumulate parameter byte
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_COMMAND:
        if (ch == '\\') {
            /* Line continuation — save state, enter LINE_CONT.
             * DLL: pContext->prevState = state; state = LINE_CONT (6) */
            s->prev_state = s->state;
            s->state = RIP_ST_LINE_CONT;
            break;
        }

        if (ch == '\r' || ch == '\n') {
            /* Line terminator — dispatch pending command; return to IDLE */
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            s->last_char = ch;
            s->state     = RIP_ST_IDLE;
            break;
        }

        if (ch == '|') {
            /* '|' terminates current command; more may follow in frame.
             * Execute, reset, stay in CMD_LETTER for next command — unless
             * the dispatched command (e.g. via $ABORT$) reset state to IDLE,
             * in which case honor that. */
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            bool aborted = (s->state == RIP_ST_IDLE);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            if (aborted)
                s->state = RIP_ST_IDLE;
            /* else stay in RIP_ST_COMMAND */
            break;
        }

        if (s->cmd_char == 0) {
            /* First byte after !| or after a '|' separator — command letter only.
             * A1: once cmd_char is set we transition to ARG_COLLECT (state 3) so
             * that parameter accumulation lives in its own dedicated state arm. */
            if (ch == '1') {
                s->is_level1 = true;
                s->state = RIP_ST_LEVEL1_LETTER;
            } else if (ch == '2') {
                s->is_level2 = true;
                s->state = RIP_ST_LEVEL2_LETTER;
            } else if (ch == '3') {
                s->is_level3 = true;
                s->state = RIP_ST_LEVEL3_LETTER;
            } else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                       ch == '*' || ch == '#' || ch == '@' || ch == '>' ||
                       ch == '=' || ch == '!' ||
                       ch == '(' || ch == ')' || ch == '+' || ch == ',' ||
                       ch == '-' || ch == '.' || ch == ':' || ch == ';' ||
                       ch == '<' || ch == '[' || ch == ']' || ch == '_' ||
                       ch == '&' || ch == '`' || ch == '{' || ch == '"' ||
                       ch == '^' || ch == '~') {
                /* Valid Level 0 command letter */
                s->cmd_char = (char)ch;
                /* A3: '!' as the command letter is the comment marker (!|!…|).
                 * Transition directly to COMMENT (state 9); no args to collect. */
                if (ch == '!') {
                    s->state = RIP_ST_COMMENT;
                } else {
                    /* A1: transition to ARG_COLLECT for parameter byte accumulation */
                    s->state = RIP_ST_ARG_COLLECT;
                }
            } else {
                /* Unknown command-letter byte — resync.
                 * DLL: table lookup fails → state = ERROR_RECOVERY (12) */
                s->state = RIP_ST_ERROR_RECOVERY;
            }
        }
        /* cmd_char != 0 no longer reached here: once cmd_char is set this state
         * transitions to ARG_COLLECT, so the else branch is unreachable.
         * Retained for robustness should prev_state restoration ever land here. */
        break;

    /* ── State 3: ARG_COLLECT ───────────────────────────────────
     * cmd_char is set; accumulate MegaNum parameter bytes until
     * '|' (next command in frame) or CR/LF (end of frame line).
     * DLL handler @ 0x10039EB8 (shares dispatch with CMD_LETTER
     * via the "cmd_char != 0" branch — split out here for clarity).
     *
     * A1: This state was previously handled inline in CMD_LETTER.
     * Separating it makes the FSM match the 13-state DLL layout.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_ARG_COLLECT:
        if (ch == '\\') {
            /* Line continuation inside a parameter sequence */
            s->prev_state = s->state;
            s->state = RIP_ST_LINE_CONT;
            break;
        }

        if (ch == '\r' || ch == '\n') {
            /* End of frame line — dispatch pending command, return to IDLE */
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            s->last_char = ch;
            s->state     = RIP_ST_IDLE;
            break;
        }

        if (ch == '|') {
            /* Command terminator — dispatch, then accept next command letter.
             * If the dispatched command (e.g. via $ABORT$) reset state to
             * IDLE, honor that rather than overriding back to COMMAND. */
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            bool aborted = (s->state == RIP_ST_IDLE);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            s->state = aborted ? RIP_ST_IDLE : RIP_ST_COMMAND;
            break;
        }

        /* Accumulate parameter byte */
        if (s->cmd_len < (int)sizeof(s->cmd_buf) - 1)
            s->cmd_buf[s->cmd_len++] = (char)ch;
        break;

    /* ── State 5: LINE_CONT ─────────────────────────────────────
     * '\' received mid-command.  Waiting for CR or LF.
     * DLL handler @ 0x1003A400.
     *
     * CR        → state = LINE_WAIT_LF(6)   [wait for optional CRLF pair]
     * LF        → restore prevState          [bare-LF continuation done]
     * !|\       → escape pair: push both bytes literally to cmd_buf so
     *             unescape_text() can resolve them.  Without this, '\|'
     *             inside a text parameter would split the command on '|'.
     * other     → restore prevState; emit '\' literal; re-process char
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_LINE_CONT:
        if (ch == '\r') {
            s->state = RIP_ST_LINE_WAIT_LF;
        } else if (ch == '\n') {
            s->state = s->prev_state;
        } else if (ch == '!' || ch == '|' || ch == '\\') {
            /* Escape pair: keep both bytes in cmd_buf so unescape_text()
             * decodes \! → !, \| → |, \\ → \ at command-execute time.
             * Stay in prev_state so subsequent bytes resume normally. */
            s->state = s->prev_state;
            if (s->cmd_char != 0 &&
                s->cmd_len + 1 < (int)sizeof(s->cmd_buf) - 1) {
                s->cmd_buf[s->cmd_len++] = '\\';
                s->cmd_buf[s->cmd_len++] = (char)ch;
            }
        } else {
            /* Non-escape, non-newline: '\' was literal; reprocess ch. */
            s->state = s->prev_state;
            if (s->cmd_char != 0 &&
                s->cmd_len < (int)sizeof(s->cmd_buf) - 1)
                s->cmd_buf[s->cmd_len++] = '\\';
            goto reprocess;
        }
        break;

    /* ── State 6: LINE_WAIT_LF ──────────────────────────────────
     * CR received after '\'.  Waiting for LF (CRLF continuation).
     * DLL handler @ 0x1003A469.
     *
     * LF  → restore prevState   [CRLF pair consumed]
     * other → restore prevState; re-process char
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_LINE_WAIT_LF:
        if (ch == '\n') {
            s->state = s->prev_state;
        } else {
            s->state = s->prev_state;
            goto reprocess;
        }
        break;

    /* ── State 7: TEXT_COLLECT ──────────────────────────────────
     * Free-text parameter collection until '|', CR, or LF.
     * DLL handler @ 0x1003A525.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_TEXT_COLLECT:
        if (ch == '\\') {
            s->prev_state = s->state;
            s->state = RIP_ST_LINE_CONT;
        } else if (ch == '|' || ch == '\r' || ch == '\n') {
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            if (ch == '\r' || ch == '\n') {
                s->last_char = ch;
                s->state = RIP_ST_IDLE;
            } else {
                s->state = RIP_ST_COMMAND;
            }
        } else {
            if (s->cmd_len < (int)sizeof(s->cmd_buf) - 1)
                s->cmd_buf[s->cmd_len++] = (char)ch;
        }
        break;

    /* ── State 8: SUPPRESS ──────────────────────────────────────
     * Suppress ANSI fallback text after an unrecognized command.
     * Consume printable bytes; stop on '|', CR/LF, or '!'.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_SUPPRESS:
        if (ch == '\r' || ch == '\n') {
            s->last_char = ch;
            s->state = RIP_ST_IDLE;
        } else if (ch == '!') {
            s->state = RIP_ST_IDLE;
            goto reprocess;
        } else if (ch == '|') {
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            s->state = RIP_ST_COMMAND;
        } else if (ch < 0x20) {
            s->state = RIP_ST_IDLE;
            goto reprocess;
        }
        /* Printable chars silently consumed */
        break;

    /* ── State 9: COMMENT ───────────────────────────────────────
     * Inside a !|! comment.  Skip bytes until closing '|'.
     * DLL Level 0 '!' command handler swallows until next '|'.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_COMMENT:
        if (ch == '|') {
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            s->state = RIP_ST_COMMAND;
        } else if (ch == '\r' || ch == '\n') {
            s->last_char = ch;
            s->state = RIP_ST_IDLE;
        }
        break;

    /* ── State 10: LEVEL1_LETTER ────────────────────────────────
     * After '1' prefix — waiting for Level 1 sub-command letter.
     * DLL: secondary dispatch table for level-prefix routing.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_LEVEL1_LETTER:
        if (ch == '\r' || ch == '\n') {
            s->last_char = ch;
            s->is_level1 = false;
            s->state = RIP_ST_IDLE;
        } else if (ch == '|') {
            s->is_level1 = false;
            s->state = RIP_ST_COMMAND;
        } else {
            /* Sub-command letter acquired; collect parameters in ARG_COLLECT */
            s->cmd_char = (char)ch;
            s->state = RIP_ST_ARG_COLLECT;
        }
        break;

    /* ── State 11: LEVEL2_LETTER ────────────────────────────────
     * After '2' prefix — waiting for Level 2 sub-command letter.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_LEVEL2_LETTER:
        if (ch == '\r' || ch == '\n') {
            s->last_char = ch;
            s->is_level2 = false;
            s->state = RIP_ST_IDLE;
        } else if (ch == '|') {
            s->is_level2 = false;
            s->state = RIP_ST_COMMAND;
        } else {
            s->cmd_char = (char)ch;
            s->state = RIP_ST_ARG_COLLECT;
        }
        break;

    /* ── State 13: LEVEL3_LETTER ────────────────────────────────
     * After '3' prefix — waiting for Level 3 sub-command letter.
     * DLL has 5 Level 3 commands; letters not fully documented.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_LEVEL3_LETTER:
        if (ch == '\r' || ch == '\n') {
            s->last_char = ch;
            s->is_level3 = false;
            s->state = RIP_ST_IDLE;
        } else if (ch == '|') {
            s->is_level3 = false;
            s->state = RIP_ST_COMMAND;
        } else {
            s->cmd_char = (char)ch;
            s->state = RIP_ST_ARG_COLLECT;
        }
        break;

    /* ── State 12: ERROR_RECOVERY ───────────────────────────────
     * Invalid command or argument overflow.  Consume until resync.
     * DLL handler @ 0x1003A581.
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_ERROR_RECOVERY:
        if (ch == '|') {
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            clear_levels(s);
            s->state = RIP_ST_COMMAND;
        } else if (ch == '\r' || ch == '\n') {
            s->last_char = ch;
            s->state = RIP_ST_IDLE;
        }
        /* Other bytes discarded */
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * HOST CALLBACK SHIMS — called from main.c Mosaic command handlers
 *
 * These replace the RIPSCRIP.DLL host-app callback slots (the 75-slot
 * FARPROC table) that riptel.exe filled at startup.  Each function
 * operates on g_rip_state directly; main.c calls them by extern decl
 * to keep rip_state_t opaque outside this compilation unit.
 * ══════════════════════════════════════════════════════════════════ */

/* FIX V1: CMD_SYNC_DATE byte handler (CB_GET_TIME / date half).
 * Called by main.c for each BUS_MOSAIC_CMD_SYNC_DATE write.
 * data_byte: next date character from IIgs ProDOS GET_TIME result,
 *   or 0x00 to commit the accumulated buffer to host_date. */
void rip_sync_date_byte(uint8_t data_byte) {
    if (!g_rip_state) return;
    if (data_byte == '\0') {
        /* NUL — commit accumulated buffer to host_date */
        int len = g_rip_state->sync_date_len;
        if (len > (int)sizeof(g_rip_state->host_date) - 1)
            len = (int)sizeof(g_rip_state->host_date) - 1;
        memcpy(g_rip_state->host_date, g_rip_state->sync_date_buf, (size_t)len);
        g_rip_state->host_date[len] = '\0';
        g_rip_state->sync_date_len = 0;
    } else {
        if (g_rip_state->sync_date_len < (int)sizeof(g_rip_state->sync_date_buf) - 1)
            g_rip_state->sync_date_buf[g_rip_state->sync_date_len++] = (char)data_byte;
    }
}

/* FIX V1: CMD_SYNC_TIME byte handler (CB_GET_TIME / time half).
 * Called by main.c for each BUS_MOSAIC_CMD_SYNC_TIME write.
 * data_byte: next time character, or 0x00 to commit to host_time. */
void rip_sync_time_byte(uint8_t data_byte) {
    if (!g_rip_state) return;
    if (data_byte == '\0') {
        int len = g_rip_state->sync_time_len;
        if (len > (int)sizeof(g_rip_state->host_time) - 1)
            len = (int)sizeof(g_rip_state->host_time) - 1;
        memcpy(g_rip_state->host_time, g_rip_state->sync_time_buf, (size_t)len);
        g_rip_state->host_time[len] = '\0';
        g_rip_state->sync_time_len = 0;
    } else {
        if (g_rip_state->sync_time_len < (int)sizeof(g_rip_state->sync_time_buf) - 1)
            g_rip_state->sync_time_buf[g_rip_state->sync_time_len++] = (char)data_byte;
    }
}

/* FIX M4: CMD_QUERY_RESPONSE byte handler (CB_INPUT_TEXT callback equivalent).
 * Called by main.c for each BUS_MOSAIC_CMD_QUERY_RESPONSE write.
 * data_byte: next response character typed by the user on the IIgs,
 *   or 0x00 to commit and send the response to the BBS.
 * On NUL: stores response in the target user variable, pushes it to the
 * BBS via TX FIFO, and clears query_pending. */
void rip_query_response_byte(uint8_t data_byte) {
    rip_state_t *s = g_rip_state;
    if (!s || !s->query_pending) return;

    if (data_byte == '\0') {
        /* Commit: identify target variable from query_var_name and store result. */
        const char *vn = s->query_var_name;
        if (vn[0] == '$' && vn[1] == 'A' && vn[2] == 'P' && vn[3] == 'P' &&
            vn[4] >= '0' && vn[4] <= '9' && vn[5] == '$') {
            int idx = vn[4] - '0';
            int rlen = s->query_response_len;
            if (rlen > 31) rlen = 31;
            memcpy(s->app_vars[idx], s->query_response, (size_t)rlen);
            s->app_vars[idx][rlen] = '\0';
            /* Send response to BBS now that we have it */
            if (rlen > 0)
                card_tx_push(s->app_vars[idx], rlen);
        } else {
            int rlen = s->query_response_len;
            if (rip_user_var_set(s, vn, (int)strlen(vn),
                                 s->query_response, rlen) && rlen > 0) {
                card_tx_push(s->query_response, rlen);
            }
        }
        s->query_pending = false;
        s->query_response_len = 0;
    } else {
        if (s->query_response_len < (int)sizeof(s->query_response) - 1)
            s->query_response[s->query_response_len++] = (char)data_byte;
    }
}
