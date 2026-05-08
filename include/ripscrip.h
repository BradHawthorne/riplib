/*
 * ripscrip.h — RIPscrip 1.54 graphics protocol parser for A2GSPU card
 *
 * TeleGrafix RIPscrip (Remote Imaging Protocol Scripting Language).
 * Vector graphics + text overlay for BBS systems. 640×350 EGA coordinates
 * scaled to 640×400 framebuffer. Base-36 "MegaNum" parameter encoding.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License. See LICENSE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rip_icons.h"
#include "riplib_platform.h"

/* Maximum mouse regions (RIPscrip spec: 128) */
#define RIP_MAX_MOUSE_REGIONS 128

/* Maximum clipboard size (640×400 = 256000 bytes, stored in PSRAM) */
#define RIP_CLIPBOARD_MAX     (640 * 400)

/* Numbered icon slots used by v2.0 SAVE_ICON / STAMP_ICON. */
#define RIP_ICON_SLOT_MAX     36

/* Maximum text block lines */
#define RIP_MAX_TEXT_LINES    64

/* Maximum nested <<IF>> depth tracked by the stream preprocessor. */
#define RIP_PREPROC_MAX_DEPTH 8

/* Parser states — 14 states (DLL has 13; state 13 LEVEL3_LETTER added
 * by A2GSPU for the '3' prefix).  Maps to RIPSCRIP.DLL ripParseStateMachine
 * (DLL states 0-12 at 0x10039E90; jump table at 0x1003AB9C)
 *
 * State 0  IDLE         — scanning for '!'  (was PASSTHROUGH)
 * State 1  GOT_BANG     — got '!', looking for '|'
 * State 2  CMD_LETTER   — collecting command letter / level prefix
 * State 3  ARG_COLLECT  — accumulating MegaNum parameter bytes
 * State 4  ARG_DISPATCH — args complete, ready to call handler
 * State 5  LINE_CONT    — received '\' mid-command, waiting for CR/LF
 * State 6  LINE_WAIT_LF — got CR after '\', waiting for LF to complete
 * State 7  TEXT_COLLECT — free-text parameter until '|'  (commands like T/@)
 * State 8  SUPPRESS     — suppress ANSI fallback until CR/LF or '!'
 * State 9  COMMENT      — inside !|! comment, skip until next '|'
 * State 10 LEVEL1_LETTER — after '1' prefix, waiting for sub-command letter
 * State 11 LEVEL2_LETTER — after '2' prefix, waiting for sub-command letter
 * State 12 ERROR_RECOVERY — resync on '|' or newline after bad command
 */
#define RIP_ST_IDLE          0  /* Scanning for '!' (was PASSTHROUGH) */
#define RIP_ST_PASSTHROUGH   0  /* Alias for back-compat */
#define RIP_ST_GOT_BANG      1  /* Received '!' */
#define RIP_ST_COMMAND       2  /* Collecting command letter (CMD_LETTER) */
#define RIP_ST_ARG_COLLECT   3  /* Accumulating MegaNum parameter bytes */
#define RIP_ST_ARG_DISPATCH  4  /* Args complete — dispatch handler */
#define RIP_ST_LINE_CONT     5  /* '\' received — waiting for CR/LF */
#define RIP_ST_LINE_WAIT_LF  6  /* CR received after '\' — waiting for LF */
#define RIP_ST_TEXT_COLLECT  7  /* Free-text param until '|' */
#define RIP_ST_SUPPRESS      8  /* Suppress ANSI fallback until CR/LF */
#define RIP_ST_COMMENT       9  /* !|! comment — skip until '|' */
#define RIP_ST_LEVEL1_LETTER 10 /* After '1', waiting for sub-command */
#define RIP_ST_LEVEL2_LETTER 11 /* After '2', waiting for sub-command */
#define RIP_ST_ERROR_RECOVERY 12 /* Resync on '|' or newline */
#define RIP_ST_LEVEL3_LETTER 13 /* After '3', waiting for sub-command */

