/*
 * platform_stubs.c — Minimal platform implementation for RIPlib demo
 *
 * Replace these with your platform's palette, allocator, and I/O.
 */

#include "riplib_platform.h"
#include <stdio.h>

/* Simple 256-entry RGB565 palette */
static uint16_t g_palette[256];

void palette_write_rgb565(uint8_t index, uint16_t rgb565) {
    g_palette[index] = rgb565;
}

uint16_t palette_read_rgb565(uint8_t index) {
    return g_palette[index];
}

void card_tx_push(const char *buf, int len) {
    /* Send to stdout for demo purposes */
    fwrite(buf, 1, len, stdout);
}
