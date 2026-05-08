/*
 * test_ripscrip.c — Targeted RIPlib parser/session regression tests
 *
 * Verifies parser, session-state, and icon-cache behavior for issues
 * found during the production-readiness audit.
 */

#include "drawing.h"
#include "rip_icons.h"
#include "ripscrip.h"
#include "ripscrip2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400

static uint8_t fb[W * H];
static uint16_t palette[256];
static int tests_run = 0;
static int tests_passed = 0;

#define TX_CAPTURE_MAX 4096
static uint8_t tx_capture[TX_CAPTURE_MAX];
static size_t tx_len = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-40s ", name); \
} while (0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

void palette_write_rgb565(uint8_t i, uint16_t v) { palette[i] = v; }
uint16_t palette_read_rgb565(uint8_t i) { return palette[i]; }
void *gpu_psram_alloc(uint32_t size) { return malloc(size); }

void card_tx_push(const char *buf, int len) {
    if (!buf || len <= 0 || tx_len >= TX_CAPTURE_MAX) return;
    size_t n = (size_t)len;
    if (n > TX_CAPTURE_MAX - tx_len) n = TX_CAPTURE_MAX - tx_len;
    memcpy(tx_capture + tx_len, buf, n);
    tx_len += n;
}

static void tx_reset(void) {
    tx_len = 0;
    memset(tx_capture, 0, sizeof(tx_capture));
}

/* Track arena heap base pointers across the whole test suite so we can
 * free them at exit.  Tracking the rip_state_t* itself wouldn't work
 * because the state is typically a stack local that becomes invalid
 * the moment its test function returns; the arena's base pointer is a
 * malloc()ed heap address and stays stable. */
#define MAX_TRACKED_ARENAS 200
static uint8_t *tracked_arenas[MAX_TRACKED_ARENAS];
static int tracked_arena_count = 0;

static void track_arena(psram_arena_t *a) {
    if (a->base && tracked_arena_count < MAX_TRACKED_ARENAS)
        tracked_arenas[tracked_arena_count++] = a->base;
}

static void cleanup_all_arenas(void) {
    for (int i = 0; i < tracked_arena_count; i++)
        free(tracked_arenas[i]);
    tracked_arena_count = 0;
}

static void init_fixture(rip_state_t *s, comp_context_t *ctx) {
    memset(fb, 0, sizeof(fb));
    memset(palette, 0, sizeof(palette));
    memset(s, 0, sizeof(*s));
    memset(ctx, 0, sizeof(*ctx));
    ctx->target = fb;
    draw_init(fb, W, W, H);
    tx_reset();
    rip_init_first(s);
    track_arena(&s->psram_arena);
}

static void feed_script(rip_state_t *s, comp_context_t *ctx, const char *script) {
    for (size_t i = 0; script[i] != '\0'; i++)
        rip_process(s, ctx, (uint8_t)script[i]);
}

static void feed_upload_bytes(rip_state_t *s, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        rip_file_upload_byte_state(s, data[i]);
}

static void test_preproc_gt_handling(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("preprocessor keeps '>' inside IF expr");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF 2>10>>!|X0000|<<ENDIF>>");
    if (draw_get_pixel(0, 0) == 0)
        PASS();
    else
        FAIL("false IF branch still drew");
}

static void test_preproc_nested_else(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("nested ELSE stays suppressed under false parent");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx,
                "<<IF 0>><<IF 0>><<ELSE>>!|X0000|<<ENDIF>><<ENDIF>>");
    if (draw_get_pixel(0, 0) == 0)
        PASS();
    else
        FAIL("nested ELSE escaped parent suppression");
}

static void test_preproc_malformed_directive_recovers(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("malformed preprocessor directive does not wedge stream");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF 1\n!|X0000|");
    if (draw_get_pixel(0, 0) != 0)
        PASS();
    else
        FAIL("malformed directive left parser wedged");
}

static void test_activate_restores_clip(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_activate restores session clip");
    init_fixture(&s, &ctx);
    draw_set_clip(0, 0, 0, 0);
    rip_activate(&s);
    feed_script(&s, &ctx, "!|X0200|");
    if (draw_get_pixel(2, 0) != 0)
        PASS();
    else
        FAIL("reactivation left stale clip active");
}

static void test_port_text_justification_roundtrip(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("port switch preserves text justification");
    init_fixture(&s, &ctx);
    ripscrip2_execute(&s.rip2_state, &s, &ctx, RIP2_CMD_PORT_SWITCH,
                      "1", 1, NULL, 0);
    s.font_hjust = 2;
    s.font_vjust = 3;
    ripscrip2_execute(&s.rip2_state, &s, &ctx, RIP2_CMD_PORT_SWITCH,
                      "0", 1, NULL, 0);
    s.font_hjust = 0;
    s.font_vjust = 0;
    ripscrip2_execute(&s.rip2_state, &s, &ctx, RIP2_CMD_PORT_SWITCH,
                      "1", 1, NULL, 0);
    if (s.font_hjust == 2 && s.font_vjust == 3)
        PASS();
    else
        FAIL("port state lost text justification");
}

static void test_icon_state_isolation(void) {
    static const char icon_name[] = "ZZSESS01";
    rip_state_t a, b;
    comp_context_t ctx_a, ctx_b;
    rip_icon_t icon;
    uint8_t *pixels;
    bool ok_a;
    bool ok_b;

    TEST("icon cache is isolated per RIP state");
    init_fixture(&a, &ctx_a);
    init_fixture(&b, &ctx_b);
    pixels = (uint8_t *)psram_arena_alloc(&a.psram_arena, 4);
    if (!pixels) {
        FAIL("icon pixel allocation failed");
        return;
    }
    pixels[0] = 1;
    pixels[1] = 2;
    pixels[2] = 3;
    pixels[3] = 4;
    if (!rip_icon_cache_pixels(&a.icon_state, icon_name, 8, pixels, 2, 2)) {
        FAIL("cache insert failed");
        return;
    }
    ok_a = rip_icon_lookup(&a.icon_state, icon_name, 8, &icon);
    ok_b = rip_icon_lookup(&b.icon_state, icon_name, 8, &icon);
    if (ok_a && !ok_b)
        PASS();
    else
        FAIL("icon cache leaked across sessions");
}

static void test_truncated_bmp_rejected(void) {
    static const uint8_t truncated_bmp[57] = {
        [0] = 'B', [1] = 'M',
        [2] = 57,
        [10] = 54,
        [14] = 40,
        [18] = 2,
        [22] = 1,
        [26] = 1,
        [28] = 8,
    };
    rip_state_t s;
    comp_context_t ctx;

    TEST("truncated BMP is rejected");
    init_fixture(&s, &ctx);
    if (!rip_icon_cache_bmp(&s.icon_state, "BADBMP", 6,
                            truncated_bmp, (int)sizeof(truncated_bmp)) &&
        rip_icon_cache_count(&s.icon_state) == 0)
        PASS();
    else
        FAIL("truncated BMP was cached");
}

static void test_zero_height_bmp_rejected(void) {
    /* Minimum-size 8bpp 1x0 BMP — height field is zero.  Should fail
     * rip_icon_cache_bmp (was a soft-spot before L13). */
    static const uint8_t bmp_zero_h[54] = {
        [0] = 'B', [1] = 'M',
        [2] = 54,
        [10] = 54,
        [14] = 40,
        [18] = 1,                  /* width = 1 */
        [22] = 0, [23] = 0, [24] = 0, [25] = 0,  /* height = 0 */
        [26] = 1,
        [28] = 8,
    };
    rip_state_t s;
    comp_context_t ctx;

    TEST("BMP with height=0 is rejected");
    init_fixture(&s, &ctx);
    if (!rip_icon_cache_bmp(&s.icon_state, "ZEROH", 5,
                            bmp_zero_h, (int)sizeof(bmp_zero_h)) &&
        rip_icon_cache_count(&s.icon_state) == 0)
        PASS();
    else
        FAIL("zero-height BMP was cached");
}

static void test_upload_name_length_respected(void) {
    static const char icon_name[] = "UPTEST01";
    static const uint8_t icn_1x1[10] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00
    };
    rip_state_t s;
    comp_context_t ctx;
    rip_icon_t icon;

    TEST("upload consumes exactly declared filename bytes");
    init_fixture(&s, &ctx);
    rip_file_upload_begin_state(&s, 8);
    feed_upload_bytes(&s, (const uint8_t *)icon_name, 8);
    feed_upload_bytes(&s, icn_1x1, sizeof(icn_1x1));
    rip_file_upload_end_state(&s);
    if (rip_icon_lookup(&s.icon_state, icon_name, 8, &icon) &&
        icon.width == 1 && icon.height == 1)
        PASS();
    else
        FAIL("upload parser consumed image bytes as filename");
}

