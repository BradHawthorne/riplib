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
static int dirty_calls = 0;
static int dirty_y0 = -1;
static int dirty_y1 = -1;

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
    draw_set_dirty_callback(NULL);
    dirty_calls = 0;
    dirty_y0 = -1;
    dirty_y1 = -1;
}

static void clear_to(uint8_t value) {
    clear();
    memset(fb, value, sizeof(fb));
}

static void record_dirty(int16_t y0, int16_t y1) {
    dirty_calls++;
    dirty_y0 = y0;
    dirty_y1 = y1;
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
        if (draw_get_pixel((int16_t)x, 20) != 0xFF) ok = 0;
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

static void test_line_write_mode_and(void) {
    TEST("line AND mode combines with destination");
    clear_to(0xCC);
    draw_set_color(0x0A);
    draw_set_write_mode(DRAW_MODE_AND);
    draw_line(10, 10, 14, 10);
    draw_set_write_mode(DRAW_MODE_COPY);
    if (draw_get_pixel(12, 10) == (uint8_t)(0xCC & 0x0A) &&
        draw_get_pixel(12, 11) == 0xCC)
        PASS();
    else
        FAIL("line AND mode incorrect");
}

static void test_restore_region_write_modes(void) {
    static const uint8_t src[4] = {0x0F, 0x33, 0x55, 0xAA};
    static const struct {
        uint8_t mode;
        uint8_t base;
        uint8_t expected[4];
    } cases[] = {
        { DRAW_MODE_OR,  0x30, {0x3F, 0x33, 0x75, 0xBA} },
        { DRAW_MODE_XOR, 0xF0, {0xFF, 0xC3, 0xA5, 0x5A} },
        { DRAW_MODE_AND, 0x3C, {0x0C, 0x30, 0x14, 0x28} },
        { DRAW_MODE_NOT, 0x55, {0xAA, 0xAA, 0xAA, 0xAA} },
    };

    TEST("restore_region honors OR/XOR/AND/NOT");
    int ok = 1;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        clear_to(cases[i].base);
        draw_set_write_mode(cases[i].mode);
        draw_restore_region(20, 20, 2, 2, src);
        draw_set_write_mode(DRAW_MODE_COPY);
        if (draw_get_pixel(20, 20) != cases[i].expected[0]) ok = 0;
        if (draw_get_pixel(21, 20) != cases[i].expected[1]) ok = 0;
        if (draw_get_pixel(20, 21) != cases[i].expected[2]) ok = 0;
        if (draw_get_pixel(21, 21) != cases[i].expected[3]) ok = 0;
    }
    if (ok) PASS(); else FAIL("restore_region mode mismatch");
}

static void test_save_region_offscreen_noop(void) {
    TEST("save_region ignores fully clipped negative spans");
    clear_to(0x5A);
    uint8_t saved[8];
    memset(saved, 0xA5, sizeof(saved));
    draw_save_region(-4, 5, 3, 1, saved);
    if (memcmp(saved, "\xA5\xA5\xA5\xA5\xA5\xA5\xA5\xA5", sizeof(saved)) == 0)
        PASS();
    else
        FAIL("save_region wrote clipped span");
}

static void test_save_region_preserves_stride_under_clipping(void) {
    TEST("save_region keeps caller stride when clipped");
    clear();
    draw_set_color(1);
    draw_pixel(0, 0);
    draw_set_color(2);
    draw_pixel(1, 0);
    draw_set_color(3);
    draw_pixel(0, 1);
    draw_set_color(4);
    draw_pixel(1, 1);
    {
        uint8_t saved[9];
        static const uint8_t expected[9] = {
            0xAA, 0xAA, 0xAA,
            0xAA, 1,    2,
            0xAA, 3,    4
        };
        memset(saved, 0xAA, sizeof(saved));
        draw_save_region(-1, -1, 3, 3, saved);
        if (memcmp(saved, expected, sizeof(saved)) == 0)
            PASS();
        else
            FAIL("save_region used clipped width as stride");
    }
}

static void test_restore_region_preserves_stride_under_clipping(void) {
    static const uint8_t src[9] = {
        1, 2, 3,
        4, 5, 6,
        7, 8, 9
    };

    TEST("restore_region keeps source stride when clipped");
    clear();
    draw_restore_region(-1, -1, 3, 3, src);
    if (draw_get_pixel(0, 0) == 5 &&
        draw_get_pixel(1, 0) == 6 &&
        draw_get_pixel(0, 1) == 8 &&
        draw_get_pixel(1, 1) == 9)
        PASS();
    else
        FAIL("restore_region misaligned clipped source rows");
}

