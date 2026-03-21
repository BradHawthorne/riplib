/*
 * ripscrip2.h — RIPscrip 2.0 protocol extensions for A2GSPU card
 *
 * TeleGrafix Communications, 1994-1995. Extends RIPscrip 1.54 with:
 *   - 256-color VGA palette (up from 16-color EGA)
 *   - Enhanced GUI widgets (windows, scrollbars, menus, dialogs)
 *   - Scalable text with rotation
 *   - Improved button/mouse region system
 *   - Clipboard operations
 *
 * RIPscrip 2.0 was never widely deployed. TeleGrafix went defunct
 * circa 1996. No complete implementation exists in the wild besides
 * the original never-released RIPterm 2.0 client.
 *
 * This parser extends ripscrip.c (1.54) with 2.0-specific commands.
 * The base !| frame detection and MegaNum decoding are shared.
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ripscrip.h"   /* rip_state_t, rip_port_t, RIP_MAX_PORTS */

/* RIPscrip 2.0 command prefixes (Level 2) */
#define RIP2_CMD_SET_PALETTE    '0'  /* Set VGA palette entry */
#define RIP2_CMD_QUERY_PALETTE  '1'  /* Query palette */
#define RIP2_CMD_SET_WINDOW     '2'  /* Define window region */
#define RIP2_CMD_SCROLLBAR      '3'  /* Define scrollbar widget */
#define RIP2_CMD_MENU           '4'  /* Define menu bar */
#define RIP2_CMD_DIALOG         '5'  /* Define dialog box */
#define RIP2_CMD_SCALE_TEXT     '6'  /* Scalable text with size/rotation */
#define RIP2_CMD_CLIPBOARD      '7'  /* Clipboard copy/paste */
#define RIP2_CMD_GRADIENT       '8'  /* Gradient fill */
#define RIP2_CMD_ALPHA_BLEND    '9'  /* Alpha transparency */

/* Drawing Port commands (v2.0/v3.0) */
#define RIP2_CMD_PORT_DEFINE    'P'  /* !|2P — define/create a port */
#define RIP2_CMD_PORT_DELETE    'p'  /* !|2p — delete a port */
#define RIP2_CMD_PORT_COPY      'C'  /* !|2C — copy pixels between ports */
#define RIP2_CMD_PORT_SWITCH    's'  /* !|2s — switch active port */

/* A2GSPU v3.1 Extensions — dead-code activation from DLL binary analysis */
#define RIP2_CMD_CHORD          'c'  /* Chord drawing (DLL RVA 0x012663) */
#define RIP2_CMD_SET_REFRESH    'R'  /* Host-triggered screen refresh */
#define RIP2_CMD_PORT_FLAGS     'F'  /* Extended port attributes (alpha/mode/zorder) */

/* Extended mouse region with callback */
#define RIP2_MAX_BUTTONS  256

typedef struct {
    /* RIPscrip 2.0 extends 1.54 state. We overlay on top of the
     * existing rip_state_t from ripscrip.h. */

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

void ripscrip2_init(ripscrip2_state_t *s);

/* Process a Level 2 command (called from ripscrip.c when '2' prefix detected).
 * cmd     — command character after the level prefix
 * raw     — raw parameter bytes from cmd_buf (before MegaNum decode)
 * raw_len — number of bytes in raw
 * params  — MegaNum-decoded parameters (mega2 pairs, for backward compat)
 * param_count — number of decoded params
 *
 * Port commands (P, p, s, C, F) parse raw directly for 1-digit and 4-digit
 * MegaNum fields.  All other commands continue to use the params array. */
void ripscrip2_execute(ripscrip2_state_t *s, rip_state_t *rs, void *ctx,
                       char cmd,
                       const char *raw, int raw_len,
                       const int16_t *params, int param_count);