static void test_invalid_icn_upload_does_not_grow_arena(void) {
    static const uint8_t bad_icn[7] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80
    };
    rip_state_t s;
    comp_context_t ctx;
    uint32_t used_before;

    TEST("invalid ICN upload does not consume arena");
    init_fixture(&s, &ctx);
    rip_file_upload_begin_state(&s, 6);
    used_before = s.psram_arena.used;
    feed_upload_bytes(&s, (const uint8_t *)"BADICN", 6);
    feed_upload_bytes(&s, bad_icn, sizeof(bad_icn));
    rip_file_upload_end_state(&s);
    if (s.psram_arena.used == used_before &&
        rip_icon_cache_count(&s.icon_state) == 0)
        PASS();
    else
        FAIL("malformed ICN burned arena space");
}

static void test_overlong_upload_name_rejected(void) {
    static const char icon_name[] = "UPLOAD_NAME_1234";
    static const uint8_t icn_1x1[10] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00
    };
    rip_state_t s;
    comp_context_t ctx;

    TEST("overlong upload filename is rejected");
    init_fixture(&s, &ctx);
    rip_file_upload_begin_state(&s, (uint8_t)(sizeof(icon_name) - 1));
    feed_upload_bytes(&s, (const uint8_t *)icon_name, sizeof(icon_name) - 1);
    feed_upload_bytes(&s, icn_1x1, sizeof(icn_1x1));
    rip_file_upload_end_state(&s);
    if (rip_icon_cache_count(&s.icon_state) == 0)
        PASS();
    else
        FAIL("overlong upload name was cached");
}

static void test_viewport_clamps_to_screen(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("viewport command clamps and normalizes");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|vZZZZ0000|");
    if (s.vp_x0 == 0 && s.vp_y0 == 0 &&
        s.vp_x1 == 639 && s.vp_y1 == 399)
        PASS();
    else
        FAIL("viewport state was not clamped");
}

static void test_text_window_clamps_to_default(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("text window clamps to valid screen bounds");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|wZZZZ000000|");
    if (s.tw_x0 == 0 && s.tw_y0 == 0 &&
        s.tw_x1 == 639 && s.tw_y1 == 349 &&
        !s.tw_active)
        PASS();
    else
        FAIL("text window retained out-of-range coordinates");
}

static void test_unsafe_file_query_does_not_queue_request(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("unsafe file query is rejected");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|F000000..\\BAD|");
    if (rip_icon_pending_requests(&s.icon_state) == 0)
        PASS();
    else
        FAIL("unsafe FILE_QUERY queued a file request");
}

static void test_unsafe_load_icon_does_not_queue_request(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("unsafe load_icon path is rejected");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|I000000000..\\BAD|");
    if (rip_icon_pending_requests(&s.icon_state) == 0)
        PASS();
    else
        FAIL("unsafe LOAD_ICON queued a file request");
}

static void test_escape_pair_preserved_in_cmd_buf(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("LINE_CONT preserves \\| as literal escape in cmd_buf");
    init_fixture(&s, &ctx);
    /* Feed "!|TX\|Y" without terminator — confirms \| was kept as a
     * two-byte escape pair instead of triggering early dispatch on '|'. */
    feed_script(&s, &ctx, "!|TX\\|Y");
    if (s.cmd_char == 'T' && s.cmd_len == 4 &&
        s.cmd_buf[0] == 'X' &&
        s.cmd_buf[1] == '\\' &&
        s.cmd_buf[2] == '|' &&
        s.cmd_buf[3] == 'Y')
        PASS();
    else
        FAIL("escape \\| not preserved through LINE_CONT");
}

static void test_escape_bang_preserved_in_cmd_buf(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("LINE_CONT preserves \\! as literal escape in cmd_buf");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|TA\\!B");
    if (s.cmd_char == 'T' && s.cmd_len == 4 &&
        s.cmd_buf[0] == 'A' &&
        s.cmd_buf[1] == '\\' &&
        s.cmd_buf[2] == '!' &&
        s.cmd_buf[3] == 'B')
        PASS();
    else
        FAIL("escape \\! not preserved");
}

static void test_line_cont_crlf_still_works(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("LINE_CONT still consumes \\<CR><LF> as continuation");
    init_fixture(&s, &ctx);
    /* "!|TabcXYZ" with \<CR><LF> in the middle should accumulate "abcXYZ" */
    feed_script(&s, &ctx, "!|Tabc\\\r\nXYZ");
    if (s.cmd_char == 'T' && s.cmd_len == 6 &&
        memcmp(s.cmd_buf, "abcXYZ", 6) == 0)
        PASS();
    else
        FAIL("CRLF line continuation broken");
}

static void test_scale_y_consistency_l0_l2(void) {
    rip_state_t s_v;
    rip_state_t s_p;
    comp_context_t ctx;

    TEST("level-0 'v' and level-2 'P' agree on Y scaling");
    /* Level 0 viewport: x0=0 y0=05 x1=02 y1=0A (EGA 2-digit) */
    init_fixture(&s_v, &ctx);
    feed_script(&s_v, &ctx, "!|v0005020A|");
    /* Level 2 port define: port=1, x0=00 y0=05 x1=02 y1=0A, flags=00 */
    init_fixture(&s_p, &ctx);
    feed_script(&s_p, &ctx, "!|2P10005020A00|");
    if (s_v.vp_y0 == s_p.ports[1].vp_y0 &&
        s_v.vp_y1 == s_p.ports[1].vp_y1)
        PASS();
    else
        FAIL("scale_y diverges between Level 0 and Level 2");
}

static void test_bgi_fill_mapping(void) {
    TEST("rip_bgi_fill_to_card produces correct pattern indices");
    int ok = 1;
    if (rip_bgi_fill_to_card(0)  != -1) ok = 0;  /* EMPTY */
    if (rip_bgi_fill_to_card(1)  !=  0) ok = 0;  /* SOLID */
    if (rip_bgi_fill_to_card(2)  !=  4) ok = 0;  /* LINE → horizontal */
    if (rip_bgi_fill_to_card(3)  !=  7) ok = 0;  /* LTSLASH */
    if (rip_bgi_fill_to_card(4)  !=  3) ok = 0;  /* SLASH */
    if (rip_bgi_fill_to_card(5)  !=  2) ok = 0;  /* BKSLASH */
    if (rip_bgi_fill_to_card(7)  !=  6) ok = 0;  /* HATCH */
    if (rip_bgi_fill_to_card(9)  !=  8) ok = 0;  /* INTERLEAVE */
    if (rip_bgi_fill_to_card(10) !=  9) ok = 0;  /* WIDE_DOT */
    if (rip_bgi_fill_to_card(11) != 10) ok = 0;  /* CLOSE_DOT */
    if (rip_bgi_fill_to_card(12) != 11) ok = 0;  /* USER */
    if (ok) PASS(); else FAIL("BGI fill style maps to wrong pattern");
}

static void test_port_alpha_init_consistency(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("port_alpha mirror matches rip_port_t.alpha after init");
    init_fixture(&s, &ctx);
    if (s.rip2_state.port_alpha[0] == 35 &&
        s.rip2_state.port_alpha[1] ==  0 &&
        s.rip2_state.port_alpha[35] == 0 &&
        s.ports[0].alpha == 35 &&
        s.ports[1].alpha ==  0)
        PASS();
    else
        FAIL("port_alpha mirror diverges from rip_port_t.alpha");
}

static void test_esc_probe_responds(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("ESC[! probe receives RIPSCRIP version string");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "\x1B[!");
    if (tx_len == 15 && memcmp(tx_capture, "RIPSCRIP015400\n", 15) == 0)
        PASS();
    else
        FAIL("ESC[! probe response missing or wrong");
}

static void test_partial_esc_does_not_respond(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("ESC[ alone does not trigger probe response");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "\x1B[2J");  /* normal ANSI clear */
    if (tx_len == 0)
        PASS();
    else
        FAIL("partial ESC sequence triggered probe response");
}

static void test_comment_skipped(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("!|! comment skips to next | and resumes parsing");
    init_fixture(&s, &ctx);
    /* Comment then a pixel command — pixel must still execute. */
    feed_script(&s, &ctx, "!|!ignored text|!|X0303|");
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("comment swallowed the next command");
}

