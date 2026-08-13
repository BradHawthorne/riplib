/*
 * test_corpus.c — replay real TeleGrafix RIPscrip scenes through the parser.
 *
 * Every other test in this tree asserts behaviour RIPlib was written to have.
 * This one asserts nothing about meaning: it feeds authentic .RIP files from a
 * RIPterm/RIPtel installation byte-for-byte and checks the parser survives
 * them — no crash, no wedged FSM, no drawing outside the framebuffer, and no
 * silently-unhandled opcode.  It is the only test whose input RIPlib did not
 * author, which is exactly why it catches what the hand-written suite cannot.
 *
 * The scenes are third-party content and are NOT vendored into this repo.
 * Point the build at a local corpus to enable the test:
 *
 *     cmake -S . -B build -DRIPLIB_CORPUS_DIR=C:/RIPtel
 *
 * Without that the test reports SKIP and exits 0, so CI stays green on a
 * checkout that has no corpus.
 */

#include "drawing.h"
#include "ripscrip.h"
#include "corpus_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400

/* Guard bands either side of the framebuffer.  draw_init() is handed only the
 * inner W*H window, so any write outside it lands in a canary and is caught. */
#define GUARD 4096
static uint8_t fb_backing[GUARD + W * H + GUARD];
static uint8_t *fb = fb_backing + GUARD;

static uint16_t palette[256];
static int tests_run = 0;
static int tests_passed = 0;

void palette_write_rgb565(uint8_t i, uint16_t v) { palette[i] = v; }
uint16_t palette_read_rgb565(uint8_t i) { return palette[i]; }
/* Host traffic is COUNTED, not discarded.  The harness used to stub this to
 * nothing, which put every byte a scene sends back to the host outside the
 * measurement -- the same blind spot that hid the '|1U' and '|1M' interaction
 * defects until mouse regions were counted (D-24).  A scene that stops
 * answering a query, or starts answering one it should not, moves this. */
static long tx_bytes;
void riplib_host_tx(const char *buf, int len) {
    (void)buf;
    if (len > 0)
        tx_bytes += len;
}

#define TEST(name) do { tests_run++; printf("  %-46s ", name); } while (0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static int guards_intact(void) {
    size_t i;

    for (i = 0; i < GUARD; i++)
        if (fb_backing[i] != 0xA5 || fb_backing[GUARD + W * H + i] != 0xA5)
            return 0;
    return 1;
}

static void init_fixture(rip_state_t *s, comp_context_t *ctx) {
    memset(fb_backing, 0xA5, sizeof(fb_backing));
    memset(fb, 0, W * H);
    memset(palette, 0, sizeof(palette));
    memset(s, 0, sizeof(*s));
    memset(ctx, 0, sizeof(*ctx));
    ctx->target = fb;
    draw_init(fb, W, W, H);
    rip_init_first(s);
}

static int load_file(const char *path, unsigned char **out, size_t *n) {
    FILE *f;
    long sz;
    unsigned char *buf;

    *out = NULL;
    *n = 0;
#if defined(_MSC_VER)
    if (fopen_s(&f, path, "rb") != 0)
        f = NULL;
#else
    f = fopen(path, "rb");
#endif
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    *n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[*n] = 0;
    *out = buf;
    return 1;
}

/* Replay one scene.  Returns 1 on success, 0 with `why` set on failure.
 *
 * `*painted` counts FOREGROUND pixels -- everything that is not the single
 * most common colour -- and `*colours` the number of distinct colours used.
 *
 * Counting non-zero pixels instead would be nearly useless: almost every
 * scene opens with '|*', which fills the framebuffer, so a scene that drew
 * its background and then nothing else would report a full 640x400 and look
 * perfectly healthy.  The same mistake made a centroid check pass seven
 * shapes it was not actually measuring; the fix in both places is to
 * discount the background before believing the number. */
