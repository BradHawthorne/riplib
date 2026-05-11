/*
 * rip2ppm — Render a RIPscrip stream to a PPM image
 *
 * Reads RIPscrip wire bytes from a file (or stdin), runs them through
 * the parser, and writes the final 640x400 framebuffer to a PPM image.
 *
 * Build: cmake -DRIPLIB_BUILD_EXAMPLES=ON .. && cmake --build .
 * Run:   ./rip2ppm input.rip output.ppm
 *        ./rip2ppm input.rip          (PPM goes to stdout)
 *        ./rip2ppm - output.ppm       (RIP from stdin)
 */

#include "ripscrip.h"
#include "drawing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400

static uint8_t fb[W * H];

static void rgb565_to_rgb888(uint16_t v, uint8_t out[3]) {
    out[0] = (uint8_t)((v >> 8) & 0xF8);  /* R: 5 bits high */
    out[1] = (uint8_t)((v >> 3) & 0xFC);  /* G: 6 bits middle */
    out[2] = (uint8_t)((v << 3) & 0xF8);  /* B: 5 bits low */
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s INFILE [OUTFILE]\n", argv[0]);
        fprintf(stderr, "  INFILE  — '.rip' wire bytes, or '-' for stdin\n");
        fprintf(stderr, "  OUTFILE — '.ppm' image, or omit for stdout\n");
        return 1;
    }

    FILE *in = (strcmp(argv[1], "-") == 0) ? stdin : fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }

    static rip_state_t s;
    comp_context_t ctx;
    memset(&s, 0, sizeof(s));
    memset(&ctx, 0, sizeof(ctx));
    memset(fb, 0, sizeof(fb));
    ctx.target = fb;
    draw_init(fb, W, W, H);
    rip_init_first(&s);

    int c;
    while ((c = fgetc(in)) != EOF)
        rip_process(&s, &ctx, (uint8_t)c);
    if (in != stdin) fclose(in);

    FILE *out;
    if (argc == 3) {
        out = fopen(argv[2], "wb");
        if (!out) { perror(argv[2]); psram_arena_destroy(&s.psram_arena); return 1; }
    } else {
        out = stdout;
    }

    fprintf(out, "P6\n%d %d\n255\n", W, H);
    uint8_t rgb[3];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t idx = fb[y * W + x];
            rgb565_to_rgb888(palette_read_rgb565(idx), rgb);
            fwrite(rgb, 1, 3, out);
        }
    }

    if (out != stdout) fclose(out);
    psram_arena_destroy(&s.psram_arena);
    return 0;
}