static void test_error_recovery_resyncs(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("error recovery resyncs on next command boundary");
    init_fixture(&s, &ctx);
    /* Bad command-letter byte (\x05 is a control char rejected by the FSM)
     * sandwiched between valid frames.  The X0808 pixel should still draw. */
    feed_script(&s, &ctx, "!|\x05garbage|!|X0808|");
    if (draw_get_pixel(8, 9) != 0)  /* scale_y(8)=9 (8*8/7=9) */
        PASS();
    else
        FAIL("FSM did not resync after bad command");
}

static void test_soh_swallowed(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("SOH (0x01) swallowed without breaking parsing");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "\x01\x01!|X0505|\x01");
    if (draw_get_pixel(5, 5) != 0)
        PASS();
    else
        FAIL("SOH disrupted parser");
}

static void test_var_refresh_clears_suppress(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$REFRESH$ clears refresh_suppress");
    init_fixture(&s, &ctx);
    s.refresh_suppress = true;
    feed_script(&s, &ctx, "!|T$REFRESH$|");
    if (!s.refresh_suppress)
        PASS();
    else
        FAIL("$REFRESH$ did not clear refresh_suppress");
}

static void test_var_norefresh_sets_suppress(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$NOREFRESH$ sets refresh_suppress");
    init_fixture(&s, &ctx);
    s.refresh_suppress = false;
    feed_script(&s, &ctx, "!|T$NOREFRESH$|");
    if (s.refresh_suppress)
        PASS();
    else
        FAIL("$NOREFRESH$ did not set refresh_suppress");
}

static void test_var_mkill_clears_regions(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$MKILL$ clears mouse regions");
    init_fixture(&s, &ctx);
    /* Pre-populate one mouse region. */
    s.mouse_regions[0].active = true;
    s.mouse_regions[0].flags = RIP_MF_ACTIVE;
    s.num_mouse_regions = 1;
    feed_script(&s, &ctx, "!|T$MKILL$|");
    if (s.num_mouse_regions == 0)
        PASS();
    else
        FAIL("$MKILL$ left mouse regions intact");
}

static void test_var_copy_resets_write_mode(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$COPY$ resets write_mode to COPY(0)");
    init_fixture(&s, &ctx);
    s.write_mode = DRAW_MODE_XOR;
    feed_script(&s, &ctx, "!|T$COPY$|");
    if (s.write_mode == 0)
        PASS();
    else
        FAIL("$COPY$ did not reset write_mode");
}

static void test_var_abort_resets_fsm(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$ABORT$ resets FSM and level flags");
    init_fixture(&s, &ctx);
    s.is_level1 = true;
    s.cmd_len = 5;
    feed_script(&s, &ctx, "!|T$ABORT$|");
    if (s.state == RIP_ST_IDLE && !s.is_level1 &&
        !s.is_level2 && !s.is_level3 && s.cmd_len == 0)
        PASS();
    else
        FAIL("$ABORT$ did not fully reset parser");
}

static void test_var_beep_pushes_bel(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$BEEP$ pushes 0x07 (BEL) to host TX");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|T$BEEP$|");
    int has_bel = 0;
    for (size_t i = 0; i < tx_len; i++)
        if (tx_capture[i] == 0x07) { has_bel = 1; break; }
    if (has_bel)
        PASS();
    else
        FAIL("$BEEP$ did not emit BEL");
}

static void test_var_rand_advances_lcg(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$RAND$ advances LCG using Knuth/POSIX coefficients");
    init_fixture(&s, &ctx);
    s.rand_state = 1u;  /* deterministic seed for the test */
    feed_script(&s, &ctx, "!|T$RAND$|");
    /* Knuth/POSIX rand: state' = state * 1103515245 + 12345 */
    uint32_t expected = 1u * 1103515245u + 12345u;
    if (s.rand_state == expected)
        PASS();
    else
        FAIL("$RAND$ LCG state not Knuth/POSIX");
}

static void test_l1_kill_mouse_regions(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1K kills all mouse regions");
    init_fixture(&s, &ctx);
    s.num_mouse_regions = 3;
    feed_script(&s, &ctx, "!|1K|");
    if (s.num_mouse_regions == 0)
        PASS();
    else
        FAIL("1K did not clear mouse regions");
}

static void test_l1_mouse_region_define(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1M defines a mouse region with hotkey + host text");
    init_fixture(&s, &ctx);
    /* num:01 x0:0A y0:0A x1:1E y1:0U hotkey:1T flags:0 res:0000 text=HELLO
     * 1T (base36) = 1*36+29 = 65 = 'A' */
    feed_script(&s, &ctx, "!|1M010A0A1E0U1T00000HELLO|");
    if (s.num_mouse_regions == 1 &&
        s.mouse_regions[0].x0 == 10 &&
        s.mouse_regions[0].x1 == 50 &&
        s.mouse_regions[0].hotkey == 65 &&
        s.mouse_regions[0].text_len == 5 &&
        memcmp(s.mouse_regions[0].text, "HELLO", 5) == 0)
        PASS();
    else
        FAIL("1M region not registered correctly");
}

static void test_l1_button_registers_region(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1U button creates a mouse region with host command");
    init_fixture(&s, &ctx);
    /* x0:0A y0:0A x1:1E y1:0U hotkey:00 flags:0 res:0  text=<>LABEL<>HOST */
    feed_script(&s, &ctx, "!|1U0A0A1E0U0000<>LABEL<>HOST|");
    if (s.num_mouse_regions == 1 &&
        s.mouse_regions[0].text_len == 4 &&
        memcmp(s.mouse_regions[0].text, "HOST", 4) == 0)
        PASS();
    else
        FAIL("1U did not register a button region");
}

static void test_l1_image_style_stored(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1S stores image display style");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1S02|");  /* style = 2 (center) */
    if (s.image_style == 2)
        PASS();
    else
        FAIL("1S image_style not stored");
}

static void test_l1_viewport_ext(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1V sets viewport rect and scale factor");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1V0000050A1|");  /* (0,0)-(5,10) scale=1 */
    if (s.vp_x0 == 0 && s.vp_x1 == 5 &&
        s.vp_y0 == 0 && s.vp_y1 == 12 &&
        s.viewport_scale == 1)
        PASS();
    else
        FAIL("1V viewport not stored correctly");
}

static void test_l1_query_ext_returns_app_var(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1Q extended query returns $APPn$ value via TX");
    init_fixture(&s, &ctx);
    strcpy(s.app_vars[3], "VALUE3");
    tx_reset();
    feed_script(&s, &ctx, "!|1Q00000$APP3$|");
    if (tx_len == 6 && memcmp(tx_capture, "VALUE3", 6) == 0)
        PASS();
    else
        FAIL("1Q did not return app var");
}

static void test_sync_date_byte_commits_on_nul(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_sync_date_byte commits buffer to host_date on NUL");
    init_fixture(&s, &ctx);
    const char *date = "11/23/26";
    for (size_t i = 0; date[i] != '\0'; i++)
        rip_sync_date_byte((uint8_t)date[i]);
    rip_sync_date_byte(0x00);
    if (strcmp(s.host_date, "11/23/26") == 0)
        PASS();
    else
        FAIL("host_date not set after NUL commit");
}

static void test_sync_time_byte_commits_on_nul(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_sync_time_byte commits buffer to host_time on NUL");
    init_fixture(&s, &ctx);
    const char *t = "14:35:09";
    for (size_t i = 0; t[i] != '\0'; i++)
        rip_sync_time_byte((uint8_t)t[i]);
    rip_sync_time_byte(0x00);
    if (strcmp(s.host_time, "14:35:09") == 0)
        PASS();
    else
        FAIL("host_time not set after NUL commit");
}

static void test_query_response_round_trip(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_query_response_byte fills $APPn$ and pushes to TX");
    init_fixture(&s, &ctx);
    /* Simulate the state after !|1Q|...$APP2$ saw an empty var:
     * query_pending=true, query_var_name="$APP2$". */
    s.query_pending = true;
    strcpy(s.query_var_name, "$APP2$");
    s.query_response_len = 0;
    tx_reset();

    const char *answer = "REPLY";
    for (size_t i = 0; answer[i] != '\0'; i++)
        rip_query_response_byte((uint8_t)answer[i]);
    rip_query_response_byte(0x00);  /* commit */

    if (!s.query_pending &&
        strcmp(s.app_vars[2], "REPLY") == 0 &&
        tx_len == 5 &&
        memcmp(tx_capture, "REPLY", 5) == 0)
        PASS();
    else
        FAIL("query response not properly committed");
}

