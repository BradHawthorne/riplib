/*
 * rip_icons.h — RIPscrip icon loader for A2GSPU card
 *
 * Two-tier icon lookup:
 *   1. Flash-embedded icons (rip_icons_data.c, built from RIPtel BMPs)
 *   2. PSRAM runtime cache (populated via BMP parser from file transfer)
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "riplib_platform.h"

/* Icon descriptor returned by lookup */
typedef struct {
    const uint8_t *pixels;   /* 8bpp top-down pixel data */
    uint16_t       width;
    uint16_t       height;
} rip_icon_t;

/* Bind the PSRAM arena used for runtime icon pixel allocations.
 * Must be called before rip_icon_cache_bmp() or rip_icon_cache_pixels()
 * will succeed. Also resets the runtime cache (flash icons are unaffected). */
void rip_icon_set_arena(psram_arena_t *arena);

/* Look up an icon by filename (uppercase, no extension).
 * Checks flash-embedded table first, then PSRAM cache.
 * Returns true + fills out icon descriptor, or false if not found. */
bool rip_icon_lookup(const char *name, int name_len, rip_icon_t *out);

/* Parse a BMP from memory and cache in PSRAM.
 * Supports 4bpp and 8bpp uncompressed BMPs.
 * name is the filename key for cache lookup.
 * Returns true on success. */
bool rip_icon_cache_bmp(const char *name, int name_len,
                        const uint8_t *bmp_data, int bmp_size);

/* Cache pre-parsed pixel data directly (from ICN parser or other sources).
 * pixels must be PSRAM-allocated (not freed by cache). */
bool rip_icon_cache_pixels(const char *name, int name_len,
                           uint8_t *pixels, uint16_t w, uint16_t h);

/* Runtime cache stats */
int rip_icon_cache_count(void);

/* ── File request queue (for icons not in flash or cache) ────────── *
 * When LOAD_ICON can't find an icon, it queues the filename here.
 * The host-side terminal app polls this queue and initiates file
 * transfer from the BBS. Once received, the host calls
 * rip_icon_cache_bmp() to populate the PSRAM cache, then re-issues
 * the LOAD_ICON command.
 *
 * This is a stub interface — the host polling and file transfer
 * protocol are implemented in the ProDOS terminal app (future). */

#define RIP_FILE_REQUEST_MAX  16
#define RIP_FILE_NAME_MAX     12

/* Queue a file request. Returns true if queued, false if full. */
bool rip_icon_request_file(const char *name, int name_len);

/* Check if there are pending file requests. */
int rip_icon_pending_requests(void);

/* Read and dequeue one pending request. Returns name length, 0 if empty. */
int rip_icon_dequeue_request(char *name_out, int max_len);

/* Codex FIX 4: Clear the entire pending request queue.
 * Called from rip_session_reset() on BBS disconnect so that icon
 * requests from a previous session are not replayed to the next BBS. */
void rip_icon_clear_requests(void);
