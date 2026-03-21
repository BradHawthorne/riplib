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
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
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

/* Shared RIPscrip 2.0 extension state */
static ripscrip2_state_t rip2_state;

/* BGI stroke fonts (parsed at init, indexed by BGI_FONT_* ID) */
#define BGI_FONT_COUNT 11  /* 0=bitmap, 1-10=stroke */
static bgi_font_t bgi_fonts[BGI_FONT_COUNT];
static bool bgi_fonts_loaded = false;

/* Backward compat alias */
#define bgi_triplex         bgi_fonts[BGI_FONT_TRIPLEX]
#define bgi_triplex_loaded  bgi_fonts_loaded

/* Global parser state (one RIPscrip session at a time) */
static rip_state_t *g_rip_state = NULL;

/* Card->host TX FIFO helper (implemented in main.c / emulator stubs). */
extern void card_tx_push(const char *buf, int len);

/* FILE UPLOAD — receive BMP/ICN data from host for PSRAM caching */
#define FILE_UPLOAD_MAX  (128 * 1024)  /* 128KB max per file */
static uint8_t *upload_buf = NULL;
static int upload_pos = 0;
static char upload_name[16];
static int upload_name_len = 0;
static bool upload_reading_name = false;

/* ══════════════════════════════════════════════════════════════════
 * MEGANUM DECODER — base-36 parameter encoding
 * ══════════════════════════════════════════════════════════════════ */

static int mega_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    return 0;
}

static int mega2(const char *p) {
    return mega_digit(p[0]) * 36 + mega_digit(p[1]);
}

static int mega4(const char *p) {
    return mega_digit(p[0]) * 46656 + mega_digit(p[1]) * 1296 +
           mega_digit(p[2]) * 36 + mega_digit(p[3]);
}

/* Scale RIPscrip Y (640×350) to card Y (640×400).
 * Two variants prevent gaps between adjacent rectangles:
 * scale_y  = floor (for top edges, y-positions, single coords)
 * scale_y1 = ceiling (for bottom edges — ensures adjacent rects touch) */
static int16_t scale_y(int y) {
    return (int16_t)((y * 8) / 7);
}
static int16_t scale_y1(int y) {
    return (int16_t)((y * 8 + 6) / 7);
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
        s->saved_palette_rgb565[i] = palette_read_rgb565(240 + i);
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
            palette_write_rgb565(240 + i, g_rip_state->saved_palette_rgb565[i]);
    } else {
        for (int i = 0; i < 16; i++)
            palette_write_rgb565(240 + i, ega_default_rgb565[i]);
    }
}

/* Codex FIX 1: Boot-time init — call ONCE at power-on (or on first use).
 * Performs the full memset, arena reservation, drawing defaults, and BGI
 * font parse.  Calling this on a mid-session protocol switch would wipe
 * session state (clipboard, mouse regions, text variables, PSRAM arena).
 * For protocol switches, call rip_activate() instead. */
void rip_init_first(rip_state_t *s) {
    /* Reserve the PSRAM arena before the memset so we can restore the
     * already-allocated block.  On the very first call, size == 0 and
     * psram_arena_init() acquires the block from the global bump allocator. */
    psram_arena_t saved_arena = s->psram_arena;
    memset(s, 0, sizeof(*s));
    s->psram_arena = saved_arena;

    /* First call: reserve the arena block from global PSRAM. */
    if (s->psram_arena.size == 0)
        psram_arena_init(&s->psram_arena, RIP_PSRAM_ARENA_SIZE);

    /* Bind the icon module to this arena.  Cache is cleared because we just
     * memset'd — all PSRAM pixel pointers from a previous session are gone. */
    rip_icon_set_arena(&s->psram_arena);

    upload_buf = NULL;

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
    s->vp_x1 = 639; s->vp_y1 = 349;

    /* Default palette: map EGA indices 0-15 to framebuffer values 240-255.
     * This avoids conflicting with xterm-256 colors at indices 0-239.
     * RIP draw commands write framebuffer value s->palette[color], and the
     * display converts via emu->palette[240+i] → EGA RGB565. */
    for (int i = 0; i < 16; i++) {
        s->palette[i] = 240 + i;
        palette_write_rgb565(240 + i, ega_default_rgb565[i]);
    }

    /* Resolution mode: 0=EGA(640×350). memset zeroed it; document the default.
     * A2GSPU framebuffer is fixed 640×400 (rendered as EGA-scaled). The 'n'
     * command may set this field; $PROT$ query reports it to the BBS. */
    s->resolution_mode = 0;

    /* A2GSPU v3.1: application variables and overflow pagination */
    memset(s->app_vars, 0, sizeof(s->app_vars));

    /* FIX TX1: Seed the $RAND$ LCG from the RP2350 RTC timestamp.
     * Using time() gives a different sequence per session (good enough for BBS
     * use).  The memset above zeroed rand_state; a zero seed is valid for the
     * Knuth LCG — first output will be 12345 — but we prefer time entropy. */
    s->rand_state = (uint32_t)time(NULL);

    /* FIX TX2: refresh_suppress starts false (normal refresh enabled). */
    s->refresh_suppress = false;

    ripscrip2_init(&rip2_state);

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
    g_rip_state = s;

    /* Restore BBS-customized palette (or EGA defaults on first activation).
     * rip_save_palette() captured saved_palette_rgb565 before the previous
     * deactivation; rip_apply_palette() writes it back to hardware. */
    rip_apply_palette();

    /* Reset the viewport clip to full screen so stale clip regions from
     * the previous protocol don't constrain RIPscrip drawing. */
    s->vp_x0 = 0; s->vp_y0 = 0;
    s->vp_x1 = 639; s->vp_y1 = 349;
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
    /* Reclaim all PSRAM arena allocations (clipboard pixels, cached icon
     * pixels, upload staging buffer) from the disconnected session. */
    psram_arena_reset(&s->psram_arena);

    /* Rebind the icon cache to the freshly-reset arena.  This also clears
     * the runtime cache so stale pixel pointers into the old arena are gone. */
    rip_icon_set_arena(&s->psram_arena);

    /* Codex FIX 4: Flush the pending icon file request queue. */
    rip_icon_clear_requests();

    /* Clear upload staging pointer — it pointed into the now-reset arena. */
    upload_buf = NULL;

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

    /* Clear application variables. */
    memset(s->app_vars, 0, sizeof(s->app_vars));

    /* Reset ripscrip2 overflow state. */
    ripscrip2_init(&rip2_state);

    /* Reset Drawing Port table — deallocate all ports except port 0.
     * Port 0 gets its state refreshed to defaults; other slots are cleared. */
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
        p0->alpha        = 35;
    }
    s->active_port = 0;
    draw_set_clip(0, 0, 639, 399);
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