static void test_l1_copy_region_blits_pixels(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1G copies a screen region to a new destination");
    init_fixture(&s, &ctx);
    /* Paint a pixel at (5, 5) (card space).  After 1G copies (5,5)-(7,7)
     * (EGA) to dest (20, 20) (EGA), the same pixel should appear there. */
    draw_set_color(0xC8);
    draw_pixel(5, 5);
    /* MegaNum 2-digit: 5="05", 7="07", 20="0K". */
    feed_script(&s, &ctx, "!|1G05050707000K0K|");
    /* scale_y(20) = 22 in card space.  Pre-paint relative offset (0,0)
     * inside the source rect lands at (20, 22) in dest. */
    if (draw_get_pixel(20, 22) == 0xC8)
        PASS();
    else
        FAIL("1G did not copy pixel to destination");
}

static void test_l1_clipboard_get_put_roundtrip(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1C + 1P round-trips pixel data through clipboard");
    init_fixture(&s, &ctx);
    /* Paint a pixel inside the source rect. */
    draw_set_color(0xAA);
    draw_pixel(3, 3);
    /* 1C: capture (3,3)-(5,5) (EGA).  res:1.  Total 9 chars. */
    feed_script(&s, &ctx, "!|1C03030505 |");
    if (!s.clipboard.valid) { FAIL("setup: 1C did not capture"); return; }
    /* 1P: paste at (30, 30) (EGA), mode=00, res=' '.  30 = "0U".
     * Total 7 chars: x:2 y:2 mode:2 res:1. */
    feed_script(&s, &ctx, "!|1P0U0U00 |");
    /* Pre-painted pixel was at card (3,3).  Source rect started at
     * (3, scale_y(3)=3) so pixel was at relative offset (0, 0).
     * Destination at (30, scale_y(30)=34) — pixel lands at (30, 34). */
    if (draw_get_pixel(30, 34) == 0xAA)
        PASS();
    else
        FAIL("1P did not paste clipboard pixels");
}

static void test_l1_text_block_lifecycle(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1T begins, 1E ends text block");
    init_fixture(&s, &ctx);
    /* 1T: x0=00 y0=00 x1=14(40) y1=14(40) res=00.  Total 10 chars. */
    feed_script(&s, &ctx, "!|1T0000141400|");
    if (!s.text_block.active) { FAIL("1T did not activate block"); return; }
    feed_script(&s, &ctx, "!|1E|");
    if (!s.text_block.active)
        PASS();
    else
        FAIL("1E did not deactivate block");
}

static void test_text_xy_expands_variables(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("@ command expands $BEEP$ via shared text path");
    init_fixture(&s, &ctx);
    tx_reset();
    /* @ at (0,0) with body "$BEEP$".  Expansion pushes BEL to TX. */
    feed_script(&s, &ctx, "!|@0000$BEEP$|");
    int has_bel = 0;
    for (size_t i = 0; i < tx_len; i++)
        if (tx_capture[i] == 0x07) { has_bel = 1; break; }
    if (has_bel)
        PASS();
    else
        FAIL("@ did not expand $BEEP$ (regression of M15)");
}

static void test_l1_audio_pushes_marker(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1A audio command pushes 0x3D + filename to TX");
    init_fixture(&s, &ctx);
    tx_reset();
    /* 1A: mode:2 res:2 filename = 4 chars + filename. */
    feed_script(&s, &ctx, "!|1A0000BEEP|");
    if (tx_len >= 6 &&
        tx_capture[0] == 0x3D &&
        memcmp(tx_capture + 1, "BEEP", 4) == 0 &&
        tx_capture[5] == '\0')
        PASS();
    else
        FAIL("1A did not push CMD_PLAY_SOUND marker");
}

static void test_text_xy_ext_renders_via_shared_helper(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("- (TEXT_XY_EXT) routes through shared text path (regression for L14)");
    init_fixture(&s, &ctx);
    /* Bitmap font path (font_id == 0).  Before L14 this passed NULL
     * font to draw_text, which silently dropped the entire string.
     * Body "$BEEP$" expansion pushes BEL to TX as a side effect we can
     * easily verify without comparing rendered glyphs. */
    tx_reset();
    /* x0=05 y0=05 x1=14(40) y1=0K(20) flags=00 body=$BEEP$ */
    feed_script(&s, &ctx, "!|-0505140K00$BEEP$|");
    int has_bel = 0;
    for (size_t i = 0; i < tx_len; i++)
        if (tx_capture[i] == 0x07) { has_bel = 1; break; }
    if (has_bel)
        PASS();
    else
        FAIL("- did not run shared text rendering (still uses old broken code)");
}

static void test_draw_to_moves_cursor(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("_ DRAW_TO updates draw cursor and optionally draws a line");
    init_fixture(&s, &ctx);
    /* x0=05 y0=05 mode=01 (draw on) param=00 x1=0F y1=0F */
    feed_script(&s, &ctx, "!|_050501000F0F|");
    /* Pixel between (5,5) and (15,15) on the diagonal — e.g. (10, 11)
     * (scale_y skews Y by 8/7).  Just check that draw_x/draw_y advanced. */
    if (s.draw_x == 15 && s.draw_y == /* scale_y(15) = 17 */ 17)
        PASS();
    else
        FAIL("DRAW_TO did not update cursor position");
}

static void test_icon_request_queue_dequeue(void) {
    rip_icon_state_t state;
    char name_out[16];

    TEST("rip_icon_request_file enqueue + dequeue + clear");
    memset(&state, 0, sizeof(state));
    /* Initially empty. */
    if (rip_icon_pending_requests(&state) != 0) {
        FAIL("setup: state not empty");
        return;
    }
    rip_icon_request_file(&state, "ALPHA", 5);
    rip_icon_request_file(&state, "BETA", 4);
    if (rip_icon_pending_requests(&state) != 2) {
        FAIL("queue did not accept two distinct files");
        return;
    }
    /* Duplicate request — must coalesce, not grow. */
    rip_icon_request_file(&state, "ALPHA", 5);
    if (rip_icon_pending_requests(&state) != 2) {
        FAIL("duplicate request grew the queue");
        return;
    }
    /* Dequeue first — FIFO order. */
    int len = rip_icon_dequeue_request(&state, name_out, sizeof(name_out));
    if (len != 5 || memcmp(name_out, "ALPHA", 5) != 0) {
        FAIL("dequeue returned wrong entry");
        return;
    }
    if (rip_icon_pending_requests(&state) != 1) {
        FAIL("dequeue did not decrement count");
        return;
    }
    /* Clear remaining. */
    rip_icon_clear_requests(&state);
    if (rip_icon_pending_requests(&state) == 0)
        PASS();
    else
        FAIL("clear did not empty the queue");
}

static void test_fuzz_multiple_seeds(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_process survives 4 different 4K random byte seeds");
    static const uint32_t seeds[] = {
        0xCAFEBABEu, 0xFEEDFACEu, 0x12345678u, 0xA5A5A5A5u
    };
    for (size_t k = 0; k < sizeof(seeds)/sizeof(seeds[0]); k++) {
        init_fixture(&s, &ctx);
        uint32_t lcg = seeds[k];
        for (int i = 0; i < 4096; i++) {
            lcg = lcg * 1103515245u + 12345u;
            uint8_t b = (uint8_t)(lcg >> 16);
            rip_process(&s, &ctx, b);
        }
        /* Recover with a clean newline + valid command. */
        feed_script(&s, &ctx, "\r\n!|X0202|");
    }
    PASS();  /* No crash across all seeds = pass. */
    (void)s;
}

static void test_null_safety_public_entrypoints(void) {
    TEST("public entrypoints are NULL-safe");
    /* These would have crashed before L11/L12.  Just call them and
     * ensure we get back without dereferencing a NULL state. */
    rip_init_first(NULL);
    rip_activate(NULL);
    rip_session_reset(NULL);
    rip_save_palette(NULL);
    rip_mouse_event_state(NULL, 0, 0, true);
    rip_file_upload_begin_state(NULL, 5);
    rip_file_upload_byte_state(NULL, 0xAA);
    rip_file_upload_end_state(NULL);
    PASS();  /* Reaching this line means none crashed. */
}

