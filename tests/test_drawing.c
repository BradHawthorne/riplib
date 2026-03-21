/*
 * test_drawing.c — Basic drawing primitive tests for RIPlib
 *
 * Verifies pixel-level correctness of core drawing operations.
 * Returns 0 on success, non-zero on failure.
 */

#include "drawing.h"
#include "bgi_font.h"
#include "font_bgi_trip.h"
#include "font_cp437_8x8.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define W 320
#define H 200
static uint8_t fb[W * H];

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-40s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ── Platform stubs ─────────────────────────────────────────────── */
static uint16_t palette[256];
void palette_write_rgb565(uint8_t i, uint16_t v) { palette[i] = v; }
uint16_t palette_read_rgb565(uint8_t i) { return palette[i]; }
void *gpu_psram_alloc(uint32_t size) { return malloc(size); }
void card_tx_push(const char *buf, int len) { (void)buf; (void)len; }

/* ── Helper ─────────────────────────────────────────────────────── */
static void clear(void) {
    memset(fb, 0, sizeof(fb));
    draw_init(fb, W, W, H);
}

/* ══════════════════════════════════════════════════════════════════
 * TESTS
 * ══════════════════════════════════════════════════════════════════ */

static void test_pixel(void) {
    TEST("draw_pixel + get_pixel roundtrip");
    clear();
    draw_set_color(0xAB);
    draw_pixel(100, 50);
    uint8_t v = draw_get_pixel(100, 50);
    if (v == 0xAB) PASS(); else FAIL("pixel value mismatch");
}

static void test_hline(void) {
    TEST("draw_hline writes correct span");
    clear();
    draw_set_color(0xFF);
    draw_hline(10, 20, 50);
    int ok = 1;
    for (int x = 10; x < 60; x++)
        if (draw_get_pixel(x, 20) != 0xFF) ok = 0;
    if (draw_get_pixel(9, 20) != 0x00) ok = 0;   /* before */
    if (draw_get_pixel(60, 20) != 0x00) ok = 0;  /* after */
    if (ok) PASS(); else FAIL("span incorrect");
}

static void test_rect_fill(void) {
    TEST("draw_rect filled covers area");
    clear();
    draw_set_color(0x42);
    draw_rect(10, 10, 20, 15, true);
    int ok = 1;
    /* Inside */
    if (draw_get_pixel(15, 15) != 0x42) ok = 0;
    if (draw_get_pixel(10, 10) != 0x42) ok = 0;
    if (draw_get_pixel(29, 24) != 0x42) ok = 0;
    /* Outside */
    if (draw_get_pixel(9, 10) != 0x00) ok = 0;
    if (draw_get_pixel(30, 10) != 0x00) ok = 0;
    if (ok) PASS(); else FAIL("fill area incorrect");
}

static void test_rect_outline(void) {
    TEST("draw_rect outline has hollow center");
    clear();
    draw_set_color(0xFF);
    draw_rect(10, 10, 30, 20, false);
    int ok = 1;
    /* Edges drawn */
    if (draw_get_pixel(10, 10) != 0xFF) ok = 0;
    if (draw_get_pixel(25, 10) != 0xFF) ok = 0;
    /* Center hollow */
    if (draw_get_pixel(20, 18) != 0x00) ok = 0;
    if (ok) PASS(); else FAIL("outline incorrect");
}

static void test_circle(void) {
    TEST("draw_circle outline at cardinal points");
    clear();
    draw_set_color(0xFF);
    draw_circle(100, 100, 20, false);
    int ok = 1;
    /* Cardinal points should have pixels */
    if (draw_get_pixel(120, 100) == 0x00) ok = 0;  /* right */
    if (draw_get_pixel(80, 100) == 0x00) ok = 0;   /* left */
    if (draw_get_pixel(100, 80) == 0x00) ok = 0;   /* top */
    if (draw_get_pixel(100, 120) == 0x00) ok = 0;  /* bottom */
    /* Center should be empty */
    if (draw_get_pixel(100, 100) != 0x00) ok = 0;
    if (ok) PASS(); else FAIL("circle points incorrect");
}

static void test_write_mode_xor(void) {
    TEST("XOR double-draw restores original");
    clear();
    draw_set_color(0x55);
    draw_rect(10, 10, 20, 20, true);
    uint8_t before = draw_get_pixel(15, 15);
    draw_set_write_mode(DRAW_MODE_XOR);
    draw_set_color(0xFF);
    draw_rect(10, 10, 20, 20, true);  /* XOR on */
    draw_rect(10, 10, 20, 20, true);  /* XOR off */
    draw_set_write_mode(DRAW_MODE_COPY);
    uint8_t after = draw_get_pixel(15, 15);
    if (before == after) PASS(); else FAIL("XOR not self-inverse");
}

