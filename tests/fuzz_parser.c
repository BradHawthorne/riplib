/*
 * fuzz_parser.c — libFuzzer entry point for the RIPscrip parser.
 *
 * This target is OPTIONAL.  Build with:
 *
 *   cmake -B build-fuzz -DRIPLIB_BUILD_FUZZ=ON \
 *         -DCMAKE_C_COMPILER=clang \
 *         -DCMAKE_C_FLAGS="-fsanitize=fuzzer,address,undefined -g -O1"
 *   cmake --build build-fuzz
 *   ./build-fuzz/tests/fuzz_parser -max_total_time=60
 *
 * The harness feeds the input as a byte stream to rip_process() one byte
 * at a time and re-initializes between corpus entries.  No assertions are
 * baked in here — the parser's own internal invariants plus ASan/UBSan
 * are the bug detectors.  Per audit C-011.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License.  See LICENSE.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "ripscrip.h"
#include "drawing.h"
#include "riplib_platform.h"

/* ── Silent platform stubs ──────────────────────────────────────── */
/* Unlike examples/platform_stubs.c which writes card_tx_push bytes to
 * stdout, the fuzz harness discards everything so the corpus runner's
 * console stays readable. */

static uint16_t g_palette[256];

void palette_write_rgb565(uint8_t index, uint16_t rgb565) {
    g_palette[index] = rgb565;
}

uint16_t palette_read_rgb565(uint8_t index) {
    return g_palette[index];
}

void card_tx_push(const char *buf, int len) {
    (void)buf;
    (void)len;
}

/* ── Fuzz harness ──────────────────────────────────────────────── */

/* 320x200 framebuffer is the smallest legal RIPscrip surface; large
 * enough to exercise drawing-bounds logic without burning RAM on every
 * corpus entry.  The framebuffer is intentionally heap-allocated once
 * and reused: the harness's job is to exercise the parser, not the
 * platform allocator. */
#define FB_W 320
#define FB_H 200
static uint8_t fb[FB_W * FB_H];
static int initialized = 0;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    rip_state_t s;
    comp_context_t ctx;

    if (!initialized) {
        draw_init(fb, FB_W, FB_W, FB_H);
        initialized = 1;
    }
    memset(&s, 0, sizeof(s));
    memset(&ctx, 0, sizeof(ctx));
    ctx.target = fb;
    rip_init_first(&s);

    for (size_t i = 0; i < size; i++) {
        rip_process(&s, &ctx, data[i]);
    }

    /* Make sure session teardown doesn't blow up on whatever state the
     * fuzzer drove us into. */
    rip_session_reset(&s);
    psram_arena_destroy(&s.psram_arena);
    return 0;
}