/* Mouse field flags — DLL field record +0x20 (rip_mouse.c line 166-170)
 * ripCmd_MouseRegion @ 0x1000A964, field byte at pFieldBuf+0x20 */
#define RIP_MF_ACTIVE      0x04  /* field is live / hit-testable */
#define RIP_MF_SEND_CHAR   0x08  /* send hotkey char on click, not host string */
#define RIP_MF_RADIO       0x20  /* radio-button group: deselect others on click */
#define RIP_MF_TOGGLE      0x40  /* toggle state on each click */
#define RIP_MF_HAS_LABEL   0x02  /* field has a visible label */

/* Mouse region entry — mirrors the DLL's 0xA3-byte mouse field record.
 * Key fields from rip_mouse.c:
 *   +0x20 flags     (MF_ACTIVE=0x04, MF_SEND_CHAR=0x08, MF_RADIO=0x20, MF_TOGGLE=0x40)
 *   +0x2B hotkey    (ASCII key, 0=none)
 *   +0x2C lineStyle
 *   +0x2D lineThick
 *   +0xA0 eventCode
 *   +0x3E iconPath  (97-byte icon filename)
 *   +0x32 pHostCmd  (pointer to host command string)
 */
typedef struct {
    int16_t x0, y0, x1, y1;       /* Bounding rectangle (card pixel coords) */
    uint8_t flags;                  /* MF_ACTIVE | MF_SEND_CHAR | MF_RADIO | MF_TOGGLE */
    uint8_t hotkey;                 /* ASCII hotkey (0=none) — DLL field +0x2B */
    uint8_t line_style;             /* Line style when drawn — DLL field +0x2C */
    uint8_t line_thick;             /* Line thickness — DLL field +0x2D */
    uint8_t highlight_color;        /* Highlight/hover color index */
    uint8_t event_code;             /* Event type code — DLL field +0xA0 low byte */
    char    text[128];              /* Host command string — DLL field pHostCmd */
    uint8_t text_len;
    char    icon_path[64];          /* Icon file path — DLL field +0x3E (97 bytes, truncated) */
    bool    active;                 /* true when MF_ACTIVE is set and region is registered */
    bool    hover;                  /* true when cursor is currently inside region */
} rip_mouse_region_t;

/* Button style (set by 1U per DLL ground truth, used by 1B button instances) */
typedef struct {
    int16_t  width, height;     /* Button dimensions */
    uint8_t  orient;            /* 0=horizontal, 1=vertical */
    uint16_t flags;             /* Style flags (beveled, chisel, etc.) */
    uint8_t  bev_size;          /* Bevel size in pixels */
    uint8_t  dfore, dback;      /* Dark foreground/background */
    uint8_t  bright, dark;      /* Highlight/shadow colors */
    uint8_t  surface;           /* Surface color */
    uint8_t  grp_no;            /* Group number (0-35) */
    uint16_t flags2;            /* Extended flags */
    uint8_t  uline_col;         /* Underline color */
    uint8_t  corner_col;        /* Corner color */
} rip_button_style_t;

/* Clipboard for GET_IMAGE/PUT_IMAGE */
typedef struct {
    uint8_t *data;              /* Pixel data (PSRAM-allocated) */
    int16_t  width, height;     /* Dimensions of stored region */
    bool     valid;             /* true if clipboard contains data */
} rip_clipboard_t;

/* Text block state (1T/1t/1E) */
typedef struct {
    int16_t  x0, y0, x1, y1;   /* Text region bounds */
    int16_t  cur_y;             /* Current Y position in block */
    bool     active;            /* Currently inside a text block */
} rip_text_block_t;