static void test_write_mode_not(void) {
    TEST("NOT inverts pixel value");
    clear();
    draw_set_color(0xFF);
    draw_pixel(50, 50);
    draw_set_write_mode(DRAW_MODE_NOT);
    draw_set_color(0);
    draw_rect(50, 50, 1, 1, true);
    draw_set_write_mode(DRAW_MODE_COPY);
    uint8_t v = draw_get_pixel(50, 50);
    if (v == 0x00) PASS(); else FAIL("NOT didn't invert");
}

static void test_clip(void) {
    TEST("clip region prevents out-of-bounds draw");
    clear();
    draw_set_clip(10, 10, 20, 20);
    draw_set_color(0xFF);
    draw_rect(0, 0, W, H, true);  /* fill everything */
    draw_reset_clip();
    int ok = 1;
    /* Inside clip: drawn */
    if (draw_get_pixel(15, 15) != 0xFF) ok = 0;
    /* Outside clip: not drawn */
    if (draw_get_pixel(5, 5) != 0x00) ok = 0;
    if (draw_get_pixel(25, 25) != 0x00) ok = 0;
    if (ok) PASS(); else FAIL("clip didn't constrain");
}

static void test_copy_rect(void) {
    TEST("copy_rect pixel integrity");
    clear();
    /* Draw gradient */
    for (int x = 0; x < 32; x++) {
        draw_set_color((uint8_t)(x * 8));
        draw_pixel(x, 0);
    }
    draw_copy_rect(0, 0, 50, 0, 32, 1);
    int ok = 1;
    for (int x = 0; x < 32; x++) {
        if (draw_get_pixel(x, 0) != draw_get_pixel(50 + x, 0)) ok = 0;
    }
    if (ok) PASS(); else FAIL("copy mismatch");
}

static void test_flood_fill(void) {
    TEST("flood_fill stops at border color");
    clear();
    draw_set_color(0xFF);
    draw_rect(10, 10, 30, 20, false);
    draw_set_color(0x42);
    draw_flood_fill(20, 18, 0xFF);
    int ok = 1;
    /* Inside: filled */
    if (draw_get_pixel(20, 18) != 0x42) ok = 0;
    /* Outside: not filled */
    if (draw_get_pixel(5, 5) != 0x00) ok = 0;
    if (ok) PASS(); else FAIL("fill leaked or missed");
}

static void test_font_parse(void) {
    TEST("BGI Triplex font parses successfully");
    bgi_font_t font;
    bool ok = bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);
    if (ok && font.num_chars == 223 && font.first_char == 0x20 &&
        font.strokes != NULL && font.widths != NULL)
        PASS();
    else
        FAIL("parse failed or wrong metrics");
}

static void test_font_string_width(void) {
    TEST("BGI string width measurement");
    bgi_font_t font;
    bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);
    int16_t w1 = bgi_font_string_width(&font, "A", 1, 1);
    int16_t w5 = bgi_font_string_width(&font, "AAAAA", 5, 1);
    if (w5 == w1 * 5 && w1 > 0)
        PASS();
    else
        FAIL("width not proportional");
}

static void test_rounded_rect(void) {
    TEST("rounded_rect corners differ from regular rect");
    clear();
    draw_set_color(0xFF);
    draw_rounded_rect(10, 10, 40, 30, 8, false);
    /* Corner pixel of a regular rect would be at (10,10).
     * Rounded rect should NOT have a pixel at the exact corner. */
    int ok = 1;
    if (draw_get_pixel(10, 10) != 0x00) ok = 0;  /* corner rounded away */
    if (draw_get_pixel(20, 10) == 0x00) ok = 0;  /* mid-top edge present */
    if (ok) PASS(); else FAIL("corners not rounded");
}

/* ══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("RIPlib v1.0 — Drawing Engine Tests\n");
    printf("==================================\n\n");

    test_pixel();
    test_hline();
    test_rect_fill();
    test_rect_outline();
    test_circle();
    test_write_mode_xor();
    test_write_mode_not();
    test_clip();
    test_copy_rect();
    test_flood_fill();
    test_font_parse();
    test_font_string_width();
    test_rounded_rect();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
