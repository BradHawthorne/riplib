/*
 * test_fuzz_seeded.c — self-contained mutation fuzzer for the command layer.
 *
 * Complements fuzz_parser.c rather than replacing it.  That one is a
 * libFuzzer target: better coverage-guided search, but it needs clang plus
 * -fsanitize=fuzzer, so it is optional and does not run in the normal CI
 * matrix.  This one is plain C99 with a fixed seed, runs anywhere the rest
 * of the suite runs, and is wired into ctest.
 *
 * WHY IT EXISTS.  An earlier version of this fuzzer ran 200,000 iterations
 * clean over a real stack-smash: rip_render_text() passed an unbounded
 * length to unescape_text() writing into a 256-byte buffer, safe only
 * because cmd_buf happened to be 256 too.  It survived because every seed
 * was SHORT, so the accumulator was never driven near its limit.  Real
 * content is long — HAWK.RIP carries 614 argument characters across 11
 * backslash continuations — and that is what found the bug.
 *
 * So this harness deliberately does two things the old one did not:
 *
 *   - builds long commands, sized around and past cmd_buf, for the
 *     opcodes that take long argument runs or free text;
 *   - splits them with '\' + CRLF continuations, the way real scenes do.
 *
 * The framebuffer is bracketed by guard bands, so an out-of-bounds write is
 * caught here rather than silently corrupting adjacent memory.
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License.  See LICENSE.
 */

#include "drawing.h"
#include "ripscrip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400
#define GUARD 8192

static unsigned char fb_backing[GUARD + W * H + GUARD];
static unsigned char *fb = fb_backing + GUARD;
static uint16_t palette[256];

void palette_write_rgb565(uint8_t i, uint16_t v) { palette[i] = v; }
uint16_t palette_read_rgb565(uint8_t i) { return palette[i]; }
void riplib_host_tx(const char *b, int n) { (void)b; (void)n; }

/* xorshift32, fixed seed: a failure must be reproducible. */
static uint32_t rng_state = 0x12345678u;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int guards_intact(void) {
    size_t i;
    for (i = 0; i < GUARD; i++)
        if (fb_backing[i] != 0xA5 || fb_backing[GUARD + W * H + i] != 0xA5)
            return 0;
    return 1;
}

/* One seed per command changed or added during the alignment work, plus the
 * malformed variants that exercise each length guard. */
static const char *const seeds[] = {
    "!|<05041010701070701070034020606020600360208040405004201K901K902K202K|",
    "!|<ZZZZ|",  "!|<01ZZ00000000|",
    "!|;2S2S050K0K0000|", "!|;ZZZZZZZZZZZZZZ|",
    "!|d0#8001u|", "!|D0z02800#0001u|", "!|DZZZZZ|",
    "!|&20151G0M1M|", "!|-203F1G0M1M|", "!|]50151G0M20601M|",
    "!|[503F1G0M20601M|", "!|+803F1G0M20601M|", "!|_B03F90601G0M|",
    "!|K00001O1O|", "!|J10|", "!|J1S|", "!|JZZ|",
    "!|3D0056|", "!|n3000|", "!|n2000|", "!|1i000000000002|",
    "!|s2U2U2U2U2U2U2U2U07|",
    "!|y1000000000001Q00001a00000000|",
    "!|@1010some text with \\| and \\\\ escapes|",
    "!|p0A05050F0F1414191923232D2D3737414147474F4F|",

    /* Commands with a TRAILING STRING, and the boundary that matters: a
     * payload of exactly the record's fixed width, so the string is empty and
     * the pointer sits one past the end.  Every string-offset defect found in
     * D-19 and D-25 lived here, and not one of the seeds above reaches it --
     * three million iterations of the set above never entered this space.
     * The '|1A' case is the literal payload NEWS.RIP sends. */
    "!|1A010000|", "!|1A010000chime.wav|",
    "!|1bVU0QYY1S0000000000back.bmp|", "!|1bVU0QYY1S0000000000|",
    "!|1R00000000dragon.txt|", "!|1R00000000|",
    "!|1W0TESTICON|", "!|1W0|",
    "!|1I0A0A00000icon.icn|",
    "!|3G00000000http://example.com/x|", "!|3G00000000|",
    "!|3R00010012345678MYVAR|", "!|3R0001001234567|",
    "!|1D000000name,10:?prompt?default|",
    "!|1F0000000file.bmp|",

    /* Mouse regions and buttons.  '|1U' with an EMPTY host command is the
     * shape every button in the shipped corpus has, and the shape that left
     * memcpy reading from NULL once hostless buttons started registering. */
    "!|1M010A0A1E0U1000000SELECT 1\\r|", "!|1M010A0A1E0U0000000|",
    "!|1U0A0A1E0U2G10<>Label<>HOST|", "!|1U0A0A1E0U0000<>Clear<>|",
    "!|1U0A0A1E0U0000<><>|", "!|1U0A0A1E0U0000bare|",
    "!|1t0some region text|", "!|1T0A0A32320000|",

    /* Level 2 -- invisible to every seed above, and the level whose offsets
     * were only ever checked by hand until D-23. */
    "!|2P1000A140U00020000|", "!|2P1000A140U0001|",
    "!|2s100|", "!|2s000|", "!|2p1000|",
    "!|2C0002WZKA810000ZK72000000|",
    "!|2A100|", "!|2B100|", "!|2E100|", "!|2T100|", "!|2Y100|",
    "!|2R0001|", "!|2W1000A140U00000000file.bmp|",

    /* Multi-signature and radix-sensitive commands: '|h' dispatches on four
     * distinct payload widths, and these four decode base 64 rather than 36. */
    "!|h00000000|", "!|h000000|", "!|h0000|", "!|h000|",
    "!|d0#8001u|", "!|y1a1a000000000000000000000000|",

    /* The two corpus-backed tolerances, at both widths they really occur. */
    "!|k04|", "!|k0|", "!|=00000000|", "!|=0000|",
};