typedef struct {
    /* 256-color VGA palette */
    uint8_t vga_palette[256];   /* Palette index → RGB332 mapping */
    bool    palette_custom;     /* true if palette has been modified */

    /* GUI widget state */
    bool    window_active;
    int16_t win_x, win_y, win_w, win_h;

    /* Scalable text */
    uint8_t text_scale;     /* 1-8x scale factor */
    int16_t text_rotation;  /* degrees */

    /* Extended button table */
    uint16_t num_buttons;

    /* A2GSPU v3.1: Overflow pagination */
    uint8_t *overflow_buf;        /* PSRAM article buffer */
    uint32_t overflow_len;        /* Current article length */
    uint16_t overflow_page;       /* Current page number */
    uint16_t overflow_total;      /* Total pages */

    /* A2GSPU v3.1: Engine capabilities */
    uint8_t  caps_mask;           /* Capability bitmask */

    /* A2GSPU v3.1: Per-port extended attributes (36 slots, one per port index).
     * Added to match ripscrip2.c init/dispatch that references these arrays. */
    uint8_t  port_alpha[36];      /* 0=transparent, 35=fully opaque */
    uint8_t  port_comp_mode[36];  /* Compositing mode per port (0=COPY) */
    uint8_t  port_zorder[36];     /* Z-order per port */
} ripscrip2_state_t;

/* ── Drawing Ports (v2.0 / v3.0) ─────────────────────────────────── *
 *
 * The DLL maintains 36 independent GDI DCs (one per port slot) backed
 * by compatible bitmaps.  On RP2350 there is a single 640×400
 * framebuffer, so we cannot have 36 independent pixel surfaces.
 * Instead each port stores its drawing state (clip region, color,
 * line style, etc.) and we save/restore that state on port switch.
 *
 * All drawing always targets the single shared framebuffer; the active
 * port's viewport is applied as the hardware clip rectangle.
 *
 * Port 0 is permanent: full-screen viewport, cannot be deleted,
 * allocated at rip_init_first() time.
 *
 * PORT_FLAG_* bits stored in rip_port_t.flags mirror the DLL's
 * PORT_ENTRY.flags byte (+0x17):
 *   bit 0 = RIP_PORT_FLAG_PROTECTED   — cannot be deleted or redefined
 *   bit 1 = RIP_PORT_FLAG_FULLSCREEN  — viewport covers entire screen
 */
#define RIP_MAX_PORTS              36
#define RIP_PORT_FLAG_PROTECTED    0x01
#define RIP_PORT_FLAG_FULLSCREEN   0x02

typedef struct {
    bool     allocated;          /* Port slot is in use */
    uint8_t  flags;              /* RIP_PORT_FLAG_* bitmask */

    /* Viewport — card pixel coordinates (Y already scaled from EGA 350→400) */
    int16_t  vp_x0, vp_y0, vp_x1, vp_y1;

    /* Coordinate origin offset (v2.0 world-space translation; 0,0 normally) */
    int16_t  origin_x, origin_y;

    /* Saved drawing state — written on switch-away, loaded on switch-in */
    int16_t  draw_x, draw_y;     /* Per-port current drawing position */
    uint8_t  draw_color;
    uint8_t  fill_color;
    uint8_t  fill_pattern;
    uint8_t  back_color;
    uint8_t  write_mode;
    uint8_t  line_style;
    uint8_t  line_thick;
    uint8_t  font_id;
    uint8_t  font_size;
    uint8_t  font_dir;
    uint8_t  font_hjust;   /* 0=left, 1=center, 2=right (v3.0 extension) */
    uint8_t  font_vjust;   /* 0=bottom, 1=center, 2=top, 3=baseline */
    uint8_t  font_attrib;  /* bit0=bold, bit1=italic, bit2=underline, bit3=shadow */
    uint8_t  font_ext_id;
    uint8_t  font_ext_attr;
    uint32_t font_ext_size;

    /* A2GSPU v3.1 extended attributes (set by !|2F port-flags command) */
    uint8_t  alpha;              /* 0=transparent, 35=fully opaque */
    uint8_t  comp_mode;          /* Compositing mode (0=COPY) */
    uint8_t  zorder;             /* Z-order for compositor layering */
} rip_port_t;