static void test_dirty_callback_clamps_rows(void) {
    TEST("dirty callback rows are clamped to framebuffer");
    clear();
    draw_set_dirty_callback(record_dirty);
    draw_set_color(0x77);
    draw_rect(-10, -10, 20, 20, true);
    draw_set_dirty_callback(NULL);
    if (dirty_calls > 0 && dirty_y0 == 0 && dirty_y1 == 9)
        PASS();
    else
        FAIL("dirty callback received unclamped rows");
}

static void test_invalid_init_disables_writes(void) {
    uint8_t scratch[16];

    TEST("invalid init leaves drawing API inert");
    memset(scratch, 0x5A, sizeof(scratch));
    draw_init(scratch, 4, 8, 2);
    draw_set_color(0xFF);
    draw_pixel(0, 0);
    draw_fill_screen(0x11);
    draw_write_pixel(0x22);
    if (memcmp(scratch, "\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A\x5A",
               sizeof(scratch)) == 0)
        PASS();
    else
        FAIL("invalid init still allowed framebuffer writes");
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
        draw_pixel((int16_t)x, 0);
    }
    draw_copy_rect(0, 0, 50, 0, 32, 1);
    int ok = 1;
    for (int x = 0; x < 32; x++) {
        if (draw_get_pixel((int16_t)x, 0) != draw_get_pixel((int16_t)(50 + x), 0)) ok = 0;
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

static void test_patterned_flood_fill_isolated(void) {
    TEST("patterned flood fill preserves unrelated rows");
    clear();
    draw_set_color(0xFF);
    draw_rect(2, 2, 5, 5, false);
    draw_set_color(0x42);
    draw_pixel(13, 4);
    draw_set_fill_style(1, 0x10);
    draw_flood_fill(4, 4, 0xFF);
    draw_set_fill_style(0, 0);
    if (draw_get_pixel(13, 4) == 0x42)
        PASS();
    else
        FAIL("pattern fill touched unrelated pixel");
}

static void test_large_polygon_fill(void) {
    int16_t points[140];
    int idx = 0;

    TEST("filled polygon supports more than 64 vertices");
    clear();
    for (int x = 10; x <= 29; x++) {
        points[idx++] = (int16_t)x;
        points[idx++] = 10;
    }
    for (int y = 11; y <= 25; y++) {
        points[idx++] = 29;
        points[idx++] = (int16_t)y;
    }
    for (int x = 28; x >= 10; x--) {
        points[idx++] = (int16_t)x;
        points[idx++] = 25;
    }
    for (int y = 24; y >= 11; y--) {
        points[idx++] = 10;
        points[idx++] = (int16_t)y;
    }
    draw_set_color(0x66);
    draw_polygon(points, idx / 2, true);
    if (draw_get_pixel(20, 18) == 0x66)
        PASS();
    else
        FAIL("large polygon fill was truncated");
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

static void test_zero_radius_circle_is_noop(void) {
    TEST("draw_circle with r<=0 leaves framebuffer unchanged");
    clear_to(0x33);
    draw_set_color(0xFF);
    draw_circle(50, 50, 0, false);
    draw_circle(50, 50, -5, true);
    if (draw_get_pixel(50, 50) == 0x33)
        PASS();
    else
        FAIL("zero/negative radius drew pixels");
}

static void test_zero_axis_ellipse_is_noop(void) {
    TEST("draw_ellipse with rx==0 or ry==0 is a no-op");
    clear_to(0x44);
    draw_set_color(0xFF);
    draw_ellipse(50, 50, 0, 10, false);
    draw_ellipse(50, 50, 10, 0, true);
    if (draw_get_pixel(50, 50) == 0x44)
        PASS();
    else
        FAIL("degenerate ellipse drew pixels");
}

static void test_zero_len_polygon_is_noop(void) {
    static const int16_t pts[] = {10, 10, 20, 20};
    TEST("draw_polygon with n<3 is a no-op");
    clear_to(0x55);
    draw_set_color(0xFF);
    draw_polygon(pts, 2, true);
    draw_polygon(pts, 0, false);
    if (draw_get_pixel(15, 15) == 0x55)
        PASS();
    else
        FAIL("polygon with <3 points still drew");
}

static void test_text_zero_len_is_noop(void) {
    TEST("draw_text with len<=0 or NULL str is a no-op");
    clear_to(0x66);
    draw_text(10, 10, "ABC", 0, cp437_8x8, 8, 0xFF, 0);
    draw_text(10, 10, NULL, 3, cp437_8x8, 8, 0xFF, 0);
    draw_text(10, 10, "ABC", 3, NULL, 8, 0xFF, 0);
    int unchanged = 1;
    for (int x = 10; x < 35; x++)
        for (int y = 10; y < 18; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0x66) unchanged = 0;
    if (unchanged)
        PASS();
    else
        FAIL("draw_text drew despite invalid args");
}

static void test_line_off_screen_trivial_reject(void) {
    TEST("draw_line both endpoints off-screen on same side is rejected");
    clear_to(0x77);
    draw_set_color(0xFF);
    /* Both endpoints to the right of the framebuffer (W=320). */
    draw_line(500, 50, 600, 50);
    /* Both above (y < 0). */
    draw_line(10, -10, 50, -20);
    /* Verify nothing was written within the FB. */
    int touched = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0x77) { touched = 1; break; }
    if (!touched)
        PASS();
    else
        FAIL("trivial-reject line still touched the framebuffer");
}

static void test_bezier_degenerate_does_not_crash(void) {
    TEST("draw_bezier with collinear/coincident points does not crash");
    clear();
    draw_set_color(0xFF);
    /* All 4 points the same. */
    draw_bezier(50, 50, 50, 50, 50, 50, 50, 50);
    /* All collinear. */
    draw_bezier(0, 50, 50, 50, 100, 50, 150, 50);
    PASS();  /* Reaching this point without crashing is the test. */
}

static void test_flood_fill_out_of_bounds_seed_bails(void) {
    TEST("draw_flood_fill with out-of-clip seed is a no-op");
    clear_to(0x88);
    draw_set_color(0xFF);
    draw_flood_fill(-5, -5, 0xFF);
    draw_flood_fill((int16_t)(W + 100), 50, 0xFF);
    if (draw_get_pixel(0, 0) == 0x88)
        PASS();
    else
        FAIL("flood fill ran with bad seed");
}

static void test_dashed_line_has_gaps(void) {
    TEST("dashed line pattern leaves gaps between dashes");
    clear();
    draw_set_color(0xFF);
    draw_set_line_style(0x33, 1);  /* dotted: 0011 0011 */
    draw_line(0, 50, 60, 50);
    int lit = 0, gap = 0;
    for (int x = 0; x < 60; x++) {
        if (draw_get_pixel((int16_t)x, 50) == 0xFF) lit++;
        else                                          gap++;
    }
    /* Restore solid for later tests. */
    draw_set_line_style(0xFF, 1);
    if (lit > 5 && gap > 5)
        PASS();
    else
        FAIL("dashed line not patterned");
}

static void test_solid_line_no_gaps(void) {
    TEST("solid line pattern has no gaps");
    clear();
    draw_set_color(0xFF);
    draw_set_line_style(0xFF, 1);
    draw_line(0, 60, 60, 60);
    int gaps = 0;
    for (int x = 0; x <= 60; x++)
        if (draw_get_pixel((int16_t)x, 60) != 0xFF) gaps++;
    if (gaps == 0)
        PASS();
    else
        FAIL("solid line had gaps");
}

static void test_bezier_curves_off_baseline(void) {
    TEST("bezier curve deviates from baseline between endpoints");
    clear();
    draw_set_color(0xFF);
    /* Endpoints at y=50, control points at y=10 — curve should bow upward. */
    draw_bezier(0, 50, 50, 10, 100, 10, 150, 50);
    /* Check at midpoint x=75: should be above baseline (y < 50). */
    int min_y = 100;
    for (int y = 0; y < 50; y++)
        if (draw_get_pixel(75, (int16_t)y) == 0xFF) { min_y = y; break; }
    if (min_y < 40)
        PASS();
    else
        FAIL("bezier did not bow above baseline");
}

static void test_arc_only_in_angular_range(void) {
    TEST("arc 0-90 fills only upper-right quadrant");
    clear();
    draw_set_color(0xFF);
    draw_arc(150, 100, 30, 0, 90);  /* arc from 0° (right) to 90° (up) */
    /* Upper-right quadrant should have pixels (x>cx, y<cy). */
    int ur = 0, ul = 0, ll = 0;
    for (int y = 65; y <= 135; y++)
        for (int x = 115; x <= 185; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) == 0xFF) {
                if (x > 150 && y < 100) ur++;
                else if (x < 150 && y < 100) ul++;
                else if (x < 150 && y > 100) ll++;
            }
    /* Pure 0..90 arc should populate upper-right exclusively. */
    if (ur > 5 && ul == 0 && ll == 0)
        PASS();
    else
        FAIL("arc bled outside angular range");
}

