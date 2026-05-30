/*
 * riplib_platform.h — Platform abstraction for RIPlib
 *
 * Defines the small set of platform-dependent surfaces RIPlib needs:
 * a bump-arena allocator (backed by malloc() by default), a 3-function
 * extern interface a host must implement (palette R/W and BBS TX), and
 * stub types/functions that let the protocol parser call into a host
 * compositor without requiring one.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License.  See LICENSE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── PSRAM arena ────────────────────────────────────────────────── */
/* A small bump allocator used for session-lived dynamic storage
 * (icon pixel caches, clipboard captures, file-upload staging).
 *
 * The name "psram" reflects the original use case (an embedded
 * target backing the arena with external PSRAM); the default
 * implementation here is simply malloc().  Consumers with a real
 * PSRAM region — or any other fixed memory pool — are expected to
 * provide their own backing inside platform code that bypasses or
 * replaces these static-inline stubs. */
typedef struct {
    uint8_t *base;
    uint32_t size;
    uint32_t used;
} psram_arena_t;

static inline void psram_arena_init(psram_arena_t *a, uint32_t size) {
    a->base = (uint8_t *)malloc(size);
    a->size = a->base ? size : 0;
    a->used = 0;
}

static inline void *psram_arena_alloc(psram_arena_t *a, uint32_t size) {
    size = (size + 31u) & ~(uint32_t)31u;  /* 32-byte align */
    if (!a->base || a->used + size > a->size) return NULL;
    void *p = a->base + a->used;
    a->used += size;
    return p;
}

static inline void psram_arena_reset(psram_arena_t *a) {
    a->used = 0;
}

/* Free the backing block.  On embedded the arena lives forever, so
 * this is typically only used by tests/desktop tools to keep ASan and
 * other leak detectors happy.  Safe to call multiple times. */
static inline void psram_arena_destroy(psram_arena_t *a) {
    if (a->base) free(a->base);
    a->base = NULL;
    a->size = 0;
    a->used = 0;
}

/* ── Platform extern functions ──────────────────────────────────── */
/* Implement these for your platform: */

/**
 * Write an RGB565 color value to the palette at the given index.
 * On indexed-color displays, this sets the hardware palette.
 * On truecolor displays, maintain a lookup table.
 */
extern void palette_write_rgb565(uint8_t index, uint16_t rgb565);

/**
 * Read an RGB565 color value from the palette.
 */
extern uint16_t palette_read_rgb565(uint8_t index);

/**
 * Send response bytes back to the BBS (via serial/TCP).
 * Used for query responses, file transfer, mouse events.
 */
extern void card_tx_push(const char *buf, int len);

/* gpu_psram_alloc was previously declared here but the library uses
 * psram_arena_alloc() exclusively.  Removed to avoid ghost-symbol
 * confusion; the test/demo platform stubs no longer need to provide it. */

/* ── Compositor stub ────────────────────────────────────────────── */
/* Minimal stub — the full compositor is not part of RIPlib.
 * These are called by ripscrip.c for protocol passthrough. */

/* Compositor context — minimal for standalone use */
typedef struct {
    void *ctx;
    uint8_t *target;  /* framebuffer pointer (used by ripscrip2.c) */
} comp_context_t;

/* Compositor stubs — no-op in standalone mode */
static inline void comp_passthrough_vt100(comp_context_t *c, uint8_t byte) {
    (void)c; (void)byte;
}
static inline void comp_set_cursor(comp_context_t *c, int16_t x, int16_t y) {
    (void)c; (void)x; (void)y;
}
static inline void comp_clear_screen(comp_context_t *c, uint8_t mode) {
    (void)c; (void)mode;
}
static inline void comp_clear_line(comp_context_t *c, uint8_t mode) {
    (void)c; (void)mode;
}

/* (Historical note: this header previously declared a `term_cell_t`
 * type and TERM_MAX_COLS/ROWS macros for the benefit of an external
 * terminal-grid renderer.  No code inside RIPlib referenced them; per
 * audit candidate C-001 + empirical question U-008 they were dropped
 * to clean up the public surface.  Consumers that need a terminal
 * cell type should declare their own.) */

#ifdef __cplusplus
}
#endif