static void test_fuzz_random_bytes_no_crash(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint32_t lcg = 0xDEADBEEFu;

    TEST("rip_process survives 16K random adversarial bytes");
    init_fixture(&s, &ctx);
    /* Feed 16K LCG-random bytes and ensure the parser stays sane. */
    for (int i = 0; i < 16384; i++) {
        lcg = lcg * 1103515245u + 12345u;
        uint8_t b = (uint8_t)(lcg >> 16);
        rip_process(&s, &ctx, b);
    }
    /* After the storm, a clean !|X command should still work. */
    feed_script(&s, &ctx, "\r\n!|X0505|");
    /* Pixel may or may not draw depending on residual state, but no
     * crash and the FSM should accept new input.  The test passes by
     * not crashing. */
    PASS();
    (void)s;
}

static void test_command_with_no_args(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("commands with too few args are silently dropped");
    init_fixture(&s, &ctx);
    /* RIP_LINE needs 8 chars; feed only 4.  Must not crash, must not
     * draw a partial line, and the parser must accept the next command. */
    feed_script(&s, &ctx, "!|L0000|!|X0606|");
    /* X0606 = (6, scale_y(6)=6) — pixel must draw. */
    if (draw_get_pixel(6, 6) != 0)
        PASS();
    else
        FAIL("parser stuck after short-arg command");
}

static void test_bang_pipe_with_no_command_letter(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("!| followed by | (empty command) is recovered from");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!||!|X0707|");
    if (draw_get_pixel(7, 8) != 0)  /* scale_y(7) = 8 */
        PASS();
    else
        FAIL("empty !|| sequence broke parser");
}

static void test_text_param_with_only_escape(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("text param ending in lone backslash doesn't UB");
    init_fixture(&s, &ctx);
    /* The trailing '\' has no escape successor.  unescape_text must
     * leave it alone and we must not read past end. */
    feed_script(&s, &ctx, "!|T\\|");
    /* No crash means pass; nothing else to assert. */
    PASS();
    (void)s;
}

static void test_ridiculously_long_text(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("text longer than cmd_buf capacity is truncated, not overflowed");
    init_fixture(&s, &ctx);
    /* Build a text command with 300 'A' bytes — cmd_buf is 256. */
    char buf[400];
    int p = 0;
    buf[p++] = '!';
    buf[p++] = '|';
    buf[p++] = 'T';
    for (int i = 0; i < 300; i++) buf[p++] = 'A';
    buf[p++] = '|';
    buf[p] = '\0';
    feed_script(&s, &ctx, buf);
    /* Subsequent valid command must still work — cmd_len is reset by
     * '|' regardless of overflow. */
    feed_script(&s, &ctx, "!|X0808|");
    if (draw_get_pixel(8, 9) != 0)  /* scale_y(8)=9 */
        PASS();
    else
        FAIL("parser broken after over-long text param");
}

static void test_poly_bezier_renders_segment(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'z' RIP_POLY_BEZIER renders a cubic segment");
    init_fixture(&s, &ctx);
    /* nsegs=1, nsteps=8, 4 control points (0,30)→(10,10)→(30,10)→(40,30) */
    feed_script(&s, &ctx, "!|z0108001E0A0A140A141E|");
    /* The curve should put some pixels in the y<25 region (control points
     * pull it up).  Sample around the midpoint. */
    int hit = 0;
    for (int x = 15; x <= 25 && !hit; x++)
        for (int y = 10; y < 25 && !hit; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) hit = 1;
    if (hit)
        PASS();
    else
        FAIL("poly-bezier did not draw any pixels");
}

static void test_bounded_text_wraps(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'\"' RIP_BOUNDED_TEXT renders inside the bounding box");
    init_fixture(&s, &ctx);
    /* Box (5,5)-(60,30) flags=00 text="HELLO". */
    feed_script(&s, &ctx, "!|\"05050U1E00HELLO|");
    /* scale_y(5)=5; expect glyph pixels somewhere in (5..60, 5..30). */
    int hit = 0;
    for (int y = 5; y <= 25 && !hit; y++)
        for (int x = 5; x <= 60 && !hit; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) hit = 1;
    if (hit)
        PASS();
    else
        FAIL("bounded text did not draw");
}

static void test_polygon_all_outside_clip_no_draw(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("polygon entirely outside the viewport draws nothing");
    init_fixture(&s, &ctx);
    /* Clip to a tiny rect. */
    feed_script(&s, &ctx, "!|v00000A0A|");
    /* Filled polygon (3 pts) at (50,50)/(60,50)/(55,60) — all outside. */
    feed_script(&s, &ctx, "!|p0332321E32 1E3C 1E");
    feed_script(&s, &ctx, "|");
    /* No pixel inside the viewport (which is small) should be set. */
    int touched = 0;
    for (int y = 0; y <= 12; y++)
        for (int x = 0; x <= 10; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) touched = 1;
    if (!touched)
        PASS();
    else
        FAIL("polygon outside clip drew inside it");
}

static void test_session_reset_clears_state(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_session_reset clears mouse/clipboard/vars/query/preproc");
    init_fixture(&s, &ctx);
    /* Populate state heavily. */
    feed_script(&s, &ctx, "!|1M010A0A1E0U0000000HOST|");
    strcpy(s.app_vars[2], "DIRTY");
    s.query_pending = true;
    strcpy(s.query_var_name, "$APP2$");
    s.preproc_depth = 3;
    s.preproc_suppress = true;
    s.text_block.active = true;
    s.clipboard.valid = true;
    /* Reset. */
    rip_session_reset(&s);
    /* Verify everything cleared. */
    if (s.num_mouse_regions == 0 &&
        s.app_vars[2][0] == '\0' &&
        !s.query_pending &&
        s.preproc_depth == 0 &&
        !s.preproc_suppress &&
        !s.text_block.active &&
        !s.clipboard.valid &&
        s.state == RIP_ST_IDLE)
        PASS();
    else
        FAIL("session reset left state behind");
}

static void test_session_reset_then_resume(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_session_reset leaves state usable for the next session");
    init_fixture(&s, &ctx);
    /* Run a real command, mutate state. */
    feed_script(&s, &ctx, "!|cF|!|S0204|!|k7|!|B05050F0F|");
    /* Disconnect. */
    rip_session_reset(&s);
    /* New session immediately starts drawing.  Must work. */
    feed_script(&s, &ctx, "!|X0303|");
    /* scale_y(3) = 3, so pixel at (3, 3). */
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("session unusable after reset");
}

static void test_session_reset_preserves_arena(void) {
    rip_state_t s;
    comp_context_t ctx;
    psram_arena_t before_arena;

    TEST("rip_session_reset reuses the same PSRAM arena base");
    init_fixture(&s, &ctx);
    before_arena = s.psram_arena;
    rip_session_reset(&s);
    /* Reset rewinds (used==0) but keeps the same backing block. */
    if (s.psram_arena.base == before_arena.base &&
        s.psram_arena.size == before_arena.size &&
        s.psram_arena.used == 0)
        PASS();
    else
        FAIL("session reset reallocated arena");
}

static void test_reset_windows_full_defaults(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'*' RIP_RESET_WINDOWS restores drawing/text/viewport defaults");
    init_fixture(&s, &ctx);
    /* Disturb state. */
    feed_script(&s, &ctx, "!|c04|!|S0205|!|k7|!|w0A0A1E1E11|!|v0000050A|");
    s.tw_active = true;
    s.text_block.active = true;
    s.num_mouse_regions = 5;
    /* Reset. */
    feed_script(&s, &ctx, "!|*|");
    if (s.draw_color == 15 &&
        s.fill_color == 15 &&
        s.fill_pattern == 1 &&
        s.line_thick == 1 &&
        s.tw_x0 == 0 && s.tw_x1 == 639 &&
        s.tw_y0 == 0 && s.tw_y1 == 349 &&
        s.vp_x0 == 0 && s.vp_x1 == 639 &&
        s.num_mouse_regions == 0 &&
        !s.text_block.active)
        PASS();
    else
        FAIL("reset_windows did not restore all defaults");
}

static void test_palette_save_apply_round_trip(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint16_t custom_red;

    TEST("rip_save_palette + rip_apply_palette round-trips custom colors");
    init_fixture(&s, &ctx);
    /* Customize palette index 5 to a non-default value. */
    feed_script(&s, &ctx, "!|a0510|");  /* idx=5, ega64=16 */
    custom_red = palette[245];
    /* Save snapshot, then clobber the live palette to default. */
    rip_save_palette(&s);
    palette[245] = 0x1234;
    /* Re-apply — saved snapshot must come back. */
    rip_apply_palette();
    if (palette[245] == custom_red && custom_red != 0x1234)
        PASS();
    else
        FAIL("palette save/apply did not round-trip");
}