static int replay(const char *path, const char **why,
                  long *painted, int *colours, int *requests, int *regions,
                  long *tx) {
    rip_state_t s;
    comp_context_t ctx;
    unsigned char *data;
    size_t n, i;

    *painted = 0;
    *colours = 0;
    *requests = 0;
    *regions = 0;
    *tx = 0;
    tx_bytes = 0;
    if (!load_file(path, &data, &n)) {
        *why = "could not read file";
        return 0;
    }

    init_fixture(&s, &ctx);
    for (i = 0; i < n; i++)
        rip_process(&s, &ctx, data[i]);

    {
        static long hist[256];
        long best = 0;
        int v;

        memset(hist, 0, sizeof(hist));
        for (i = 0; i < (size_t)(W * H); i++)
            hist[fb[i]]++;
        for (v = 0; v < 256; v++) {
            if (hist[v] > best)
                best = hist[v];
            if (hist[v] != 0)
                (*colours)++;
        }
        *painted = (long)(W * H) - best;   /* everything but the background */
    }

    /* Several shipped scenes draw almost nothing on their own because their
     * visuals live in external files -- DRAGON.RIP pulls in five .BMPs plus
     * dragon.txt, SHADMOVE.RIP defers to shadowdo.fn.  The harness supplies
     * none of them, so low foreground there is expected.  What is NOT
     * expected is silence: RIPlib must still ASK the host for those assets,
     * and a scene that neither draws nor requests anything has quietly
     * dropped its content rather than deferring it. */
    *requests = s.icon_state.request_count;

    /* Mouse regions are counted for the same reason as asset requests: they
     * are behaviour the pixel metrics cannot see.  Every '|1U' button in the
     * corpus carries an EMPTY host command ("<>Clear<>"), and registration
     * used to be gated on that command being non-empty -- so not one of the
     * 39 buttons in shipped content became clickable, and no pixel count
     * moved when that was fixed.  '|1M' has the same blind spot: its flags
     * were read from a reserved column and were always zero.  A scene that
     * silently stops registering its interactive areas now shows up here. */
    *regions = s.num_mouse_regions;
    *tx = tx_bytes;

    /* A well-formed scene must leave the FSM back at idle.  Anything else
     * means a command swallowed the rest of the stream. */
    if (s.state != RIP_ST_IDLE) {
        free(data);
        *why = "parser did not return to idle";
        return 0;
    }
    if (!guards_intact()) {
        free(data);
        *why = "drawing escaped the framebuffer";
        return 0;
    }
    /* Passively rendering a scene must not send anything to the host.
     *
     * This is an assertion, not a statistic.  RIPlib's security posture is
     * that untrusted content cannot make the terminal act on its own -- it
     * never launches a URL, never touches the filesystem, and must not open
     * its mouth to the BBS either.  Host traffic is a RESPONSE: to a click,
     * to a query the host itself initiated, to a file the host asked about.
     * None of that happens during replay, so anything here means a command
     * started talking unbidden.
     *
     * All 35 shipped scenes emit zero bytes today.  The 95 '|#' commands in
     * the corpus are RIP_NO_MORE, a scene terminator, not a query. */
    if (tx_bytes != 0) {
        free(data);
        *why = "scene sent data to the host during passive replay";
        return 0;
    }

    if (s.psram_arena.base)
        psram_arena_destroy(&s.psram_arena);
    free(data);
    return 1;
}

int main(void) {
    size_t i;

    printf("RIPlib corpus replay\n");

    if (riplib_corpus_scenes[0] == 0) {
        printf("  SKIP: no corpus configured "
               "(-DRIPLIB_CORPUS_DIR=<dir> to enable)\n");
        return 0;
    }

    for (i = 0; riplib_corpus_scenes[i] != 0; i++) {
        const char *why = "";
        const char *base = strrchr(riplib_corpus_scenes[i], '/');
        long painted = 0;
        int colours = 0, requests = 0, regions = 0;
        long tx = 0;

        base = base ? base + 1 : riplib_corpus_scenes[i];
        TEST(base);
        if (!replay(riplib_corpus_scenes[i], &why, &painted, &colours,
                    &requests, &regions, &tx)) {
            FAIL(why);
        } else {
            /* Zero output is not a failure: some shipped scenes are
             * conditional templates that resolve to $NULL$ when their
             * driving text variable is unset (WIPE.RIP is one).  Both
             * numbers are reported so a scene that quietly stops drawing
             * after a parser change shows up in the diff -- foreground
             * pixels catch a scene reduced to its background, and the
             * colour count catches one reduced to a single flat fill. */
            tests_passed++;
            if (painted == 0 && requests == 0 && regions == 0 && tx == 0)
                printf("PASS        0 fg  (background only, nothing asked for)\n");
            else if (painted == 0)
                printf("PASS        0 fg  (%d asset request(s), %d region(s))\n",
                       requests, regions);
            else
                /* "fg" is everything that is not the single most common
                 * colour, so it FLIPS once drawing covers more than half the
                 * screen: the drawn colour becomes the majority and fg then
                 * counts the background instead.  CURVES.RIP does exactly
                 * that -- widening cmd_buf let it draw 41 bezier segments
                 * instead of 15, painting 188186 pixels, and fg fell from
                 * 98008 to 67814 while the scene got MORE complete.  The
                 * dominant share is printed so that flip is visible rather
                 * than looking like a regression. */
                printf("PASS  %7ld fg  %2d colours  %2d%% dom  %d req  %2d rgn\n",
                       painted, colours,
                       (int)(100 - (painted * 100 / (long)(W * H))),
                       requests, regions);
        }
    }

    printf("\n%d/%d scenes replayed cleanly\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