/* Build a long command of `nargs` argument characters, optionally broken
 * across lines with '\' continuations every `cont` characters. */
static size_t build_long(char *out, size_t cap, char cmd, int nargs, int cont) {
    static const char D[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#&";
    size_t o = 0;
    int i;

    if (cap < 16) return 0;
    out[o++] = '!'; out[o++] = '|'; out[o++] = cmd;
    for (i = 0; i < nargs && o + 5 < cap; i++) {
        out[o++] = D[rnd() % 64];
        if (cont && (i % cont) == (cont - 1)) {
            out[o++] = '\\'; out[o++] = '\r'; out[o++] = '\n';
        }
    }
    if (o + 1 < cap) out[o++] = '|';
    out[o] = '\0';
    return o;
}

int main(int argc, char **argv) {
    static char buf[4096];
    /* Opcodes that take long argument runs or free text — the ones whose
     * payload can approach or exceed the accumulator. */
    static const char longcmds[] = "pPl<xzt@T\"";
    rip_state_t s;
    comp_context_t ctx;
    long iters = (argc > 1) ? atol(argv[1]) : 100000;
    long iter;
    int nseeds = (int)(sizeof(seeds) / sizeof(seeds[0]));

    printf("RIPlib seeded fuzz: %ld iterations\n", iters);

    for (iter = 0; iter < iters; iter++) {
        size_t len, i, nmut;

        /* One in four drives a LONG command past cmd_buf. */
        if (rnd() % 4 == 0) {
            char cmd = longcmds[rnd() % (sizeof(longcmds) - 1)];
            int nargs = (int)(rnd() % 1600) + 200;
            int cont  = (rnd() % 2) ? (int)(rnd() % 70) + 8 : 0;
            len = build_long(buf, sizeof(buf) - 1, cmd, nargs, cont);
            if (len == 0) continue;
        } else {
            const char *seed = seeds[rnd() % nseeds];
            len = strlen(seed);
            if (len >= sizeof(buf)) continue;
            memcpy(buf, seed, len + 1);
        }

        /* Truncation is what exercises the `len >=` guards: a payload that
         * stops mid-field is exactly the case a bounds check exists for. */
        if (rnd() % 3 == 0 && len > 4) {
            len = 3 + rnd() % (len - 3);
            buf[len] = '\0';
        }
        nmut = 1 + rnd() % 6;
        for (i = 0; i < nmut; i++) {
            size_t at = rnd() % len;
            uint32_t r = rnd() % 3;
            if (r == 0)      buf[at] = (char)('0' + rnd() % 10);
            else if (r == 1) buf[at] = (char)('A' + rnd() % 26);
            else             buf[at] = (char)(rnd() % 256);
        }

        memset(fb_backing, 0xA5, sizeof(fb_backing));
        memset(fb, 0, W * H);
        memset(&s, 0, sizeof(s));
        memset(&ctx, 0, sizeof(ctx));
        ctx.target = fb;
        draw_init(fb, W, W, H);
        rip_init_first(&s);

        for (i = 0; i < len; i++)
            rip_process(&s, &ctx, (uint8_t)buf[i]);

        if (!guards_intact()) {
            printf("GUARD VIOLATION at iteration %ld\n  input: ", iter);
            for (i = 0; i < len; i++)
                printf("%c", (buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.');
            printf("\n");
            return 1;
        }
        if (s.psram_arena.base)
            psram_arena_destroy(&s.psram_arena);
    }

    printf("%ld iterations, no guard violations, no crash\n", iters);
    return 0;
}