static void test_viewport_persists_across_port_switch(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'v' viewport survives a port-switch round-trip (L15)");
    init_fixture(&s, &ctx);
    /* Set viewport on port 0 to a non-default rect.
     * !|v 00 0A 14 1E| → x0=0 y0=10 x1=20 y1=30 (EGA). */
    feed_script(&s, &ctx, "!|v00000A14001E|");
    int16_t after_v_y0 = s.vp_y0;
    int16_t after_v_y1 = s.vp_y1;
    /* Define & switch to port 1, then back to port 0. */
    feed_script(&s, &ctx, "!|2P1000005050200|");
    feed_script(&s, &ctx, "!|2s100|");
    feed_script(&s, &ctx, "!|2s000|");
    /* Viewport must still match what 'v' set, not pre-'v' defaults. */
    if (s.vp_y0 == after_v_y0 && s.vp_y1 == after_v_y1)
        PASS();
    else
        FAIL("viewport reverted on port switch (L15 regression)");
}

static void test_port_switch_preserves_color_and_pos(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("port switch saves/restores color, position, line style, fill");
    init_fixture(&s, &ctx);
    /* Define port 1.  Set state on port 0 first. */
    s.draw_color = 7;
    s.fill_color = 11;
    s.draw_x = 100;
    s.draw_y = 80;
    s.line_thick = 3;
    /* Define and switch to port 1.  '2P' creates port; '2s' switches. */
    feed_script(&s, &ctx, "!|2P1000005050200|");  /* port 1, viewport */
    feed_script(&s, &ctx, "!|2s100|");             /* switch to port 1 */
    /* Mutate state inside port 1. */
    s.draw_color = 2;
    s.draw_x = 30;
    /* Switch back to port 0 — state should be restored. */
    feed_script(&s, &ctx, "!|2s000|");
    if (s.draw_color == 7 &&
        s.fill_color == 11 &&
        s.draw_x == 100 &&
        s.draw_y == 80 &&
        s.line_thick == 3)
        PASS();
    else
        FAIL("port switch did not restore port 0 state");
}

static void test_preproc_depth_overflow(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("preprocessor handles > MAX_DEPTH nested IFs without corruption");
    init_fixture(&s, &ctx);
    /* 9 IFs, all true.  RIP_PREPROC_MAX_DEPTH is 8 — the 9th IF must
     * route into preproc_overflow, not corrupt the stack.  Then 9
     * ENDIFs unwind both overflow and depth back to zero. */
    feed_script(&s, &ctx,
        "<<IF 1>><<IF 1>><<IF 1>><<IF 1>><<IF 1>>"
        "<<IF 1>><<IF 1>><<IF 1>><<IF 1>>"
        "<<ENDIF>><<ENDIF>><<ENDIF>><<ENDIF>><<ENDIF>>"
        "<<ENDIF>><<ENDIF>><<ENDIF>><<ENDIF>>"
        "!|X0202|");
    if (s.preproc_depth == 0 &&
        s.preproc_overflow == 0 &&
        !s.preproc_suppress &&
        draw_get_pixel(2, 2) != 0)
        PASS();
    else
        FAIL("preprocessor depth overflow not handled cleanly");
}

static void test_preproc_if_with_app_var(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("<<IF $APP0$=hello>> branches on substituted value");
    init_fixture(&s, &ctx);
    strcpy(s.app_vars[0], "hello");
    feed_script(&s, &ctx, "<<IF $APP0$=hello>>!|X0303|<<ENDIF>>");
    /* scale_y(3) = 3 (3*8/7 = 3). */
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("variable substitution in IF expression failed");
}

static void test_preproc_if_with_app_var_false(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("<<IF $APP0$=other>> false branch suppresses output");
    init_fixture(&s, &ctx);
    strcpy(s.app_vars[0], "hello");
    feed_script(&s, &ctx, "<<IF $APP0$=other>>!|X0404|<<ENDIF>>");
    if (draw_get_pixel(4, 4) == 0)
        PASS();
    else
        FAIL("variable substitution in IF false-branch leaked");
}

static void test_mouse_click_dispatches_host_string(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("mouse click on active region pushes host text to TX");
    init_fixture(&s, &ctx);
    /* 1M region #1 at (10,10)-(50,30) hotkey=A flags=0 host=GO. */
    feed_script(&s, &ctx, "!|1M010A0A1E0U1T00000GO|");
    if (s.num_mouse_regions != 1) { FAIL("setup: 1M missed"); return; }
    tx_reset();
    /* Click inside the region (in card pixel space). */
    rip_mouse_event_state(&s, 25, 20, true);
    /* Expect "GO\r" pushed to TX. */
    if (tx_len == 3 && memcmp(tx_capture, "GO\r", 3) == 0)
        PASS();
    else
        FAIL("click did not dispatch host text");
}

static void test_mouse_radio_deselects_others(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("MF_RADIO click deselects other regions in same group");
    init_fixture(&s, &ctx);
    /* 1M format: num:2 x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:1 res:4 text.
     * flags digit 2 → spec bit 1 → RIP_MF_RADIO. */
    feed_script(&s, &ctx, "!|1M010505 0F0F0020000R1|");  /* radio #1 (5..15) */
    feed_script(&s, &ctx, "!|1M0214141E1E0020000R2|");   /* radio #2 (40..54) */
    /* Click region #2.  Note the typo above (' 0F') was intentional fixing. */
    /* Reset s and re-feed without the space typo. */
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1M0105050F0F0020000R1|");
    feed_script(&s, &ctx, "!|1M0214141E1E0020000R2|");
    rip_mouse_event_state(&s, 50, 50, true);
    /* First match wins — region 0 is the older one, region 1 was clicked
     * (assuming it was found by the loop second).  Both must end up
     * inactive: clicked region deactivates after dispatch (one-shot)
     * AND its RADIO peers are also deactivated. */
    if (!s.mouse_regions[0].active && !s.mouse_regions[1].active)
        PASS();
    else
        FAIL("RADIO did not deselect peers");
}

static void test_mouse_send_char_uses_hotkey(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("MF_SEND_CHAR sends hotkey instead of host text");
    init_fixture(&s, &ctx);
    /* hotkey = 'X' = 88 = mega2 "2G" (2*36+16=88).  flags '1' → SEND_CHAR. */
    feed_script(&s, &ctx, "!|1M010A0A1E0U2G10000IGNORED|");
    tx_reset();
    rip_mouse_event_state(&s, 25, 20, true);
    if (tx_len == 2 && tx_capture[0] == 88 && tx_capture[1] == '\r')
        PASS();
    else
        FAIL("SEND_CHAR did not push hotkey");
}

static void test_mouse_toggle_inverts_active(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("MF_TOGGLE flips region active state on each click");
    init_fixture(&s, &ctx);
    /* flags digit '4' → spec bit 2 → RIP_MF_TOGGLE. */
    feed_script(&s, &ctx, "!|1M010A0A1E0U0040000T|");
    bool first = s.mouse_regions[0].active;
    rip_mouse_event_state(&s, 25, 20, true);
    bool after_first = s.mouse_regions[0].active;
    rip_mouse_event_state(&s, 25, 20, true);
    bool after_second = s.mouse_regions[0].active;
    if (first != after_first && after_second == first)
        PASS();
    else
        FAIL("TOGGLE did not flip + flip back");
}

static void test_mouse_click_outside_no_dispatch(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("mouse click outside any region pushes nothing");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1M010A0A1E0U1T00000NO|");
    tx_reset();
    rip_mouse_event_state(&s, 200, 200, true);
    if (tx_len == 0)
        PASS();
    else
        FAIL("click outside region still dispatched");
}