/* Map BGI fill_style to card pattern_id. Returns -1 for EMPTY_FILL. */
static int8_t bgi_fill_to_card(uint8_t bgi_style) {
    if (bgi_style == 0) return -1; /* EMPTY — caller should not fill */
    if (bgi_style == 1) return 0;  /* SOLID → card 0 (fast path) */
    if (bgi_style >= 2 && bgi_style <= 7) return bgi_style - 1; /* LINE..LTBKSLASH → 1..6 */
    if (bgi_style == 8) return 7;  /* XHATCH → card 7 (closest cross-hatch variant) */
    if (bgi_style == 9) return 8;  /* INTERLEAVE → card 8 (native, was approximate) */
    if (bgi_style == 10) return 9; /* WIDE_DOT → card 9 (native, was approximate) */
    if (bgi_style == 11) return 10;/* CLOSE_DOT → card 10 (native, was approximate) */
    if (bgi_style == 12) return 11;/* USER → card user pattern */
    return 0; /* fallback: solid */
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
                vval_len = (int)strnlen(s->host_date, sizeof(s->host_date) - 1);
                if (vval_len > (int)sizeof(val) - 1) vval_len = (int)sizeof(val) - 1;
                memcpy(val, s->host_date, vval_len);
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
                vval_len = (int)strnlen(s->host_time, sizeof(s->host_time) - 1);
                if (vval_len > (int)sizeof(val) - 1) vval_len = (int)sizeof(val) - 1;
                memcpy(val, s->host_time, vval_len);
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
        } else if (vlen == 4 && vname[0] == 'A' && vname[1] == 'P' &&
                   vname[2] == 'P' && vname[3] >= '0' && vname[3] <= '9') {
            /* $APP0$-$APP9$ — application-defined variables */
            int idx = vname[3] - '0';
            vval_len = (int)strnlen(s->app_vars[idx], sizeof(s->app_vars[0]));
            if (vval_len > (int)sizeof(val) - 1) vval_len = (int)sizeof(val) - 1;
            memcpy(val, s->app_vars[idx], vval_len);

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
         * DLL ripTextVarEngine @ 0x026218, handler @ 0x0390B0 ("WOYM").
         * Algorithm: (yday + 6 - (wday + 6) % 7) / 7 + 1, clamped 01-53. */
        } else if (vlen == 4 && memcmp(vname, "WOYM", 4) == 0) {
            int week = 0;
            if (s->host_date[0] != '\0') {
                /* Parse host_date "MM/DD/YY" and compute day-of-year + wday.
                 * Simplified: use a rough estimate since we don't have mktime
                 * from a "MM/DD/YY" string without a full calendar library.
                 * Fall through to RTC path which has full struct tm. */
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                int yday = tm->tm_yday;
                int wday = tm->tm_wday; /* 0=Sunday */
                week = (yday + 6 - (wday + 6) % 7) / 7 + 1;
                if (week < 1) week = 1;
                if (week > 53) week = 53;
            } else {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                int yday = tm->tm_yday;
                int wday = tm->tm_wday; /* 0=Sunday */
                week = (yday + 6 - (wday + 6) % 7) / 7 + 1;
                if (week < 1) week = 1;
                if (week > 53) week = 53;
            }
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

        /* $ABORT$ — abort the current RIPscrip scene (reset state).
         * DLL: sets abort flag; parser discards remaining stream bytes.
         * A2GSPU: reset the FSM to IDLE so the next !| starts fresh. */
        } else if (vlen == 5 && memcmp(vname, "ABORT", 5) == 0) {
            s->state = RIP_ST_IDLE;
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
            s->cmd_len = 0;
            val[0] = '\0';
            vval_len = 0;
        }

        if (vval_len >= 0) {
            /* Recognized variable — substitute its value */
            int copy = vval_len;
            if (o + copy > max_out - 1) copy = max_out - 1 - o;
            memcpy(out + o, val, copy);
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
    int char_w = 8, char_h = 16;

    if (ch == '\r') {
        s->tw_cur_x = s->tw_x0;
        return;
    }
    if (ch == '\n') {
        s->tw_cur_y += char_h;
        /* Scroll if past bottom of text window */
        if (s->tw_cur_y + char_h > tw_y1_s) {
            int16_t tw_y0_s = scale_y(s->tw_y0);
            draw_copy_rect(s->tw_x0, tw_y0_s + char_h,
                           s->tw_x0, tw_y0_s,
                           tw_x1_s - s->tw_x0 + 1,
                           tw_y1_s - tw_y0_s + 1 - char_h);
            /* Clear the last line */
            draw_set_color(0);
            draw_rect(s->tw_x0, tw_y1_s - char_h + 1,
                      tw_x1_s - s->tw_x0 + 1, char_h, true);
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

    /* Draw the character */
    uint8_t tc = s->palette[s->draw_color & 0x0F];
    draw_text(s->tw_cur_x, s->tw_cur_y, (const char *)&ch, 1,
              NULL, char_h, tc, 0xFF);
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

void rip_mouse_event_ext(int16_t x, int16_t y, bool clicked) {
    rip_state_t *s = g_rip_state;
    if (!s || !clicked) return;

    for (int i = 0; i < s->num_mouse_regions; i++) {
        rip_mouse_region_t *r = &s->mouse_regions[i];

        /* DLL: field must have MF_ACTIVE(0x04) set to be hit-testable.
         * rip_mouse.c field record +0x20, pFieldBuf+0x20 |= MF_ACTIVE */
        if (!(r->flags & RIP_MF_ACTIVE)) continue;
        if (!r->active) continue;

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
                          r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, true);
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

/* ══════════════════════════════════════════════════════════════════
 * FILE UPLOAD — receive BMP/ICN data from host for PSRAM caching
 * ══════════════════════════════════════════════════════════════════ */

void rip_file_upload_begin(uint8_t name_len) {
    if (!upload_buf && g_rip_state)
        upload_buf = (uint8_t *)psram_arena_alloc(&g_rip_state->psram_arena,
                                                  FILE_UPLOAD_MAX);
    upload_pos = 0;
    upload_name_len = 0;
    upload_reading_name = (name_len > 0);
    /* Name bytes follow as FILE_UPLOAD_DATA writes */
}

void rip_file_upload_byte(uint8_t b) {
    if (upload_reading_name) {
        if (upload_name_len < 15) {
            upload_name[upload_name_len++] = b;
            upload_name[upload_name_len] = '\0';
        }
        if (b == '\0' || upload_name_len >= 12) {
            upload_reading_name = false; /* name complete, data follows */
        }
        return;
    }
    if (upload_buf && upload_pos < FILE_UPLOAD_MAX)
        upload_buf[upload_pos++] = b;
}

void rip_file_upload_end(void) {
    if (!upload_buf || upload_pos < 4) return;

    /* RAF archive support — requires rip_raf.h (not in standalone RIPlib) */
#ifdef RIPLIB_HAS_RAF
    /* Try RAF archive first ("SQSH" magic at byte 0x10 in the 0x64-byte header).
     * DLL ground truth: ripResFileReadIndex (RVA 0x0648B9) validates the magic
     * at buf[0x10] and decodes each 0x13-byte index entry via XOR (sub_0756C4).
     * When the host transfers a .RAF archive, we unpack every member into the
     * PSRAM icon cache so subsequent LOAD_ICON commands find them immediately. */
    if (upload_pos >= 0x64 + 4 &&
        (uint32_t)(upload_buf[0x10]       |
                  ((uint32_t)upload_buf[0x11] << 8) |
                  ((uint32_t)upload_buf[0x12] << 16) |
                  ((uint32_t)upload_buf[0x13] << 24)) == RAF_MAGIC_SQSH) {

        raf_archive_t raf;
        if (g_rip_state &&
            raf_open(&raf, upload_buf, (uint32_t)upload_pos,
                     &g_rip_state->psram_arena)) {

            /* Allocate a decompression scratch buffer from the arena.
             * 64 KB covers the largest icon likely to appear in a RAF archive.
             * DLL: rafDecompressEntry uses a 0x400-byte ring + 0x200 input chunk;
             * we decompress into a flat buffer and hand off to the existing BMP/ICN
             * parsers. (RVA 0x064D68 rafDecompressEntry, RVA 0x06522A rafInflateBlock) */
            const uint32_t RAF_SCRATCH_MAX = 64u * 1024u;
            uint8_t *scratch = (uint8_t *)psram_arena_alloc(
                                   &g_rip_state->psram_arena, RAF_SCRATCH_MAX);

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
                        rip_icon_cache_bmp(me->name, (int)strlen(me->name),
                                           scratch, (int)nbytes);
                    } else if (nbytes >= 6) {
                        /* Try ICN (BGI putimage format) */
                        uint16_t icn_w = (uint16_t)((scratch[0] | (scratch[1] << 8)) + 1);
                        uint16_t icn_h = (uint16_t)((scratch[2] | (scratch[3] << 8)) + 1);
                        uint32_t pixel_count = (uint32_t)icn_w * icn_h;
                        if (icn_w <= 640 && icn_h <= 400 &&
                            pixel_count <= RIP_CLIPBOARD_MAX) {
                            uint8_t *pixels = (uint8_t *)psram_arena_alloc(
                                                  &g_rip_state->psram_arena, pixel_count);
                            if (pixels && rip_icn_parse(scratch, (int)nbytes,
                                                        pixels, &icn_w, &icn_h)) {
                                rip_icon_cache_pixels(me->name, (int)strlen(me->name),
                                                      pixels, icn_w, icn_h);
                            }
                        }
                    }
                }
            }
            /* raf.entries lives in the arena — no explicit free needed. */
        }
        upload_pos = 0;
        upload_name_len = 0;
        return;
    }
#endif /* RIPLIB_HAS_RAF */

    /* Try BMP first (check 'BM' magic) */
    if (upload_buf[0] == 'B' && upload_buf[1] == 'M') {
        rip_icon_cache_bmp(upload_name, upload_name_len,
                           upload_buf, upload_pos);
    } else {
        /* Try ICN (BGI putimage format — no magic, just validate header) */
        uint16_t icn_w, icn_h;
        uint32_t pixel_count;
        if (upload_pos >= 6) {
            icn_w = (upload_buf[0] | (upload_buf[1] << 8)) + 1;
            icn_h = (upload_buf[2] | (upload_buf[3] << 8)) + 1;
            pixel_count = (uint32_t)icn_w * icn_h;
            if (icn_w <= 640 && icn_h <= 400 && pixel_count <= RIP_CLIPBOARD_MAX) {
                uint8_t *pixels = g_rip_state
                    ? (uint8_t *)psram_arena_alloc(&g_rip_state->psram_arena, pixel_count)
                    : NULL;
                if (pixels && rip_icn_parse(upload_buf, upload_pos,
                                            pixels, &icn_w, &icn_h)) {
                    /* Manually add to icon cache */
                    extern bool rip_icon_cache_pixels(const char *name, int name_len,
                                                      uint8_t *pixels, uint16_t w, uint16_t h);
                    rip_icon_cache_pixels(upload_name, upload_name_len,
                                          pixels, icn_w, icn_h);
                }
            }
        }
    }

    upload_pos = 0;
    upload_name_len = 0;
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

    /* != must be checked before = to avoid splitting "!=" on the '=' */
    char *op = strstr(expanded, "!=");
    if (op) {
        *op = '\0';
        return strcmp(expanded, op + 2) != 0;
    }
    op = strchr(expanded, '=');
    if (op) {
        *op = '\0';
        return strcmp(expanded, op + 1) == 0;
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

/* ══════════════════════════════════════════════════════════════════
 * COMMAND EXECUTION
 * ══════════════════════════════════════════════════════════════════ */

static void apply_draw_state(rip_state_t *s) {
    draw_set_color(s->palette[s->draw_color & 0x0F]);
    draw_set_write_mode(s->write_mode);
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
         * total).  Command letters and argument counts not fully documented;
         * accept and ignore to prevent ERROR_RECOVERY consuming the frame. */
        switch (s->cmd_char) {
            default: break;
        }
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
        ripscrip2_execute(&rip2_state, s, ctx, s->cmd_char,
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
                if (tlen > 0) memcpy(r->text, p + text_start, tlen);
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
                bs->orient     = mega2(p + 4);
                bs->flags      = (uint16_t)mega4(p + 6); /* 4-digit MegaNum */
                bs->bev_size   = mega2(p + 10);
                bs->dfore      = mega2(p + 12) & 0x0F;
                bs->dback      = mega2(p + 14) & 0x0F;
                bs->bright     = mega2(p + 16) & 0x0F;
                bs->dark       = mega2(p + 18) & 0x0F;
                bs->surface    = mega2(p + 20) & 0x0F;
                bs->grp_no     = mega2(p + 22);
                bs->flags2     = mega2(p + 24);
                bs->uline_col  = mega2(p + 26) & 0x0F;
                bs->corner_col = mega2(p + 28) & 0x0F;
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
                int16_t bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
                int bev = bs->bev_size ? bs->bev_size : 2;

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
                for (int i = 0; i < bev; i++) {
                    draw_hline(bx0 + i,     by0 + i, bw - 2 * i - 1); /* top, full */
                    draw_vline(bx0 + i, by0 + i + 1, bh - 2 * i - 2); /* left, skip top corner */
                }
                /* Shadow (bottom + right) */
                draw_set_color(dk);
                for (int i = 0; i < bev; i++) {
                    draw_hline(bx0 + i + 1,     by1 - i, bw - 2 * i - 1); /* bottom, skip left corner */
                    draw_vline(    bx1 - i, by0 + i + 1, bh - 2 * i - 2); /* right, skip top corner */
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
                int16_t label_y = by0 + (bh - 16) / 2; /* default: label centered */
                if (icon_len > 0) {
                    char icon_name[RIP_FILE_NAME_MAX + 1];
                    int nlen = icon_len < RIP_FILE_NAME_MAX ? icon_len : RIP_FILE_NAME_MAX;
                    memcpy(icon_name, icon_text, nlen);
                    icon_name[nlen] = '\0';

                    rip_icon_t icon;
                    if (rip_icon_lookup(icon_name, nlen, &icon)) {
                        int16_t ix = bx0 + (bw - (int16_t)icon.width) / 2;
                        int16_t iy = by0 + (bh - (int16_t)icon.height) / 2;
                        if (label_len > 0) {
                            /* Icon in upper half; label drawn below */
                            iy = by0 + bev + 2;
                            label_y = iy + (int16_t)icon.height + 2;
                        }
                        draw_restore_region(ix, iy, (int16_t)icon.width,
                                            (int16_t)icon.height, icon.pixels);
                        icon_drawn = true;
                    } else {
                        /* Icon not in cache — queue request for file transfer */
                        rip_icon_request_file(icon_name, nlen);
                    }
                }
                (void)icon_drawn;

                /* Draw display label centered (or below icon if icon present) */
                if (label_len > 0) {
                    char lbuf[128];
                    int llen = unescape_text(label_text, label_len > 127 ? 127 : label_len, lbuf);
                    if (llen > 0) {
                        uint8_t tc = s->palette[bs->dfore & 0x0F];
                        int16_t tx = bx0 + (bw - llen * 8) / 2;
                        draw_text(tx, label_y, lbuf, llen, cp437_8x16, 16, tc, 0xFF);
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
                    memcpy(r->text, host_text, host_len);
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
                if (gw > 0 && gh > 0 && (uint32_t)(gw * gh) <= RIP_CLIPBOARD_MAX) {
                    /* Lazy-allocate clipboard from session arena */
                    if (!s->clipboard.data)
                        s->clipboard.data = (uint8_t *)psram_arena_alloc(&s->psram_arena,
                                                                          RIP_CLIPBOARD_MAX);
                    if (s->clipboard.data) {
                        draw_save_region(gx0, gy0, gw, gh, s->clipboard.data);
                        s->clipboard.width = gw;
                        s->clipboard.height = gh;
                        s->clipboard.valid = true;
                    }
                }
            }
            break;
        case 'P': /* RIP_PUT_IMAGE — paste clipboard to screen
                   * x:2 y:2 mode:2 res:1 */
            if (len >= 5 && s->clipboard.valid && s->clipboard.data) {
                int16_t px = mega2(p), py = scale_y(mega2(p + 2));
                /* mode at p+4: 0=COPY, 1=XOR, 2=OR, 3=AND, 4=NOT */
                uint8_t mode = (len >= 6) ? mega2(p + 4) : 0;
                draw_set_write_mode(mode <= 2 ? mode : 0);
                draw_restore_region(px, py, s->clipboard.width,
                                    s->clipboard.height, s->clipboard.data);
                draw_set_write_mode(s->write_mode); /* restore */
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
        case 't': /* RIP_REGION_TEXT — one line in text block
                   * justify:1, text */
            if (s->text_block.active && len >= 1) {
                /* justify: p[0] (0=left, 1=justified — we do left for now) */
                int tstart = 1;
                if (tstart < len) {
                    char tbuf[256];
                    int tlen = unescape_text(p + tstart, len - tstart, tbuf);
                    uint8_t tc = s->palette[s->draw_color & 0x0F];
                    draw_text(s->text_block.x0, s->text_block.cur_y,
                              tbuf, tlen, NULL, 16, tc, 0xFF);
                    s->text_block.cur_y += 16; /* advance one line */
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
                /* mode at p+4, clipboard at p+6, res at p+7:8 */
                int fname_start = 9;
                int fname_len = len - fname_start;
                if (fname_len > 0) {
                    const char *path = p + fname_start;

                    /* SV3-8: Path traversal protection.
                     * Reject any filename that contains ".." (directory escape),
                     * or begins with an absolute-path character ('/' or '\').
                     * The icon cache accepts only bare filenames (no directory
                     * components), so these patterns are always illegitimate. */
                    {
                        char path_chk[65];
                        int chk_len = fname_len < 64 ? fname_len : 64;
                        memcpy(path_chk, path, chk_len);
                        path_chk[chk_len] = '\0';
                        if (strstr(path_chk, "..") ||
                            path_chk[0] == '/'     ||
                            path_chk[0] == '\\') {
                            break; /* SV3-8: reject path traversal attempt */
                        }
                    }

                    rip_icon_t icon;
                    if (rip_icon_lookup(path, fname_len, &icon)) {
                        /* Blit icon to framebuffer */
                        draw_restore_region(ix, iy, icon.width, icon.height,
                                            icon.pixels);
                        /* If clipboard flag set, also copy to clipboard */
                        if (mega_digit(p[6]) && !s->clipboard.data)
                            s->clipboard.data = (uint8_t *)psram_arena_alloc(&s->psram_arena,
                                                                              RIP_CLIPBOARD_MAX);
                        if (mega_digit(p[6]) && s->clipboard.data) {
                            int sz = icon.width * icon.height;
                            if ((uint32_t)sz <= RIP_CLIPBOARD_MAX) {
                                memcpy(s->clipboard.data, icon.pixels, sz);
                                s->clipboard.width = icon.width;
                                s->clipboard.height = icon.height;
                                s->clipboard.valid = true;
                            }
                        }
                    } else {
                        /* Icon not found — queue file request + draw placeholder */
                        rip_icon_request_file(path, fname_len);
                        draw_set_color(s->palette[8]);
                        draw_rect(ix, iy, 32, 32, false);
                        draw_set_color(s->palette[s->draw_color & 0x0F]);
                    }
                }
            }
            break;
        case 'W': /* RIP_WRITE_ICON — no-op on embedded card (no writable fs) */
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
                    memcpy(snd_buf + 1, fname, copy);
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
                    memcpy(snd_buf + 1, fname, copy);
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
                memcpy(s->icon_dir, p + 2, plen);
                s->icon_dir[plen] = '\0';
            }
            break;

        /* ── Font loading stub ──────────────────────────────────── */
        case 'O': /* RIP_FONT_LOAD — load BGI/RFF font from file.
                   * DLL: loads font into the per-instance font table.
                   * A2GSPU: all fonts are pre-compiled in flash; ignore.
                   * TODO: if a CHR filename matches a known slot, record it. */
            /* stub — no dynamic font loading on embedded card */
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
                    rlen = (int)strnlen(s->app_vars[idx], sizeof(s->app_vars[0]));
                    if (rlen > 0)
                        card_tx_push(s->app_vars[idx], rlen);
                } else {
                    /* Unrecognised extended query — reply with empty string */
                    (void)resp; (void)rlen;
                    card_tx_push("\r", 1);
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
                /* Normalize per DLL convention */
                if (vx0 > vx1) { int16_t t = vx0; vx0 = vx1; vx1 = t; }
                if (vy0 > vy1) { int16_t t = vy0; vy0 = vy1; vy1 = t; }
                s->vp_x0 = vx0;
                s->vp_y0 = scale_y(vy0);
                s->vp_x1 = vx1;
                s->vp_y1 = scale_y1(vy1);
                s->viewport_scale = (uint8_t)mega_digit(p[8]);
                /* Fix: draw_set_clip takes corners, not width/height */
                draw_set_clip(s->vp_x0, s->vp_y0, s->vp_x1, s->vp_y1);
            }
            break;

        /* ── Extended clipboard operations ──────────────────────── */
        case 'X': /* RIP_CLIPBOARD_OP — extended clipboard operations.
                   * DLL: provides compound clipboard operations (blend, mask,
                   * flip, rotate) beyond the basic GET/PUT_IMAGE pair.
                   * Format: op:2 [params vary by op].
                   * TODO: implement blend/flip when draw layer supports it. */
            /* stub — basic GET/PUT via 1C/1P already implemented */
            break;

        /* ── Scene / file operations (no filesystem) ──────────── */
        case 'R': /* RIP_READ_SCENE — no-op (no scene file loading) */
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
                if (flen > (int)sizeof(fname_buf) - 1)
                    flen = (int)sizeof(fname_buf) - 1;
                memcpy(fname_buf, fname, flen);
                fname_buf[flen] = '\0';

                rip_icon_t icon;
                if (rip_icon_lookup(fname, flen, &icon)) {
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
                    rip_icon_request_file(fname, flen);
                }
                (void)mode; /* mode byte reserved for future size/date filtering */
            }
            break;

        /* ── Text variables (DEFINE / QUERY) ──────────────────── */
        case 'D': /* RIP_DEFINE — define text variable
                   * flags:3 res:2 text (format: varname[,width]:?prompt?[default])
                   * For $APP0$-$APP9$: store default value in app_vars[].
                   * For other variables: display the prompt/default as text. */
            if (len >= 5) {
                /* Skip flags:3 + res:2, process remaining text */
                int tstart = 5;
                if (tstart < len) {
                    char tbuf[128];
                    int tlen = unescape_text(p + tstart, len - tstart > 127 ? 127 : len - tstart, tbuf);
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

                    /* Check if variable name is $APPn$ (appears before ':' or '?') */
                    int colon = -1;
                    for (int i = 0; i < tlen; i++) {
                        if (tbuf[i] == ':' || tbuf[i] == '?') { colon = i; break; }
                    }
                    int name_end = (colon >= 0) ? colon : tlen;
                    if (name_end >= 6 && tbuf[0] == '$' && tbuf[1] == 'A' &&
                        tbuf[2] == 'P' && tbuf[3] == 'P' &&
                        tbuf[4] >= '0' && tbuf[4] <= '9' && tbuf[5] == '$') {
                        /* Store default value into app_vars[idx] */
                        int idx = tbuf[4] - '0';
                        int vlen = dlen < 31 ? dlen : 31;
                        memcpy(s->app_vars[idx], display, vlen);
                        s->app_vars[idx][vlen] = '\0';
                    } else {
                        if (dlen > 0) {
                            uint8_t tc = s->palette[s->draw_color & 0x0F];
                            draw_text(s->draw_x, s->draw_y, display, dlen,
                                      NULL, 16, tc, 0xFF);
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
                    rlen = (int)strnlen(s->app_vars[idx], sizeof(s->app_vars[0]));
                    if (rlen == 0 && !s->query_pending) {
                        /* Empty variable — BBS wants user input.  Start round-trip:
                         * push 0x3E (CMD_QUERY_PROMPT marker) + variable name + NUL
                         * so the IIgs bridge loop can show an input form. */
                        char prompt_buf[40];
                        int plen = 0;
                        prompt_buf[plen++] = (char)0x3E;  /* CMD_QUERY_PROMPT marker */
                        /* Copy variable name ("$APPn$") as the prompt label */
                        {
                            int k;
                            for (k = 0; k < vlen && k < 6 && plen < 38; k++)
                                prompt_buf[plen++] = vname[k];
                        }
                        prompt_buf[plen++] = '\0';
                        card_tx_push(prompt_buf, plen);
                        /* Record pending state — main.c CMD_QUERY_RESPONSE handler
                         * will fill s->app_vars[idx] when the IIgs responds. */
                        s->query_pending = true;
                        {
                            int k;
                            for (k = 0; k < vlen && k < 31; k++)
                                s->query_var_name[k] = vname[k];
                            s->query_var_name[k < 31 ? k : 31] = '\0';
                        }
                        s->query_response_len = 0;
                        /* Do not push a response to the BBS yet */
                        rlen = -1;  /* sentinel: skip card_tx_push below */
                    } else {
                        memcpy(resp, s->app_vars[idx], rlen);
                    }

                /* $OVERFLOW(RESET)$ — reset to first page */
                } else if (vlen >= 18 &&
                    memcmp(vname, "$OVERFLOW(RESET)$", 17) == 0) {
                    rip2_state.overflow_page = 0;
                    rlen = 0;

                /* $OVERFLOW(NEXT)$ — advance one page */
                } else if (vlen >= 17 &&
                    memcmp(vname, "$OVERFLOW(NEXT)$", 16) == 0) {
                    if (rip2_state.overflow_page + 1 < rip2_state.overflow_total)
                        rip2_state.overflow_page++;
                    rlen = 0;

                /* $OVERFLOW(PREV)$ — back one page */
                } else if (vlen >= 17 &&
                    memcmp(vname, "$OVERFLOW(PREV)$", 16) == 0) {
                    if (rip2_state.overflow_page > 0)
                        rip2_state.overflow_page--;
                    rlen = 0;

                /* $OVERFLOW$ — return "page/total" string */
                } else if (vlen >= 11 &&
                    memcmp(vname, "$OVERFLOW$", 10) == 0) {
                    rlen = snprintf(resp, sizeof(resp), "%u/%u",
                                   rip2_state.overflow_page + 1,
                                   rip2_state.overflow_total);
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
            s->fill_pattern = mega2(p);
            s->fill_color = mega2(p + 2) & 0x0F;
            int8_t card_pat = bgi_fill_to_card(s->fill_pattern);
            if (card_pat >= 0)
                draw_set_fill_style(card_pat, s->palette[s->fill_color]);
        }
        break;
    case '=': /* RIP_LINE_STYLE: style:2, user_pat:4, thick:2 */
        if (len >= 2) {
            s->line_style = mega2(p);
            if (len >= 8) s->line_thick = scale_y(mega2(p + 6)); /* Fix B6: scale thickness to card Y */
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
            uint8_t wm = mega2(p);
            if (wm > 4) wm = 0;
            s->write_mode = wm;
            draw_set_write_mode(wm);
        }
        break;
    case 'Y': /* RIP_FONT_STYLE — font:2 dir:2 size:2 flags:2 */
        if (len >= 6) {
            uint8_t fid = mega2(p);
            uint8_t fdir = mega2(p + 2);
            uint8_t fsize = mega2(p + 4);
            /* Validate: dir 0-2 (v3.1 adds dir=2 CCW), size 1-10 */
            if (fdir > 2) break;
            if (fsize < 1 || fsize > 10) break;
            s->font_id = fid;
            s->font_dir = fdir;
            s->font_size = fsize;
            /* v3.0 extension: justification flags in arg[3] (reserved in v1.54) */
            if (len >= 8) {
                uint8_t flags = mega2(p + 6);
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
        /* Windows + viewport → full defaults */
        s->tw_x0 = 0; s->tw_y0 = 0;
        s->tw_x1 = 639; s->tw_y1 = 349;
        s->tw_wrap = 0; s->tw_font_size = 0;
        s->tw_active = false;
        s->vp_x0 = 0; s->vp_y0 = 0;
        s->vp_x1 = 639; s->vp_y1 = 349;
        draw_reset_clip();
        /* Drawing state → defaults */
        s->draw_color = 15; /* white */
        s->draw_x = 0; s->draw_y = 0;
        s->write_mode = 0;
        s->line_style = 0; s->line_thick = 1;
        draw_set_line_style(0xFF, 1);
        s->fill_pattern = 1; s->fill_color = 15; /* Fix B4: BGI SOLID_FILL, white (DLL default) */
        draw_set_fill_style(0, 0); /* card 0 = solid */
        s->font_id = 0; s->font_dir = 0; s->font_size = 1;
        /* Palette → EGA defaults (offset to 240-255 to avoid xterm conflict) */
        for (int i = 0; i < 16; i++) {
            s->palette[i] = 240 + i;
            palette_write_rgb565(240 + i, ega_default_rgb565[i]);
        }
        /* Mouse regions → cleared */
        s->num_mouse_regions = 0;
        /* Level 1 state → cleared */
        memset(&s->button_style, 0, sizeof(s->button_style));
        s->clipboard.valid = false;
        s->text_block.active = false;
        /* Screen → cleared directly. Also reset rip_has_drawn so that
         * any ANSI ESC[2J arriving before the next RIP drawing command
         * is allowed to clear (e.g., between login screens). */
        s->rip_has_drawn = false;
        s->cursor_repositioned = false;
        draw_fill_screen(0);
        comp_set_cursor(c, 0, 0);
        break;
    case 'e': /* v1.54 spec §3.1: '|e' = RIP_ERASE_WINDOW — clear text window to background,
               * move cursor to upper-left of text window. IcyTerm: EraseWindow (0 args).
               * Note: previous "Fix B5" had this backwards; 'e' is always text-window clear. */
        comp_clear_screen(c, 2);
        break;
    case 'E': /* v1.54 spec §3.2: '|E' = RIP_ERASE_VIEW — clear graphics viewport to background.
               * IcyTerm: EraseView (0 args). Note: previous "Fix B5" had this backwards. */
        draw_set_color(0);
        { int16_t ey0 = scale_y(s->vp_y0), ey1 = scale_y1(s->vp_y1);
          draw_rect(s->vp_x0, ey0,
                    s->vp_x1 - s->vp_x0 + 1, ey1 - ey0 + 1, true);
        }
        break;
    case '>': /* v1.54 spec §3.3: '|>' = RIP_ERASE_EOL — erase from text cursor to end of line.
               * IcyTerm: EraseEOL (0 args).
               * Note: previous code had '>' as NoMore and '#' as EraseWindow — both wrong. */
        comp_clear_line(c, 0);
        break;
    case 'w': /* RIP_TEXT_WINDOW — pixel coordinates for text region */
        if (len >= 10) {
            s->tw_x0 = mega2(p); s->tw_y0 = mega2(p + 2);
            s->tw_x1 = mega2(p + 4); s->tw_y1 = mega2(p + 6);
            s->tw_wrap = mega_digit(p[8]);
            s->tw_font_size = mega_digit(p[9]);
            /* Initialize cursor to top-left of text window (scaled) */
            s->tw_cur_x = s->tw_x0;
            s->tw_cur_y = scale_y(s->tw_y0);
            /* Non-default text window → route passthrough text to draw_text */
            s->tw_active = (s->tw_x0 != 0 || s->tw_y0 != 0 ||
                            s->tw_x1 != 639 || s->tw_y1 != 349);
        }
        break;
    case 'v': /* RIP_VIEWPORT */
        if (len >= 8) {
            int16_t vx0 = mega2(p), vy0 = mega2(p + 2);
            int16_t vx1 = mega2(p + 4), vy1 = mega2(p + 6);
            /* Normalize: ensure x0<=x1, y0<=y1 (DLL calls sub_03112E) */
            if (vx0 > vx1) { int16_t t = vx0; vx0 = vx1; vx1 = t; }
            if (vy0 > vy1) { int16_t t = vy0; vy0 = vy1; vy1 = t; }
            s->vp_x0 = vx0; s->vp_y0 = vy0;
            s->vp_x1 = vx1; s->vp_y1 = vy1;
            draw_set_clip(vx0, scale_y(vy0), vx1, scale_y1(vy1));
        }
        break;

    /* ── Palette ─────────────────────────────────────────────── */
    case 'Q': /* RIP_SET_PALETTE — 16 entries, values are EGA 64-color indices.
               * EGA indices map to framebuffer values 240-255 (see rip_init_first). */
        if (len >= 32) {
            for (int i = 0; i < 16; i++) {
                uint8_t ega64 = mega2(p + i * 2) & 0x3F;
                palette_write_rgb565(240 + i, ega64_to_rgb565(ega64));
            }
        }
        break;
    case 'a': /* RIP_ONE_PALETTE — set one entry to EGA 64-color index */
        if (len >= 4) {
            uint8_t idx = mega2(p) & 0x0F;
            uint8_t ega64 = mega2(p + 2) & 0x3F;
            palette_write_rgb565(240 + idx, ega64_to_rgb565(ega64));
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
            draw_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, false);
        }
        break;
    case 'B': /* RIP_BAR (filled rectangle, no border) — uses fill style */
        if (len >= 8 && s->fill_pattern != 0) {
            int16_t x0 = mega2(p), y0 = scale_y(mega2(p + 2));
            int16_t x1 = mega2(p + 4), y1 = scale_y1(mega2(p + 6));
            draw_set_color(s->palette[s->fill_color & 0x0F]);
            draw_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, true);
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
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_ellipse(cx, cy, rx, ry_s, true);
            }
            draw_set_color(s->palette[s->draw_color & 0x0F]);
            draw_ellipse(cx, cy, rx, ry_s, false);
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
    case 'V': /* RIP_OVAL_ARC */
        if (len >= 12) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t rx = mega2(p + 8), ry = mega2(p + 10);
            draw_elliptical_arc(cx, cy, rx, scale_y(ry), sa, ea);
        }
        break;

    /* ── Pie slices ──────────────────────────────────────────── */
    case 'I': /* RIP_PIE_SLICE — outline in draw_color, fill in fill_color.
               * DLL scales radius via ripScaleCoordY (EGA 350→400). */
        if (len >= 10) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t r = scale_y(mega2(p + 8));
            uint8_t dc = s->palette[s->draw_color & 0x0F];
            draw_pie(cx, cy, r, sa, ea, false);
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_pie(cx, cy, r, sa, ea, true);
                draw_set_color(dc);
                draw_pie(cx, cy, r, sa, ea, false);
            }
        }
        break;
    case 'i': /* RIP_OVAL_PIE_SLICE — outline + fill */
        if (len >= 12) {
            int16_t cx = mega2(p), cy = scale_y(mega2(p + 2));
            int16_t sa = mega2(p + 4), ea = mega2(p + 6);
            int16_t rx = mega2(p + 8), ry_s = scale_y(mega2(p + 10));
            uint8_t dc = s->palette[s->draw_color & 0x0F];
            draw_elliptical_pie(cx, cy, rx, ry_s, sa, ea, false);
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_elliptical_pie(cx, cy, rx, ry_s, sa, ea, true);
                draw_set_color(dc);
                draw_elliptical_pie(cx, cy, rx, ry_s, sa, ea, false);
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
                if (s->fill_pattern != 0) {
                    draw_set_color(s->palette[s->fill_color & 0x0F]);
                    draw_polygon(pts, npts, true);
                }
                draw_set_color(s->palette[s->draw_color & 0x0F]);
                draw_polygon(pts, npts, false);
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
        if (len > 0) {
            /* DLL ground truth (rip_textvars.c): variable substitution occurs
             * at the text output layer, after backslash unescape. Apply both. */
            char tbuf[256];
            char vbuf[256];
            int tlen = unescape_text(p, len, tbuf);
            tlen = rip_expand_variables(s, tbuf, tlen, vbuf, sizeof(vbuf));
            uint8_t tc = s->palette[s->draw_color & 0x0F];
            uint8_t fid = s->font_id;
            uint8_t fscale = s->font_size ? s->font_size : 1;
            int16_t adv;
            int16_t tx = s->draw_x, ty = s->draw_y;
            /* Measure text width for justification (DLL rip_textvars.c) */
            int16_t tw, th;
            if (fid > 0 && fid < BGI_FONT_COUNT && bgi_fonts_loaded && bgi_fonts[fid].strokes) {
                tw = bgi_font_string_width(&bgi_fonts[fid], vbuf, tlen, fscale);
                th = bgi_fonts[fid].top * fscale;
            } else {
                tw = tlen * 8;
                th = 16;
            }
            /* Apply horizontal justification */
            if (s->font_hjust == 1)      tx -= tw / 2;  /* center */
            else if (s->font_hjust == 2) tx -= tw;       /* right */
            /* Apply vertical justification (DLL: 0=bottom, 1=center, 2=top, 3=baseline) */
            if (s->font_vjust == 0)      ty -= th;       /* bottom: text above point */
            else if (s->font_vjust == 1) ty -= th / 2;   /* center */
            /* vjust 2 = top: ty unchanged (text below point) */
            /* vjust 3 = baseline: ty unchanged for bitmap fonts */
            if (fid > 0 && fid < BGI_FONT_COUNT && bgi_fonts_loaded && bgi_fonts[fid].strokes) {
                adv = bgi_font_draw_string_ex(&bgi_fonts[fid],
                    tx, ty, vbuf, tlen, fscale, tc, s->font_dir, s->font_attrib);
            } else {
                draw_text(tx, ty, vbuf, tlen,
                          cp437_8x16, 16, tc, 0xFF);
                adv = tw;
            }
            if (s->font_dir == 0) s->draw_x += adv;
            else                  s->draw_y += adv;
        }
        break;
    /* v1.54 spec: '@' = RIP_TEXT_XY — draw text at pixel position.
     * NOTE: Session 12 remapped this to RIP_PIXEL based on DLL command table
     * entry 16, but the DLL table maps internal function pointers, not protocol
     * letters.  The v1.54 spec and all BBSes use '@' for TEXT_XY. */
    case '@': /* RIP_TEXT_XY — x:2 y:2 text */
        if (len >= 4) {
            s->draw_x = mega2(p);
            s->draw_y = scale_y(mega2(p + 2));
            char tbuf_at[256];
            int tlen_at = unescape_text(p + 4, len - 4, tbuf_at);
            uint8_t tc_at = s->palette[s->draw_color & 0x0F];
            uint8_t fid_at = s->font_id;
            uint8_t fscale_at = s->font_size ? s->font_size : 1;
            int16_t adv_at;
            int16_t tx_at = s->draw_x, ty_at = s->draw_y;
            /* Measure + justify (same as RIP_TEXT) */
            int16_t tw_at, th_at;
            if (fid_at > 0 && fid_at < BGI_FONT_COUNT && bgi_fonts_loaded && bgi_fonts[fid_at].strokes) {
                tw_at = bgi_font_string_width(&bgi_fonts[fid_at], tbuf_at, tlen_at, fscale_at);
                th_at = bgi_fonts[fid_at].top * fscale_at;
            } else {
                tw_at = tlen_at * 8;
                th_at = 16;
            }
            if (s->font_hjust == 1)      tx_at -= tw_at / 2;
            else if (s->font_hjust == 2) tx_at -= tw_at;
            if (s->font_vjust == 0)      ty_at -= th_at;
            else if (s->font_vjust == 1) ty_at -= th_at / 2;
            if (fid_at > 0 && fid_at < BGI_FONT_COUNT && bgi_fonts_loaded && bgi_fonts[fid_at].strokes) {
                adv_at = bgi_font_draw_string_ex(&bgi_fonts[fid_at],
                    tx_at, ty_at, tbuf_at, tlen_at, fscale_at, tc_at, s->font_dir, s->font_attrib);
            } else {
                draw_text(tx_at, ty_at, tbuf_at, tlen_at,
                          cp437_8x16, 16, tc_at, 0xFF);
                adv_at = tw_at;
            }
            if (s->font_dir == 0) s->draw_x += adv_at;
            else                  s->draw_y += adv_at;
        }
        break;
    /* v1.54 spec: 'X' = RIP_PIXEL — draw single pixel at (x,y). */
    case 'X': /* RIP_PIXEL — x:2 y:2 */
        if (len >= 4) {
            draw_pixel(mega2(p), scale_y(mega2(p + 2)));
        }
        break;
    /* v1.54 spec: 't' = RIP_REGION_TEXT — display a line of text in a
     * previously defined text region (Level 0, used with 'T' begin/end). */
    case 't': /* RIP_REGION_TEXT — justify:1 text */
        if (s->text_block.active && len >= 1) {
            int tstart = 1;
            if (tstart < len) {
                char tbuf_t[256];
                int tlen_t = unescape_text(p + tstart, len - tstart, tbuf_t);
                uint8_t tc_t = s->palette[s->draw_color & 0x0F];
                draw_text(s->text_block.x0, s->text_block.cur_y,
                          tbuf_t, tlen_t, cp437_8x16, 16, tc_t, 0xFF);
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
            draw_set_fill_style(11, s->palette[s->fill_color]);
        }
        break;

    /* ── Scene control ───────────────────────────────────────── */
    case '#': /* v1.54 spec §3.4: '|#' = RIP_NO_MORE — end of RIPscrip scene.
               * IcyTerm: NoMore (0 args). BBS sends 3+ consecutive '#' commands for noise immunity.
               * Note: previous code had '#' as EraseWindow and '>' as NoMore — both wrong.
               * Correct mapping confirmed by v1.54 spec and IcyTerm command.rs:NoMore → "|#". */
        /* No-op: scene terminator; mouse regions already activated incrementally */
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
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_circle(cx, cy, r, true);
            }
            draw_set_color(s->palette[s->draw_color & 0x0F]);
            draw_circle(cx, cy, r, false);
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
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_rounded_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, r, true);
            }
            draw_set_color(s->palette[s->draw_color & 0x0F]);
            draw_rounded_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, r, false);
        }
        break;

    /* -- Background color (v2.0+) ---------------------------------------- */
    /* DLL command table entry 43: 'k' = RIP_BACK_COLOR (1 arg: COL) */
    case 'k': /* RIP_BACK_COLOR -- color:1 */
        if (len >= 1)
            s->back_color = mega_digit(p[0]) & 0x0F;
        break;

    /* -- Save icon (v2.0+) ----------------------------------------------- */
    /* DLL command table entry 40: 'J' = RIP_SAVE_ICON (1 arg: 2-digit slot) */
    case 'J': /* RIP_SAVE_ICON -- slot:2 */
        /* TODO: save clipboard contents to named icon slot.
         * Requires a persistent icon slot table indexed by mega2(p).
         * The clipboard must already be valid (populated by 1C GET_IMAGE).
         * No filesystem write is performed on this embedded card. */
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
                /* Clear the exposed strip left behind by the scroll */
                draw_set_color(s->palette[fc]);
                if (dy > 0)
                    draw_rect(sx0, sy0, rw, dy, true);
                else if (dy < 0)
                    draw_rect(sx0, sy1 + dy + 1, rw, -dy, true);
                if (dx > 0)
                    draw_rect(sx0, sy0, dx, rh, true);
                else if (dx < 0)
                    draw_rect(sx1 + dx + 1, sy0, -dx, rh, true);
                draw_set_color(s->palette[s->draw_color & 0x0F]);
            }
        }
        break;

    /* -- Copy region (v2.0+) --------------------------------------------- */
    /* DLL command table entry 8: ',' = RIP_COPY_REGION (10 args: XY*10) */
    case ',': /* RIP_COPY_REGION -- sx0:2 sy0:2 sx1:2 sy1:2 dx:2 dy:2 dx1:2 dy1:2 res:2 res:2 */
        if (len >= 20) {
            int16_t sx0 = mega2(p),      sy0 = scale_y(mega2(p + 2));
            int16_t sx1 = mega2(p + 4),  sy1 = scale_y1(mega2(p + 6));
            int16_t dx  = mega2(p + 8),  dy  = scale_y(mega2(p + 10));
            int16_t rw  = sx1 - sx0 + 1, rh  = sy1 - sy0 + 1;
            if (rw > 0 && rh > 0)
                draw_copy_rect(sx0, sy0, dx, dy, rw, rh);
        }
        break;

    /* -- Extended positioned text (v2.0+) -------------------------------- */
    /* DLL command table entry 9: '-' = RIP_TEXT_XY_EXT (5 args: XY,XY,XY,XY,2 + text) */
    case '-': /* RIP_TEXT_XY_EXT -- x0:2 y0:2 x1:2 y1:2 flags:2 text */
        if (len >= 10) {
            s->draw_x = mega2(p);
            s->draw_y = scale_y(mega2(p + 2));
            /* x1/y1 define a bounding box (p+4..p+7); flags at p+8 control justify.
             * Rendered without bounding clip -- same font path as RIP_TEXT_XY. */
            const char *tp = p + 10;
            int tlen = len - 10;
            if (tlen > 0) {
                char tbuf[256];
                int outlen = unescape_text(tp, tlen, tbuf);
                uint8_t tc = s->palette[s->draw_color & 0x0F];
                uint8_t fid = s->font_id;
                int16_t adv;
                if (fid > 0 && fid < BGI_FONT_COUNT && bgi_fonts_loaded &&
                    bgi_fonts[fid].strokes) {
                    adv = bgi_font_draw_string_ex(&bgi_fonts[fid],
                        s->draw_x, s->draw_y, tbuf, outlen,
                        s->font_size ? s->font_size : 1, tc, s->font_dir, s->font_attrib);
                } else {
                    draw_text(s->draw_x, s->draw_y, tbuf, outlen,
                              NULL, 16, tc, 0xFF);
                    adv = outlen * 8;
                }
                if (s->font_dir == 0) s->draw_x += adv;
                else                  s->draw_y += adv;
            }
        }
        break;

    /* -- Header (v2.0+) -------------------------------------------------- */
    /* DLL command table entry 32: 'h' = RIP_HEADER (3 args: 2,4,2) */
    case 'h': /* RIP_HEADER -- type:2 id:4 flags:2 */
        /* Sets page header/title metadata -- no visible output on card.
         * Accepted for stream hygiene; values are not stored. */
        break;

    /* -- Coordinate size (v2.0+) ----------------------------------------- */
    /* DLL command table entry 49: 'n' = RIP_SET_COORDINATE_SIZE (2 args: 1,3) */
    case 'n': /* RIP_SET_COORDINATE_SIZE -- mode:1 size:3 */
        /* Selects resolution mode for subsequent XY decoding.
         * 5 modes: 0=640x350 (EGA default), 1=640x480, 2=800x600,
         *          3=1024x768, 4=custom (size:3 = custom value).
         * Card is fixed at 640x400; all modes map to the same display.
         * Accept and ignore -- no coordinate scaling adjustment needed. */
        break;

    /* -- Color mode (v2.0+) ---------------------------------------------- */
    /* DLL command table entry 46: 'M' = RIP_SET_COLOR_MODE (2 args: 1,1) */
    case 'M': /* RIP_SET_COLOR_MODE -- mode:1 depth:1 */
        /* Selects color depth for subsequent color arguments.
         * mode=0: 4-bit EGA (default), mode=1: 8-bit VGA, mode=2: 18-bit VGA.
         * Card always uses 16-color EGA palette; higher depths fold to 4 bits.
         * Accept and ignore. */
        break;

    /* -- Border color (v2.0+) -------------------------------------------- */
    /* DLL command table entry 48: 'N' = RIP_SET_BORDER (1 arg: 2-digit) */
    case 'N': /* RIP_SET_BORDER -- color:2 */
        /* Sets screen border / overscan color.
         * Card has no overscan region; accepted and ignored. */
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
    /* DLL command table entry 4: '(' = RIP_GROUP_BEGIN (0 args) -- no-op stub */
    case '(':
    /* DLL command table entry 5: ')' = RIP_GROUP_END (0 args) -- no-op stub */
    case ')':
        /* Group begin/end markers support conditional rendering: a host can
         * omit a group if the client has a cached copy.  DLL handler is a
         * single RET -- group-skip logic lives in the parser layer.
         * A2GSPU: accept and ignore. */
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
            /* flags:2 at p+8 -- reserved, ignored */
            const char *tp = p + 10;
            int tlen = len - 10;
            if (tlen > 0 && bx1 > bx0 && by1 > by0) {
                char tbuf[256];
                int outlen = unescape_text(tp, tlen > 255 ? 255 : tlen, tbuf);
                uint8_t tc = s->palette[s->draw_color & 0x0F];
                int char_w = 8, char_h = 16;
                int cur_x = bx0, cur_y = by0;
                int i = 0;
                while (i < outlen && cur_y + char_h <= by1) {
                    int word_end = i;
                    while (word_end < outlen && tbuf[word_end] != ' ') word_end++;
                    int word_w = (word_end - i) * char_w;
                    if (cur_x + word_w > bx1 && cur_x > bx0) {
                        cur_x = bx0;
                        cur_y += char_h;
                        if (cur_y + char_h > by1) break;
                    }
                    draw_text(cur_x, cur_y, tbuf + i, word_end - i,
                              cp437_8x16, char_h, tc, 0xFF);
                    cur_x += word_w;
                    i = word_end;
                    if (i < outlen && tbuf[i] == ' ') {
                        cur_x += char_w;
                        i++;
                    }
                }
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
            /* mode:2 at p+8, param1:2 at p+10, param2:2 at p+12 -- ignored */
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_rect(ex0, ey0, ex1 - ex0 + 1, ey1 - ey0 + 1, true);
            }
            draw_set_color(s->palette[s->draw_color & 0x0F]);
            draw_rect(ex0, ey0, ex1 - ex0 + 1, ey1 - ey0 + 1, false);
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
         * to destination regions using the specified raster op.
         * TODO: implement all 5 pairs when multi-region compositing is needed.
         * Currently performs only the first src->dst blit. */
        if (len >= 20) {
            int16_t cx0 = mega2(p),      cy0 = scale_y(mega2(p + 2));
            int16_t cx1 = mega2(p + 4),  cy1 = scale_y1(mega2(p + 6));
            int16_t cdx = mega2(p + 8),  cdy = scale_y(mega2(p + 10));
            int16_t cw  = cx1 - cx0 + 1, ch  = cy1 - cy0 + 1;
            if (cw > 0 && ch > 0)
                draw_copy_rect(cx0, cy0, cdx, cdy, cw, ch);
            /* Pairs 2-5 (p+12..p+39) and mode (p+40) not yet processed */
        }
        break;

    /* DLL binary: '{' = RIP_ANIMATION_FRAME (6 args: XY x 6 = 3 vertex pairs) */
    case '{': /* RIP_ANIMATION_FRAME -- 3 coordinate pairs */
        /* Draws filled polygon interior AND outline in one operation
         * (DLL calls GDI Polygon + Polyline in sequence).
         * With 3 points this is a filled+outlined triangle. */
        if (len >= 12) {
            int16_t pts[6];
            pts[0] = mega2(p);     pts[1] = scale_y(mega2(p + 2));
            pts[2] = mega2(p + 4); pts[3] = scale_y(mega2(p + 6));
            pts[4] = mega2(p + 8); pts[5] = scale_y(mega2(p + 10));
            if (s->fill_pattern != 0) {
                draw_set_color(s->palette[s->fill_color & 0x0F]);
                draw_polygon(pts, 3, true);
            }
            draw_set_color(s->palette[s->draw_color & 0x0F]);
            draw_polygon(pts, 3, false);
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
            draw_set_fill_style(11, s->palette[s->fill_color]);
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
            if (gw > 0 && gh > 0 &&
                (uint32_t)gw * (uint32_t)gh <= RIP_CLIPBOARD_MAX) {
                if (!s->clipboard.data)
                    s->clipboard.data = (uint8_t *)psram_arena_alloc(
                                            &s->psram_arena, RIP_CLIPBOARD_MAX);
                if (s->clipboard.data) {
                    draw_save_region(gx0, gy0, gw, gh, s->clipboard.data);
                    s->clipboard.width  = gw;
                    s->clipboard.height = gh;
                    s->clipboard.valid  = true;
                }
            }
        }
        break;

    /* ── Icon display style (v2.0+) ──────────────────────────── */
    /* DLL command table entry 3: '&' = RIP_ICON_STYLE (5 args: XY,XY,2,2,2). */
    case '&': /* RIP_ICON_STYLE — x0:2 y0:2 x1:2 y1:2 style:2 align:2 scale:2 */
        /* TODO: icon display style controls scaling/alignment of subsequently
         * stamped icons.  Arguments not yet consumed.  Accepted to prevent
         * ERROR_RECOVERY from consuming the rest of the frame. */
        (void)len;
        break;

    /* ── Stamp icon from slot (v2.0+) ───────────────────────── */
    /* DLL command table entry 10: '.' = RIP_STAMP_ICON (6 args: XY×6). */
    case '.': /* RIP_STAMP_ICON — slot:2 x:2 y:2 w:2 h:2 flags:2 */
        /* Stamps the clipboard icon at (x,y).  The first arg is the icon slot
         * number (currently ignored — only the clipboard is supported). */
        if (len >= 8 && s->clipboard.valid && s->clipboard.data) {
            int16_t dx = mega2(p + 2);
            int16_t dy = scale_y(mega2(p + 4));
            draw_restore_region(dx, dy, s->clipboard.width, s->clipboard.height,
                                s->clipboard.data);
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
            s->tw_x0        = mega2(p);
            s->tw_y0        = mega2(p + 2);
            s->tw_x1        = mega2(p + 4);
            s->tw_y1        = mega2(p + 6);
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
         * Stored in s->font_attrib for use by text rendering paths.
         * BGI stroke font renderer support for attribs is future work. */
        if (len >= 2)
            s->font_attrib = (uint8_t)(mega2(p) & 0x0F);
        break;

    }
}