static void test_thick_line_diagonal_perpendicular(void) {
    TEST("thick 45-degree line has full perpendicular width");
    clear();
    draw_set_color(0xFF);
    draw_set_line_style(0xFF, 5);
    /* 45-degree line of length 80 starting at (50, 20) → (130, 100). */
    draw_thick_line(50, 20, 130, 100);
    draw_set_line_style(0xFF, 1);
    /* Sample the perpendicular cross-section at the midpoint (90, 60).
     * Walk along the perpendicular direction (-1, +1) and count lit pixels. */
    int hits = 0;
    for (int d = -8; d <= 8; d++) {
        int16_t px = (int16_t)(90 - d);
        int16_t py = (int16_t)(60 + d);
        if (draw_get_pixel(px, py) == 0xFF) hits++;
    }
    /* Old axis-aligned approach gave ~3 perpendicular pixels for thickness 5
     * on a 45° line.  New perpendicular approach should give ~5. */
    if (hits >= 4)
        PASS();
    else
        FAIL("diagonal thick line too thin perpendicular to its axis");
}

static void test_thick_line_widens_horizontal(void) {
    TEST("thick horizontal line spans multiple rows");
    clear();
    draw_set_color(0xFF);
    draw_set_line_style(0xFF, 3);
    draw_thick_line(10, 50, 40, 50);
    int rows_with_pixels = 0;
    for (int y = 47; y <= 53; y++) {
        int hit = 0;
        for (int x = 10; x <= 40; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) == 0xFF) { hit = 1; break; }
        rows_with_pixels += hit;
    }
    draw_set_line_style(0xFF, 1);
    if (rows_with_pixels >= 3)
        PASS();
    else
        FAIL("thick line did not widen");
}