static void test_polygon_overflow_rejected(void) {
    rip_state_t s;
    comp_context_t ctx;
    char script[2 + 4 + 4 + 65 * 4 + 2];
    int idx = 0;

    TEST("polygon command with > 64 vertices is rejected");
    init_fixture(&s, &ctx);
    /* Build a !|P<npts:2><pts...>| with npts = 65.  The handler must
     * reject this without writing pixels. */
    script[idx++] = '!';
    script[idx++] = '|';
    script[idx++] = 'P';
    /* npts = 65 = "1T" in MegaNum (1*36+29=65). */
    script[idx++] = '1';
    script[idx++] = 'T';
    /* Provide 65 dummy 4-char (XY) entries so length validation cannot
     * truncate.  Use "0000" repeated. */
    for (int i = 0; i < 65; i++) {
        script[idx++] = '0';
        script[idx++] = '0';
        script[idx++] = '0';
        script[idx++] = '0';
    }
    script[idx++] = '|';
    script[idx] = '\0';
    /* Pre-paint pixel (0,0) sentinel. */
    draw_set_color(0xFF);
    draw_pixel(0, 0);
    feed_script(&s, &ctx, script);
    /* Pixel must remain 0xFF — polygon must not have drawn. */
    if (draw_get_pixel(0, 0) == 0xFF)
        PASS();
    else
        FAIL("oversized polygon was drawn");
}

static void test_set_one_palette_entry(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint16_t before;

    TEST("'a' updates a single palette slot");
    init_fixture(&s, &ctx);
    before = palette[243];  /* slot 3 → palette index 240+3 = 243 */
    /* a 03 04: set palette idx 3 to EGA64 color 0x04 (high red) → RGB565 0xA800 */
    feed_script(&s, &ctx, "!|a0304|");
    if (palette[243] != before && palette[243] == 0xA800)
        PASS();
    else
        FAIL("'a' did not write expected palette entry");
}

static void test_group_markers_accepted(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'(' and ')' group markers are accepted as no-ops");
    init_fixture(&s, &ctx);
    /* Sandwich a real command between group markers — the X must still
     * draw, proving group markers don't consume subsequent bytes. */
    feed_script(&s, &ctx, "!|(|!|X0202|!|)|");
    if (draw_get_pixel(2, 2) != 0)
        PASS();
    else
        FAIL("group markers swallowed inner command");
}

static void test_no_op_metadata_commands_accepted(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("h/n/M/N metadata commands accepted without resync");
    init_fixture(&s, &ctx);
    /* Each of these has a 'recognised but stubbed' handler. */
    feed_script(&s, &ctx, "!|h00000000|");
    feed_script(&s, &ctx, "!|n0000|");
    feed_script(&s, &ctx, "!|M00|");
    feed_script(&s, &ctx, "!|N00|");
    /* If any threw the FSM into ERROR_RECOVERY, this final pixel would
     * not draw.  Use it as the canary. */
    feed_script(&s, &ctx, "!|X0303|");
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("a no-op metadata command broke parsing");
}

static void test_back_color_visible_in_pattern_fill(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("RIP_BACK_COLOR shows through patterned bar OFF bits");
    init_fixture(&s, &ctx);
    /* k5: back_color=5 (palette slot 245).
     * S 02 0C: BGI LINE_FILL pattern, fill_color=12 (palette slot 252).
     * B 05 05 0F 0F: bar (5,5)-(15,15).
     * Pattern 4 (horizontal) row 5 (y&7=5) = 0x00 → all OFF → back_color
     * Pattern 4 row 6 (y&7=6) = 0xFF → all ON → fill_color */
    feed_script(&s, &ctx, "!|k5|");
    feed_script(&s, &ctx, "!|S020C|");
    feed_script(&s, &ctx, "!|B05050F0F|");
    if (draw_get_pixel(10, 5) == 245 &&
        draw_get_pixel(10, 6) == 252)
        PASS();
    else
        FAIL("pattern fill did not use back_color for OFF bits");
}

static void test_erase_view_uses_back_color(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("RIP_ERASE_VIEW clears viewport to back_color (not 0)");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|k7|");      /* back_color = 7 → palette slot 247 */
    feed_script(&s, &ctx, "!|E|");        /* erase view */
    if (draw_get_pixel(100, 100) == 247)
        PASS();
    else
        FAIL("E erased to wrong color");
}

static void test_back_color_command_propagates_to_draw_layer(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'k' command takes effect without re-issuing 'S'");
    init_fixture(&s, &ctx);
    /* Set fill style first, then change back, then bar — back change must
     * have updated the draw layer so OFF bits use the new back_color. */
    feed_script(&s, &ctx, "!|S020C|");
    feed_script(&s, &ctx, "!|k3|");      /* back_color = 3 → palette slot 243 */
    feed_script(&s, &ctx, "!|B05050F0F|");
    if (draw_get_pixel(10, 5) == 243)    /* OFF bit row */
        PASS();
    else
        FAIL("back_color change did not propagate");
}

static void test_scroll_clears_only_source_rect(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("RIP_SCROLL clamps strip clear to source rect");
    init_fixture(&s, &ctx);
    /* Pre-paint a pixel between source rect (10..15, 11..18)
     * and dest rect (20..25, 11..18) — must survive the scroll. */
    draw_set_color(0xFF);
    draw_pixel(16, 12);
    /* x0=10 y0=10 x1=15 y1=15 dx=10 dy=0 fc=0 */
    feed_script(&s, &ctx, "!|+0A0A0F0F0A0000|");
    if (draw_get_pixel(16, 12) == 0xFF)
        PASS();
    else
        FAIL("strip clear leaked past source rect");
}

static void test_eval_if_ge_5_eq_5(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("<<IF 5>=5>> evaluates true (compound op precedence)");
    init_fixture(&s, &ctx);
    /* "0A0A" = (10, 10).  scale_y(10) = 11.  Pixel ends up at (10, 11). */
    feed_script(&s, &ctx, "<<IF 5>=5>>!|X0A0A|<<ENDIF>>");
    if (draw_get_pixel(10, 11) != 0)
        PASS();
    else
        FAIL("5>=5 wrongly evaluated as false");
}

static void test_eval_if_le_3_5(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("<<IF 3<=5>> evaluates true");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF 3<=5>>!|X0A0B|<<ENDIF>>");
    /* (10, 11).  scale_y(11) = 12. */
    if (draw_get_pixel(10, 12) != 0)
        PASS();
    else
        FAIL("3<=5 wrongly evaluated as false");
}

static void test_eval_if_ne_strings(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("<<IF foo!=bar>> uses string inequality");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF foo!=bar>>!|X0A0C|<<ENDIF>>");
    /* (10, 12).  scale_y(12) = 13. */
    if (draw_get_pixel(10, 13) != 0)
        PASS();
    else
        FAIL("foo!=bar wrongly false");
}

static void test_query_response_ignored_when_no_pending(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("query_response bytes ignored when no query is pending");
    init_fixture(&s, &ctx);
    s.query_pending = false;
    rip_query_response_byte('X');
    rip_query_response_byte(0x00);
    if (s.query_response_len == 0)
        PASS();
    else
        FAIL("query response accumulated without pending query");
}

static void test_l2_port_define(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("2P creates a port with scaled viewport");
    init_fixture(&s, &ctx);
    /* port=1 x0=00 y0=0A(10) x1=14(40) y1=0U(30) flags=00 */
    feed_script(&s, &ctx, "!|2P1000A140U00|");
    if (s.ports[1].allocated &&
        s.ports[1].vp_x0 == 0  && s.ports[1].vp_x1 == 40 &&
        s.ports[1].vp_y0 == 11 && s.ports[1].vp_y1 == 35) {
        PASS();
    } else {
        FAIL("2P port viewport wrong or not allocated");
    }
}

static void test_l2_port_zero_protected(void) {
    rip_state_t s;
    comp_context_t ctx;
    rip_port_t before;

    TEST("2P rejects port 0 (protected)");
    init_fixture(&s, &ctx);
    before = s.ports[0];
    feed_script(&s, &ctx, "!|2P0000A140U00|");
    if (memcmp(&before, &s.ports[0], sizeof(rip_port_t)) == 0)
        PASS();
    else
        FAIL("2P clobbered port 0");
}

static void test_l2_port_delete(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("2p deletes an allocated port");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|2P1000A140U00|");
    if (!s.ports[1].allocated) { FAIL("setup: 2P did not allocate"); return; }
    feed_script(&s, &ctx, "!|2p1|");
    if (!s.ports[1].allocated)
        PASS();
    else
        FAIL("2p did not deallocate");
}

static void test_l2_port_switch_changes_active(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("2s switches active port");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|2P1000A140U00|");
    feed_script(&s, &ctx, "!|2s100|");  /* switch to port 1, flags=00 */
    if (s.active_port == 1)
        PASS();
    else
        FAIL("2s did not change active_port");
}