/* RIPscrip parser state */
typedef struct {
    uint8_t  state;          /* Current FSM state (RIP_ST_* constants, 0-12) */
    uint8_t  prev_state;     /* Saved state for line-continuation restore (DLL pContext+0x04) */
    char     cmd_buf[256];   /* Command parameter accumulator */
    uint8_t  cmd_len;
    char     cmd_char;       /* Current command letter */
    bool     is_level1;      /* Currently parsing a Level 1 command */
    bool     is_level2;      /* Currently parsing a Level 2 command */
    bool     is_level3;      /* Currently parsing a Level 3 command */
    bool     line_cont;      /* Previous byte was '\' — line continuation pending */

    /* Drawing state */
    int16_t  draw_x, draw_y; /* Current drawing position */
    uint8_t  draw_color;     /* Current drawing color (0-15) */
    uint8_t  back_color;     /* Fix Q2: background color index (DLL GFXSTYLE+0x02) */
    uint8_t  write_mode;     /* 0=COPY, 1=OR, 2=AND, 3=XOR, 4=NOT */
    uint8_t  line_style;     /* 0=solid, 1=dotted, 2=center, 3=dashed */
    uint8_t  line_thick;     /* 1 or 3 */
    uint8_t  fill_pattern;   /* 0-11 predefined, 12=custom */
    uint8_t  fill_color;     /* Fill color index */
    uint8_t  font_id;        /* 0-10 */
    uint8_t  font_dir;       /* 0=horizontal, 1=vertical */
    uint8_t  font_size;      /* 1-10 */
    uint8_t  font_hjust;     /* 0=left, 1=center, 2=right (v3.0 ext) */
    uint8_t  font_vjust;     /* 0=bottom, 1=center, 2=top, 3=baseline */
    uint8_t  font_attrib;    /* RIP_FONT_ATTRIB bits: bit0=bold, bit1=italic,
                              * bit2=underline, bit3=shadow — DLL cmd 'f' */
    uint8_t  font_ext_id;    /* RIP_EXT_FONT_STYLE: 2-digit font selector */
    uint8_t  font_ext_attr;  /* RIP_EXT_FONT_STYLE: 1-digit attribute */
    uint32_t font_ext_size;  /* RIP_EXT_FONT_STYLE: 4-digit point size */

    /* Extended text window state (RIP_EXT_TEXT_WINDOW 'b') */
    uint8_t  etw_font_id;    /* Font ID for extended text window */
    uint8_t  etw_fore_col;   /* Foreground color index */
    uint8_t  etw_back_col;   /* Background color index */

    /* Text window */
    int16_t  tw_x0, tw_y0, tw_x1, tw_y1;
    uint8_t  tw_wrap;
    uint8_t  tw_font_size;
    int16_t  tw_cur_x, tw_cur_y;   /* Pixel cursor within text window */
    bool     tw_active;             /* true when non-default text window set */

    /* Viewport (clip region) */
    int16_t  vp_x0, vp_y0, vp_x1, vp_y1;

    /* Mouse regions */
    rip_mouse_region_t mouse_regions[RIP_MAX_MOUSE_REGIONS];
    uint16_t num_mouse_regions;  /* Fix S5/FB-4: uint8_t wraps at 256, bypassing the 128 limit check */

    /* 16-color EGA palette → card palette index mapping */
    uint8_t  palette[16];

    /* Saved hardware palette RGB565 values for indices 240-255.
     * Populated by rip_save_palette() before protocol deactivation so that
     * rip_apply_palette() can restore BBS-customized colors (not just EGA
     * defaults) when switching back from VT100 or another protocol. */
    uint16_t saved_palette_rgb565[16];

    /* Level 1 state */
    rip_button_style_t button_style;   /* Current button style (set by 1B) */
    rip_clipboard_t    clipboard;      /* Image clipboard (GET/PUT_IMAGE) */
    rip_icon_t         icon_slots[RIP_ICON_SLOT_MAX];  /* SAVE_ICON slot table */
    bool               icon_slot_valid[RIP_ICON_SLOT_MAX];
    rip_text_block_t   text_block;     /* Active text block (1T/1t/1E) */
    rip_icon_state_t   icon_state;     /* Session-scoped runtime icon cache + request queue */
    ripscrip2_state_t  rip2_state;     /* Per-session RIPscrip 2.0 / v3.x state */

    /* Fix Q1/A7: last_char tracks previous byte for '!' line-boundary check */
    uint8_t  last_char;                /* previous byte received in PASSTHROUGH state */

    /* ESC[! auto-detect tracking (Synchronet sends this to probe for RIP) */
    uint8_t  esc_detect;               /* 0=idle, 1=got ESC, 2=got ESC[ */
    bool     utf8_pipe_pending;        /* true after 0xC2 in GOT_BANG, waiting for 0xA6 */
    bool     rip_has_drawn;            /* true after RIP cmds draw; cleared by '*' reset */
    bool     cursor_repositioned;     /* true after VT100 cursor moved to bottom for status bar */

    /* E4: <<IF>>/<<ELSE>>/<<ENDIF>> pre-processor state (IDLE/PASSTHROUGH).
     * Directives are detected before normal byte routing.  When preproc_suppress
     * is true all output bytes (and command bytes) are swallowed except for
     * additional << directive starts needed to maintain nesting depth. */
    uint8_t  preproc_state;    /* 0=normal, 1=got first '<', 2=collecting, 3=got first '>' */
    char     preproc_buf[32];  /* Directive name accumulator */
    uint8_t  preproc_len;      /* Bytes written into preproc_buf */
    bool     preproc_suppress; /* true = inside false <<IF>> branch — suppress output */
    uint8_t  preproc_depth;    /* Active stack depth, capped at RIP_PREPROC_MAX_DEPTH */
    uint8_t  preproc_overflow; /* Nested levels beyond RIP_PREPROC_MAX_DEPTH */
    bool     preproc_parent_suppress[RIP_PREPROC_MAX_DEPTH];
    bool     preproc_branch_active[RIP_PREPROC_MAX_DEPTH];
    bool     preproc_branch_taken[RIP_PREPROC_MAX_DEPTH];

    /* A2GSPU v3.1: Application variables */
    char app_vars[10][32];   /* $APP0$ through $APP9$ */

    /* DLL ground truth: GFXSTYLE resolution_mode field.
     * 0=EGA(640x350), 1=VGA(640x480), 2=SVGA, 3=XGA, 4=HIGH.
     * A2GSPU has a fixed 640x400 framebuffer so always renders as EGA (0),
     * but the field is stored so the 'n' command can set it and $PROT$
     * can report the negotiated mode back to the BBS. */
    uint8_t resolution_mode;

    /* FIX V1: Host-supplied date/time (CB_GET_TIME callback equivalent).
     * Populated by CMD_SYNC_DATE / CMD_SYNC_TIME from the IIgs bridge loop,
     * which reads the ProDOS MLI GET_TIME result at connect time.
     * Used by $DATE$ and $TIME$ text variable expansion instead of calling
     * time()/localtime() — the RP2350 RTC is not authoritative for BBS time.
     * Falls back to the RP2350 RTC when strings are empty (host not synced). */
    char host_date[12];   /* "MM/DD/YY" from IIgs ProDOS clock (NUL-terminated) */
    char host_time[12];   /* "HH:MM:SS" from IIgs ProDOS clock (NUL-terminated) */

    /* FIX M3: In-progress SYNC_DATE / SYNC_TIME accumulator.
     * Bytes arrive one at a time via CMD_SYNC_DATE / CMD_SYNC_TIME; the
     * accumulator is committed to host_date / host_time on the NUL byte. */
    char sync_date_buf[12];   /* staging buffer — written to host_date on NUL */
    uint8_t sync_date_len;
    char sync_time_buf[12];   /* staging buffer — written to host_time on NUL */
    uint8_t sync_time_len;

    /* FIX M4: $QUERY$ card→IIgs→card round-trip state (CB_INPUT_TEXT equiv).
     * When the card encounters a RIP_QUERY command that requires user text
     * input it sets query_pending, pushes a CMD_QUERY_PROMPT stream via the
     * TX FIFO, and pauses variable expansion.  The IIgs sends back the typed
     * response via CMD_QUERY_RESPONSE bytes; on the NUL terminator the card
     * stores the result in the target $APPn$ variable and clears query_pending.
     * query_response is the staging accumulator for incoming response bytes. */
    bool    query_pending;         /* true while waiting for IIgs response */
    char    query_var_name[32];    /* $APPn$ variable name being queried */
    char    query_response[32];    /* incoming response accumulator */
    uint8_t query_response_len;

    /* Per-session PSRAM arena — 1 MB reserved once at boot by
     * rip_init_first(), reset on BBS disconnect by rip_session_reset().
     * Never reset on a mid-session protocol switch (rip_activate). */
    psram_arena_t psram_arena;

    /* Per-session file-upload staging for dynamic BMP/ICN cache fills. */
    uint8_t *upload_buf;
    int      upload_pos;
    char     upload_name[16];
    int      upload_name_len;
    uint8_t  upload_name_remaining;
    bool     upload_name_overflow;
    bool     upload_reading_name;

    /* FIX TX1: $RAND$ LCG seed — seeded from frame_count in rip_init_first().
     * Uses the same Knuth/POSIX LCG as MSVC rand(): multiplier 1103515245,
     * addend 12345.  ripTextVarEngine @ 0x026218 calls rand() for $RAND$. */
    uint32_t rand_state;

    /* FIX TX2: $NOREFRESH$ / $REFRESH$ suppress-refresh flag.
     * When true, the BBS has requested that the card suppress automatic
     * screen refresh while compositing a complex scene.  $REFRESH$ clears it
     * and marks all rows dirty (equivalent to CB_REFRESH in the DLL). */
    bool refresh_suppress;

    /* FIX L1-4: 1N RIP_SET_ICON_DIR — icon search path override.
     * DLL stores this in the instance IconPath field (rip_instance.c).
     * A2GSPU uses a flat flash archive (no real FS) so the path is stored
     * but only consulted as a tag for future dynamic icon requests. */
    char icon_dir[64];

    /* FIX L1-2: 1S RIP_IMAGE_STYLE — image display mode (0=stretch, 1=tile,
     * 2=center).  Stored so subsequent icon-load / PUT_IMAGE calls can honour
     * the BBS-requested presentation.  DLL GFXSTYLE imageStyle field. */
    uint8_t image_style;

    /* RIP_ICON_STYLE ('&') parameters for subsequent icon rendering.
     * Coordinates are stored as card pixels.  style follows 1S where possible:
     * 0=stretch-to-box, 1=tile, 2=center, 3=proportional fit. */
    bool     icon_style_active;
    int16_t  icon_style_x0, icon_style_y0, icon_style_x1, icon_style_y1;
    uint8_t  icon_style_style;
    uint8_t  icon_style_align;
    uint8_t  icon_style_scale;

    /* FIX L1-7: 1V extended viewport scale factor (0=none, 1-35 in MegaNum).
     * Stored alongside the viewport rect; ignored on this fixed-res card but
     * needed so $PROT$ can report back the correct capability flags. */
    uint8_t viewport_scale;

    /* ── Drawing Ports (v2.0 / v3.0) ─────────────────────────────── *
     * Port 0 is always allocated (full-screen, permanent).
     * Ports 1-35 are created on demand by !|2P and destroyed by !|2p.
     * active_port tracks the current drawing port (0-35).
     * On switch: active port's drawing fields are snapshotted into
     * ports[active_port], then new port's fields are loaded back into
     * the rip_state_t drawing fields and its viewport clip is applied. */
    rip_port_t ports[RIP_MAX_PORTS];
    uint8_t    active_port;           /* Index of current active port (0-35) */
} rip_state_t;

