/*
 * riplib_platform.h — Platform abstraction for RIPlib
 *
 * Provides the minimal types and stubs that replace A2GSPU card-specific
 * dependencies (PSRAM arena, compositor, etc.). Consumers implement
 * the 3 extern functions below for their platform.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License. See LICENSE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── PSRAM arena stub ───────────────────────────────────────────── */
/* On embedded (RP2350), this is a bump allocator in 8MB PSRAM.
 * On desktop, just use malloc(). */
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

/* ── Terminal stub ──────────────────────────────────────────────── */
/* Minimal terminal types for protocol parser compatibility */
#ifndef TERM_MAX_COLS
#define TERM_MAX_COLS 80
#define TERM_MAX_ROWS 25
typedef struct { uint8_t ch; uint8_t attr; } term_cell_t;
#endif

#ifdef __cplusplus
}
#endif