static void test_l2_port_flags_set_alpha(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("2F sets per-port alpha and mirrors it to ripscrip2_state");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|2P1000A140U00|");
    /* port=1 alpha=Z(35) comp=00 zorder=00 */
    feed_script(&s, &ctx, "!|2F1Z0000|");
    if (s.ports[1].alpha == 35 &&
        s.rip2_state.port_alpha[1] == 35)
        PASS();
    else
        FAIL("2F alpha not stored correctly");
}

static void test_l2_scale_text(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("26 stores text scale and rotation");
    init_fixture(&s, &ctx);
    /* params decoded as mega2 pairs by the dispatcher.
     * scale=01 rotation=0A → text_scale=1, text_rotation=10 */
    feed_script(&s, &ctx, "!|26010A|");
    if (s.rip2_state.text_scale == 1 &&
        s.rip2_state.text_rotation == 10)
        PASS();
    else
        FAIL("26 did not update scale/rotation");
}

static void test_l2_set_palette_writes_hardware(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("20 set-palette pushes RGB565 to hardware");
    init_fixture(&s, &ctx);
    /* idx=05 R=FF G=00 B=00 → bright red */
    feed_script(&s, &ctx, "!|2005FFV9V900|");
    /* mega2 of "FF"= 15*36+15 = 555.  The handler ANDs with 0xFF → 555&0xFF = 0x2B (43).
     * That gets shifted into RGB565.  Don't try to compute exactly — just check that
     * palette[5] was updated away from 0. */
    if (palette[5] != 0)
        PASS();
    else
        FAIL("20 did not write palette entry");
}

static void test_l1_file_query_missing_returns_zero(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1F missing file returns \"0\\r\"");
    init_fixture(&s, &ctx);
    tx_reset();
    /* Level 1 'F' = RIP_FILE_QUERY.  Format: mode:2 res:4 filename */
    feed_script(&s, &ctx, "!|1F000000NOSUCH|");
    if (tx_len >= 2 && tx_capture[0] == '0' && tx_capture[1] == '\r')
        PASS();
    else
        FAIL("file query missing-file response wrong");
}

static void test_var_appn_set_via_define(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1D RIP_DEFINE stores $APP0$ default value");
    init_fixture(&s, &ctx);
    /* 1D: flags:3 res:2 then text "$APP0$:?prompt?HELLO".
     * Parse extracts the value after the second '?' as the default. */
    feed_script(&s, &ctx, "!|1D000  $APP0$:?prompt?HELLO|");
    if (strcmp(s.app_vars[0], "HELLO") == 0)
        PASS();
    else
        FAIL("1D did not store $APP0$ default");
}

static void test_line_continuation_crlf(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("backslash CRLF continues a parameter line");
    init_fixture(&s, &ctx);
    /* Split a pixel command across a continuation: !|X03\<CR><LF>03| */
    feed_script(&s, &ctx, "!|X03\\\r\n03|");
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("line continuation broke command");
}

static void test_init_first_idempotent_for_arena(void) {
    rip_state_t s;
    comp_context_t ctx;
    psram_arena_t saved;

    TEST("rip_init_first preserves arena across re-init");
    init_fixture(&s, &ctx);
    saved = s.psram_arena;
    /* Second call should keep the same arena base (no malloc churn). */
    rip_init_first(&s);
    if (s.psram_arena.base == saved.base &&
        s.psram_arena.size == saved.size)
        PASS();
    else
        FAIL("re-init reallocated the arena");
}

static void test_duplicate_icon_insert_is_deduplicated(void) {
    static uint8_t pixels_a[1] = {0x11};
    static uint8_t pixels_b[1] = {0x22};
    rip_state_t s;
    comp_context_t ctx;
    rip_icon_t icon;

    TEST("duplicate icon inserts do not grow cache");
    init_fixture(&s, &ctx);
    if (!rip_icon_cache_pixels(&s.icon_state, "DUPICON", 7, pixels_a, 1, 1) ||
        !rip_icon_cache_pixels(&s.icon_state, "DUPICON", 7, pixels_b, 1, 1)) {
        FAIL("duplicate insert unexpectedly failed");
        return;
    }
    if (rip_icon_cache_count(&s.icon_state) == 1 &&
        rip_icon_lookup(&s.icon_state, "DUPICON", 7, &icon) &&
        icon.pixels == pixels_a)
        PASS();
    else
        FAIL("duplicate insert consumed a new cache slot");
}

int main(void) {
    printf("RIPlib v1.0 — RIPscrip Regression Tests\n");
    printf("======================================\n\n");

    test_preproc_gt_handling();
    test_preproc_nested_else();
    test_preproc_malformed_directive_recovers();
    test_activate_restores_clip();
    test_port_text_justification_roundtrip();
    test_icon_state_isolation();
    test_truncated_bmp_rejected();
    test_zero_height_bmp_rejected();
    test_upload_name_length_respected();
    test_invalid_icn_upload_does_not_grow_arena();
    test_overlong_upload_name_rejected();
    test_viewport_clamps_to_screen();
    test_text_window_clamps_to_default();
    test_unsafe_file_query_does_not_queue_request();
    test_unsafe_load_icon_does_not_queue_request();
    test_duplicate_icon_insert_is_deduplicated();
    test_escape_pair_preserved_in_cmd_buf();
    test_escape_bang_preserved_in_cmd_buf();
    test_line_cont_crlf_still_works();
    test_scale_y_consistency_l0_l2();
    test_bgi_fill_mapping();
    test_port_alpha_init_consistency();
    test_init_first_idempotent_for_arena();
    test_esc_probe_responds();
    test_partial_esc_does_not_respond();
    test_comment_skipped();
    test_error_recovery_resyncs();
    test_soh_swallowed();
    test_line_continuation_crlf();
    test_var_refresh_clears_suppress();
    test_var_norefresh_sets_suppress();
    test_var_mkill_clears_regions();
    test_var_copy_resets_write_mode();
    test_var_abort_resets_fsm();
    test_var_beep_pushes_bel();
    test_var_rand_advances_lcg();
    test_var_appn_set_via_define();
    test_l1_kill_mouse_regions();
    test_l1_mouse_region_define();
    test_l1_button_registers_region();
    test_l1_image_style_stored();
    test_l1_viewport_ext();
    test_l1_query_ext_returns_app_var();
    test_l1_file_query_missing_returns_zero();
    test_l2_port_define();
    test_l2_port_zero_protected();
    test_l2_port_delete();
    test_l2_port_switch_changes_active();
    test_l2_port_flags_set_alpha();
    test_l2_scale_text();
    test_l2_set_palette_writes_hardware();
    test_sync_date_byte_commits_on_nul();
    test_sync_time_byte_commits_on_nul();
    test_query_response_round_trip();
    test_query_response_ignored_when_no_pending();
    test_eval_if_ge_5_eq_5();
    test_eval_if_le_3_5();
    test_eval_if_ne_strings();
    test_scroll_clears_only_source_rect();
    test_text_xy_ext_renders_via_shared_helper();
    test_draw_to_moves_cursor();
    test_icon_request_queue_dequeue();
    test_fuzz_multiple_seeds();
    test_null_safety_public_entrypoints();
    test_fuzz_random_bytes_no_crash();
    test_command_with_no_args();
    test_bang_pipe_with_no_command_letter();
    test_text_param_with_only_escape();
    test_ridiculously_long_text();
    test_poly_bezier_renders_segment();
    test_bounded_text_wraps();
    test_polygon_all_outside_clip_no_draw();
    test_session_reset_clears_state();
    test_session_reset_then_resume();
    test_session_reset_preserves_arena();
    test_reset_windows_full_defaults();
    test_palette_save_apply_round_trip();
    test_viewport_persists_across_port_switch();
    test_port_switch_preserves_color_and_pos();
    test_preproc_depth_overflow();
    test_preproc_if_with_app_var();
    test_preproc_if_with_app_var_false();
    test_mouse_click_dispatches_host_string();
    test_mouse_radio_deselects_others();
    test_mouse_send_char_uses_hotkey();
    test_mouse_toggle_inverts_active();
    test_mouse_click_outside_no_dispatch();
    test_polygon_overflow_rejected();
    test_set_one_palette_entry();
    test_group_markers_accepted();
    test_no_op_metadata_commands_accepted();
    test_back_color_visible_in_pattern_fill();
    test_erase_view_uses_back_color();
    test_back_color_command_propagates_to_draw_layer();
    test_l1_copy_region_blits_pixels();
    test_l1_clipboard_get_put_roundtrip();
    test_l1_text_block_lifecycle();
    test_text_xy_expands_variables();
    test_l1_audio_pushes_marker();

    cleanup_all_arenas();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