/* Map a BGI fill style (0=EMPTY .. 12=USER) to the card-native fill
 * pattern index used by drawing.c::fill_span().  Returns -1 for EMPTY
 * (caller must skip the fill entirely). */
int8_t rip_bgi_fill_to_card(uint8_t bgi_style);

/* Codex FIX 1: Boot-time init — call ONCE at power-on.
 * Does memset, arena reservation, BGI font parse, and drawing defaults.
 * rip_init() is kept as a backward-compat alias for rip_init_first().
 *
 * CONTRACT: caller MUST zero `*s` (e.g. via memset, calloc, or static
 * storage) before the FIRST call.  The implementation snapshots the
 * existing psram_arena before memset so re-initialization does not
 * leak the allocated block; that snapshot reads from `*s` and would
 * be UB on uninitialized memory. */
void rip_init_first(rip_state_t *s);

/* Codex FIX 1: Protocol-switch activation — called every time the
 * compositor switches to RIPscrip.  Restores the EGA palette and marks
 * the framebuffer dirty.  Does NOT memset or touch session state. */
void rip_activate(rip_state_t *s);

/* Codex FIX 3: Session disconnect reset — call on BBS disconnect.
 * Resets the PSRAM arena, clears mouse regions, text variables,
 * query state, icon request queue, and upload staging.
 * Does NOT re-init drawing defaults (rip_init_first already did that). */