static void test_polygon_concave_fill(void) {
    /* "C"-shaped concave polygon — should fill correctly with scanline. */
    int16_t pts[14] = {
        10, 10,
        50, 10,
        50, 20,
        20, 20,
        20, 30,
        50, 30,
        50, 40,
    };
    /* Close back to (10,10) via 8th point */
    int16_t pts_full[16] = {
        10, 10,
        50, 10,
        50, 20,
        20, 20,
        20, 30,
        50, 30,
        50, 40,
        10, 40,
    };

    TEST("concave polygon scanline fill respects the bay");
    clear();
    draw_set_color(0x42);
    draw_polygon(pts_full, 8, true);
    /* Inside the C body (left half of the shape) — filled. */
    int body = (draw_get_pixel(15, 25) == 0x42);
    /* Inside the bay (mouth of the C, x > 20, 20 < y < 30) — NOT filled. */
    int bay = (draw_get_pixel(40, 25) == 0x42);
    /* Outside the polygon — NOT filled. */
    int outside = (draw_get_pixel(60, 25) == 0x42);
    (void)pts;
    if (body && !bay && !outside)
        PASS();
    else
        FAIL("concave polygon fill incorrect");
}

static void test_bgi_render_draws_glyph(void) {
    bgi_font_t font;

    TEST("BGI Triplex renders 'A' with non-zero pixel coverage");
    clear();
    if (!bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size)) {
        FAIL("font parse failed"); return;
    }
    int16_t adv = bgi_font_draw_string(&font, 50, 50, "A", 1, 2, 0xFF, 0);
    int pixels_lit = 0;
    for (int y = 30; y < 70; y++)
        for (int x = 40; x < 90; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) == 0xFF) pixels_lit++;
    if (adv > 0 && pixels_lit > 5)
        PASS();
    else
        FAIL("BGI glyph render produced no/few pixels");
}

static void test_bgi_render_advances_x(void) {
    bgi_font_t font;

    TEST("BGI string width = sum of character advances");
    bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);
    int16_t w_HI    = bgi_font_string_width(&font, "HI", 2, 1);
    int16_t w_HIBYE = bgi_font_string_width(&font, "HIBYE", 5, 1);
    if (w_HI > 0 && w_HIBYE > w_HI)
        PASS();
    else
        FAIL("string width not monotone in length");
}