/* ══════════════════════════════════════════════════════════════════
 * BYTE PROCESSING — 13-state FSM matching RIPSCRIP.DLL
 *
 * DLL ground truth: ripParseStateMachine @ 0x10039E90
 * Jump table: 0x1003AB9C  (states 0-12, stored at pContext+0x00)
 * prevState saved at pContext+0x04 for line-continuation restore.
 * lastChar  saved at pContext+0x9F for '!' line-boundary detection.
 * ══════════════════════════════════════════════════════════════════ */

void rip_process(rip_state_t *s, void *ctx, uint8_t ch) {
    comp_context_t *c = (comp_context_t *)ctx;

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
         *   preproc_state 2 = inside <<…>> — collect directive name
         *
         * Directive evaluation is intentionally minimal: the DLL supports
         * only simple string comparisons of $VARIABLE$ values, so we do
         * the same.  Unknown directives are treated as false (suppress). */
        if (s->preproc_state == 0 && ch == '<') {
            s->preproc_state = 1;
            return; /* swallow first '<', wait for second */
        }
        if (s->preproc_state == 1) {
            if (ch == '<') {
                /* Got <<  — start collecting directive name */
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
            /* Collecting directive bytes until '>>' */
            if (ch == '>') {
                /* Null-terminate and evaluate */
                if (s->preproc_len < (int)sizeof(s->preproc_buf))
                    s->preproc_buf[s->preproc_len] = '\0';
                s->preproc_state = 0;
                const char *dir = s->preproc_buf;

                if (strncmp(dir, "IF ", 3) == 0 || strcmp(dir, "IF") == 0) {
                    /* <<IF expr>> — push nesting depth; evaluate expression.
                     * eval_if_expr() expands $VARIABLE$ references first (matching
                     * DLL ordering: ripTextVarEngine before condition check), then
                     * applies comparison operators (=, !=, >, <) or plain boolean. */
                    s->preproc_depth++;
                    const char *expr = (dir[2] == ' ') ? dir + 3 : "";
                    bool cond = eval_if_expr(s, expr);
                    if (!cond) s->preproc_suppress = true;
                } else if (strcmp(dir, "ELSE") == 0) {
                    /* <<ELSE>> — flip suppress state at current depth */
                    if (s->preproc_depth > 0)
                        s->preproc_suppress = !s->preproc_suppress;
                } else if (strcmp(dir, "ENDIF") == 0) {
                    /* <<ENDIF>> — pop nesting depth; restore normal output */
                    if (s->preproc_depth > 0) {
                        s->preproc_depth--;
                        if (s->preproc_depth == 0)
                            s->preproc_suppress = false;
                    }
                }
                /* Consume the trailing '>' of '>>' (eat one; second already consumed) */
            } else {
                /* Accumulate directive character */
                if (s->preproc_len < (int)sizeof(s->preproc_buf) - 1)
                    s->preproc_buf[s->preproc_len++] = ch;
            }
            return;
        }

        /* When suppressing, swallow all output bytes except '!' (which could
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
            s->last_char = ch;
            s->state     = RIP_ST_IDLE;
            break;
        }

        if (ch == '|') {
            /* '|' terminates current command; more may follow in frame.
             * Execute, reset, stay in CMD_LETTER for next command. */
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
            /* Stay in RIP_ST_COMMAND */
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
                       ch == '&' || ch == '`' || ch == '{' || ch == '"') {
                /* Valid Level 0 command letter */
                s->cmd_char = ch;
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
            s->last_char = ch;
            s->state     = RIP_ST_IDLE;
            break;
        }

        if (ch == '|') {
            /* Command terminator — dispatch, then accept next command letter */
            if (s->cmd_char)
                execute_rip_command(s, ctx);
            s->cmd_char  = 0;
            s->cmd_len   = 0;
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
            s->state = RIP_ST_COMMAND;
            break;
        }

        /* Accumulate parameter byte */
        if (s->cmd_len < (int)sizeof(s->cmd_buf) - 1)
            s->cmd_buf[s->cmd_len++] = ch;
        break;

    /* ── State 5: LINE_CONT ─────────────────────────────────────
     * '\' received mid-command.  Waiting for CR or LF.
     * DLL handler @ 0x1003A400.
     *
     * CR  → state = LINE_WAIT_LF(6)   [wait for optional CRLF pair]
     * LF  → restore prevState          [bare-LF continuation done]
     * other → restore prevState; emit '\' literal; re-process char
     * ─────────────────────────────────────────────────────────── */
    case RIP_ST_LINE_CONT:
        if (ch == '\r') {
            s->state = RIP_ST_LINE_WAIT_LF;
        } else if (ch == '\n') {
            s->state = s->prev_state;
        } else {
            /* Non-newline: '\' was literal; re-process ch in restored state */
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
            if (ch == '\r' || ch == '\n') {
                s->last_char = ch;
                s->state = RIP_ST_IDLE;
            } else {
                s->state = RIP_ST_COMMAND;
            }
        } else {
            if (s->cmd_len < (int)sizeof(s->cmd_buf) - 1)
                s->cmd_buf[s->cmd_len++] = ch;
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
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
            s->cmd_char = ch;
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
            s->cmd_char = ch;
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
            s->cmd_char = ch;
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
            s->is_level1 = false;
            s->is_level2 = false;
            s->is_level3 = false;
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
        memcpy(g_rip_state->host_date, g_rip_state->sync_date_buf, len);
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
        memcpy(g_rip_state->host_time, g_rip_state->sync_time_buf, len);
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
 * On NUL: stores response in the target $APPn$ variable, pushes it to the
 * BBS via TX FIFO, and clears query_pending. */
void rip_query_response_byte(uint8_t data_byte) {
    rip_state_t *s = g_rip_state;
    if (!s || !s->query_pending) return;

    if (data_byte == '\0') {
        /* Commit: identify target $APPn$ from query_var_name and store result */
        const char *vn = s->query_var_name;
        if (vn[0] == '$' && vn[1] == 'A' && vn[2] == 'P' && vn[3] == 'P' &&
            vn[4] >= '0' && vn[4] <= '9' && vn[5] == '$') {
            int idx = vn[4] - '0';
            int rlen = s->query_response_len;
            if (rlen > 31) rlen = 31;
            memcpy(s->app_vars[idx], s->query_response, rlen);
            s->app_vars[idx][rlen] = '\0';
            /* Send response to BBS now that we have it */
            if (rlen > 0)
                card_tx_push(s->app_vars[idx], rlen);
        }
        s->query_pending = false;
        s->query_response_len = 0;
    } else {
        if (s->query_response_len < (int)sizeof(s->query_response) - 1)
            s->query_response[s->query_response_len++] = (char)data_byte;
    }
}