void rip_session_reset(rip_state_t *s);

/* Backward-compat wrapper: calls rip_init_first(). */
void rip_init(rip_state_t *s);

void rip_process(rip_state_t *s, void *ctx, uint8_t ch);

/* Stateful host-event helpers for multi-session integrations. */
void rip_mouse_event_state(rip_state_t *s, int16_t x, int16_t y, bool clicked);
void rip_file_upload_begin_state(rip_state_t *s, uint8_t name_len);
void rip_file_upload_byte_state(rip_state_t *s, uint8_t data_byte);
void rip_file_upload_end_state(rip_state_t *s);

/* Backward-compatible active-session wrappers. */
void rip_mouse_event_ext(int16_t x, int16_t y, bool clicked);
void rip_file_upload_begin(uint8_t name_len);
void rip_file_upload_byte(uint8_t data_byte);
void rip_file_upload_end(void);

/* FIX V1/M4: Host callback shims — called from main.c Mosaic CMD handlers.
 * These accumulate one byte per call and commit on the NUL terminator.
 * main.c uses extern declarations; rip_state_t stays opaque outside ripscrip.c. */
void rip_sync_date_byte(uint8_t data_byte);      /* CMD_SYNC_DATE — CB_GET_TIME date half */
void rip_sync_time_byte(uint8_t data_byte);      /* CMD_SYNC_TIME — CB_GET_TIME time half */
void rip_query_response_byte(uint8_t data_byte); /* CMD_QUERY_RESPONSE — CB_INPUT_TEXT */

/* Palette coexistence helpers — called by the compositor on protocol switch.
 * rip_save_palette() snapshots hardware indices 240-255 into s->saved_palette_rgb565
 * so that BBS-customized colors (set via RIP_SET_PALETTE / RIP_ONE_PALETTE) survive
 * a temporary switch to VT100 or another protocol.
 * rip_apply_palette() writes saved_palette_rgb565 back to hardware; falls back to
 * EGA defaults if no snapshot has been taken yet (saved_palette_rgb565 all zero).
 *
 * NOTE: API asymmetry — save takes an explicit `s`, apply uses the active
 * (last-initialized) session via the internal g_rip_state pointer.  Single-
 * session in practice; if multi-session ever lands, change apply to take `s`. */
void rip_save_palette(rip_state_t *s);
void rip_apply_palette(void);
