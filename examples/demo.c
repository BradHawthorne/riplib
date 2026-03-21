/*
 * RIPlib Demo — Renders shapes to a raw framebuffer and saves as PGM
 *
 * Build: cmake -DRIPLIB_BUILD_EXAMPLES=ON .. && make
 * Run:   ./riplib_demo > output.pgm
 */

#include "drawing.h"
#include "bgi_font.h"
#include <stdio.h>
#include <string.h>

/* Font data */
#include "font_bgi_trip.h"
#include "font_cp437_8x8.h"

#define W 640
#define H 400

static uint8_t fb[W * H];
static bgi_font_t triplex;

/* Bit-reverse CP437 8x8 for draw_text compatibility */
static uint8_t font_8x8[2048];
static void init_font(void) {
    for (int i = 0; i < 2048; i++) {
        uint8_t b = cp437_8x8[i];
        font_8x8[i] = ((b & 0x80) >> 7) | ((b & 0x40) >> 5) |
                       ((b & 0x20) >> 3) | ((b & 0x10) >> 1) |
                       ((b & 0x08) << 1) | ((b & 0x04) << 3) |
                       ((b & 0x02) << 5) | ((b & 0x01) << 7);
    }
}

int main(void) {
    memset(fb, 0, sizeof(fb));
    draw_init(fb, W, W, H);
    init_font();

    /* Parse Triplex font */
    bgi_font_parse(&triplex, bgi_font_trip, bgi_font_trip_size);

    /* Draw some shapes */
    draw_set_color(0xFF);  /* white */
    draw_rounded_rect(20, 20, 200, 80, 10, false);

    draw_set_color(0xE0);  /* red */
    draw_circle(320, 200, 60, true);

    draw_set_color(0x1C);  /* green */
    draw_bezier(50, 350, 200, 100, 400, 300, 600, 150);

    /* Bitmap text */
    draw_set_color(0xFF);
    draw_text(30, 40, "RIPlib Demo", 11, font_8x8, 8, 0xFF, 0x00);

    /* BGI stroke font */
    bgi_font_draw_string(&triplex, 30, 90, "Triplex Font", 12, 2, 0xFF, 0);

    /* Output as PGM (grayscale image) */
    fprintf(stdout, "P5\n%d %d\n255\n", W, H);
    fwrite(fb, 1, W * H, stdout);

    return 0;
}