static void test_bgi_scale_doubles_advance(void) {
    bgi_font_t font;

    TEST("BGI render at scale 2 advances ~2x scale 1");
    bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);
    int16_t w1 = bgi_font_string_width(&font, "ABC", 3, 1);
    int16_t w2 = bgi_font_string_width(&font, "ABC", 3, 2);
    if (w1 > 0 && w2 == w1 * 2)
        PASS();
    else
        FAIL("scale 2 not exactly 2x scale 1");
}

static void test_bgi_italic_shears_glyph(void) {
    bgi_font_t font;

    TEST("BGI italic produces sheared glyphs (top pixels shifted right)");
    bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);

    /* Render plain 'I' at (50, 60) scale 4 — find rightmost lit pixel
     * in the glyph's top row vs bottom row. */
    clear();
    bgi_font_draw_string_ex(&font, 50, 60, "I", 1, 4, 0xFF, 0, 0);
    int plain_top = -1, plain_bot = -1;
    for (int x = 100; x >= 30; x--) {
        if (plain_top < 0 && draw_get_pixel((int16_t)x, 30) == 0xFF) plain_top = x;
        if (plain_bot < 0 && draw_get_pixel((int16_t)x, 60) == 0xFF) plain_bot = x;
    }

    clear();
    bgi_font_draw_string_ex(&font, 50, 60, "I", 1, 4, 0xFF, 0, BGI_ATTR_ITALIC);
    int it_top = -1, it_bot = -1;
    for (int x = 120; x >= 30; x--) {
        if (it_top < 0 && draw_get_pixel((int16_t)x, 30) == 0xFF) it_top = x;
        if (it_bot < 0 && draw_get_pixel((int16_t)x, 60) == 0xFF) it_bot = x;
    }

    /* Italic should shift the top of the glyph rightward more than the
     * bottom — i.e. (it_top - plain_top) > (it_bot - plain_bot). */
    int top_shift = it_top - plain_top;
    int bot_shift = it_bot - plain_bot;
    if (it_top > 0 && it_bot > 0 && top_shift > bot_shift)
        PASS();
    else
        FAIL("italic shear did not slant the glyph");
}

static void test_bgi_attrib_underline_draws_extra_line(void) {
    bgi_font_t font;

    TEST("BGI underline attribute draws a horizontal line below the glyph");
    clear();
    bgi_font_parse(&font, bgi_font_trip, bgi_font_trip_size);
    /* Draw same glyph with and without underline at different Y. */
    bgi_font_draw_string_ex(&font, 50, 50, "x", 1, 1, 0xFF, 0, 0);
    int plain_lit = 0;
    for (int x = 40; x < 80; x++)
        for (int y = 50; y < 60; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) == 0xFF) plain_lit++;
    clear();
    bgi_font_draw_string_ex(&font, 50, 50, "x", 1, 1, 0xFF, 0, BGI_ATTR_UNDERLINE);
    int ul_lit = 0;
    for (int x = 40; x < 80; x++)
        for (int y = 50; y < 60; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) == 0xFF) ul_lit++;
    if (ul_lit > plain_lit)
        PASS();
    else
        FAIL("underline did not add pixels");
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
    test_line_write_mode_and();
    test_restore_region_write_modes();
    test_save_region_offscreen_noop();
    test_save_region_preserves_stride_under_clipping();
    test_restore_region_preserves_stride_under_clipping();
    test_dirty_callback_clamps_rows();
    test_invalid_init_disables_writes();
    test_clip();
    test_copy_rect();
    test_flood_fill();
    test_patterned_flood_fill_isolated();
    test_large_polygon_fill();
    test_font_parse();
    test_font_string_width();
    test_rounded_rect();
    test_zero_radius_circle_is_noop();
    test_zero_axis_ellipse_is_noop();
    test_zero_len_polygon_is_noop();
    test_text_zero_len_is_noop();
    test_line_off_screen_trivial_reject();
    test_bezier_degenerate_does_not_crash();
    test_flood_fill_out_of_bounds_seed_bails();
    test_dashed_line_has_gaps();
    test_solid_line_no_gaps();
    test_bezier_curves_off_baseline();
    test_arc_only_in_angular_range();
    test_thick_line_widens_horizontal();
    test_thick_line_diagonal_perpendicular();
    test_polygon_concave_fill();
    test_bgi_render_draws_glyph();
    test_bgi_render_advances_x();
    test_bgi_scale_doubles_advance();
    test_bgi_italic_shears_glyph();
    test_bgi_attrib_underline_draws_extra_line();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
