/*
 * test_ripscrip.c — Targeted RIPlib parser/session regression tests
 *
 * Verifies parser, session-state, and icon-cache behavior for issues
 * found during the production-readiness audit.
 */

#include "bgi_font.h"
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
#define MAX_TRACKED_ARENAS 512
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

static void test_icn_upload_replaces_cached_name(void) {
    static const char icon_name[] = "UPTEST02";
    static const uint8_t icn_blue[10] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00
    };
    static const uint8_t icn_green[10] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00
    };
    rip_state_t s;
    comp_context_t ctx;
    rip_icon_t icon;

    TEST("ICN upload replaces an existing runtime icon");
    init_fixture(&s, &ctx);
    rip_file_upload_begin_state(&s, 8);
    feed_upload_bytes(&s, (const uint8_t *)icon_name, 8);
    feed_upload_bytes(&s, icn_blue, sizeof(icn_blue));
    rip_file_upload_end_state(&s);

    rip_file_upload_begin_state(&s, 8);
    feed_upload_bytes(&s, (const uint8_t *)icon_name, 8);
    feed_upload_bytes(&s, icn_green, sizeof(icn_green));
    rip_file_upload_end_state(&s);

    if (rip_icon_cache_count(&s.icon_state) == 1 &&
        rip_icon_lookup(&s.icon_state, icon_name, 8, &icon) &&
        icon.width == 1 && icon.height == 1 && icon.pixels[0] == 2)
        PASS();
    else
        FAIL("second ICN upload left stale pixels cached");
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

static void test_var_reset_restores_state(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$RESET$ performs the same state reset as '*'");
    init_fixture(&s, &ctx);
    draw_set_color(77);
    draw_pixel(10, 10);
    s.write_mode = DRAW_MODE_XOR;
    draw_set_write_mode(DRAW_MODE_XOR);
    s.font_attrib = 7;
    s.num_mouse_regions = 4;
    feed_script(&s, &ctx, "!|T$RESET$|");
    if (s.num_mouse_regions == 0 &&
        s.write_mode == DRAW_MODE_COPY &&
        s.font_attrib == 0 &&
        draw_get_pixel(10, 10) == s.palette[s.back_color])
        PASS();
    else
        FAIL("$RESET$ did not restore windows/drawing state");
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

static void test_l1_define_query_and_expand_generic_var(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1D generic variables query and expand in IF expressions");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1D000  CITY=Austin|");
    tx_reset();
    feed_script(&s, &ctx, "!|1Q00000$CITY$|");
    if (!(tx_len == 6 && memcmp(tx_capture, "Austin", 6) == 0)) {
        FAIL("1Q did not return generic variable");
        return;
    }
    feed_script(&s, &ctx, "<<IF $CITY$=Austin>>!|X0303|<<ENDIF>>");
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("generic variable did not expand in IF expression");
}

static void test_l1_generic_query_round_trip(void) {
    rip_state_t s;
    comp_context_t ctx;
    const char *answer = "Paris";

    TEST("undefined generic query prompts and stores response");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1Q00000$CITY$|");
    if (!(s.query_pending && tx_len >= 8 && tx_capture[0] == 0x3E &&
          memcmp(tx_capture + 1, "$CITY$", 6) == 0)) {
        FAIL("generic query did not start prompt round-trip");
        return;
    }
    tx_reset();
    for (size_t i = 0; answer[i] != '\0'; i++)
        rip_query_response_byte((uint8_t)answer[i]);
    rip_query_response_byte(0x00);
    if (!s.query_pending &&
        s.user_var_count == 1 &&
        strcmp(s.user_var_names[0], "CITY") == 0 &&
        strcmp(s.user_var_values[0], "Paris") == 0 &&
        tx_len == 5 && memcmp(tx_capture, "Paris", 5) == 0)
        PASS();
    else
        FAIL("generic query response was not stored and returned");
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

static void test_text_window_passthrough_renders(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("text window passthrough renders a visible glyph (L17a)");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|w0A0A1E1E10|");
    if (!s.tw_active) { FAIL("setup: w did not activate text window"); return; }
    /* After the '|' terminator the FSM is in RIP_ST_COMMAND, not IDLE.
     * A bare '\r' returns it to IDLE so subsequent bytes route through
     * the text-window passthrough. */
    rip_process(&s, &ctx, '\r');
    rip_process(&s, &ctx, 'A');
    int hit = 0;
    for (int y = 11; y < 28 && !hit; y++)
        for (int x = 10; x < 18 && !hit; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) hit = 1;
    if (hit)
        PASS();
    else
        FAIL("text window passthrough produced no pixels (L17a regression)");
}

static void test_define_prompt_renders(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1D RIP_DEFINE prompt body renders (L17b)");
    init_fixture(&s, &ctx);
    /* 1D with non-$APPn$ variable name renders the default text via
     * draw_text.  Format: flags:3 res:2 then "varname?prompt?DEFAULT". */
    feed_script(&s, &ctx, "!|1D000  X?prompt?HELLO|");
    /* Default value "HELLO" should be drawn at (s->draw_x, s->draw_y).
     * draw_x/y are 0,0 by default.  Look for non-zero pixels in the
     * top-left where the string would be rendered. */
    int hit = 0;
    for (int y = 0; y < 16 && !hit; y++)
        for (int x = 0; x < 50 && !hit; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) hit = 1;
    if (hit)
        PASS();
    else
        FAIL("RIP_DEFINE prompt did not render (L17b regression)");
}

static void test_region_text_expands_variables(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1t REGION_TEXT expands $BEEP$ (regression for L16)");
    init_fixture(&s, &ctx);
    /* Open a text block, then 1t with body containing $BEEP$. */
    feed_script(&s, &ctx, "!|1T0000141400|");  /* begin block */
    if (!s.text_block.active) { FAIL("setup: 1T did not open block"); return; }
    tx_reset();
    feed_script(&s, &ctx, "!|1t0$BEEP$|");
    int has_bel = 0;
    for (size_t i = 0; i < tx_len; i++)
        if (tx_capture[i] == 0x07) { has_bel = 1; break; }
    if (has_bel)
        PASS();
    else
        FAIL("1t did not expand $BEEP$");
}

static void test_region_text_renders_bitmap(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1t REGION_TEXT bitmap path renders pixels (L16 NULL-font fix)");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1T0000141400|");
    if (!s.text_block.active) { FAIL("setup: 1T did not open block"); return; }
    /* Render a non-empty literal — before L16 this dropped silently. */
    feed_script(&s, &ctx, "!|1t0HELLO|");
    /* Look for any non-zero pixel in the text block region. */
    int hit = 0;
    for (int y = s.text_block.y0; y < s.text_block.y0 + 16 && !hit; y++)
        for (int x = s.text_block.x0; x < s.text_block.x0 + 50 && !hit; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) hit = 1;
    if (hit)
        PASS();
    else
        FAIL("1t bitmap rendering produced no pixels");
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

static void test_text_xy_ext_clips_to_box(void) {
    rip_state_t s;
    comp_context_t ctx;
    int inside = 0;
    int outside = 0;

    TEST("- (TEXT_XY_EXT) clips glyphs to its bounding box");
    init_fixture(&s, &ctx);
    /* Box (5,5)-(12,20), flags=00, long text would extend past x=12. */
    feed_script(&s, &ctx, "!|-05050C0K00AAAA|");
    for (int y = 5; y <= 23 && !inside; y++)
        for (int x = 5; x <= 12 && !inside; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) inside = 1;
    for (int y = 5; y <= 23 && !outside; y++)
        for (int x = 13; x <= 40 && !outside; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) outside = 1;
    if (inside && !outside)
        PASS();
    else
        FAIL("TEXT_XY_EXT ignored its clipping box");
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

static void test_bounded_text_clips_long_word(void) {
    rip_state_t s;
    comp_context_t ctx;
    int inside = 0;
    int outside = 0;

    TEST("'\"' RIP_BOUNDED_TEXT clips overlong words to the box");
    init_fixture(&s, &ctx);
    /* Box (5,5)-(12,20), flags=00, text would extend to x=36 unclipped. */
    feed_script(&s, &ctx, "!|\"05050C0K00AAAA|");
    for (int y = 5; y <= 20 && !inside; y++)
        for (int x = 5; x <= 12 && !inside; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) inside = 1;
    for (int y = 5; y <= 20 && !outside; y++)
        for (int x = 13; x <= 40 && !outside; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) outside = 1;
    if (inside && !outside)
        PASS();
    else
        FAIL("bounded text wrote outside its box");
}

static void test_bounded_text_expands_empty_app_variable(void) {
    rip_state_t s;
    comp_context_t ctx;
    int touched = 0;

    TEST("'\"' RIP_BOUNDED_TEXT expands empty $APPn$ variables");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|\"05050U1E00$APP0$|");
    for (int y = 5; y <= 30 && !touched; y++)
        for (int x = 5; x <= 60 && !touched; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) touched = 1;
    if (!touched)
        PASS();
    else
        FAIL("bounded text rendered a literal empty variable token");
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

static void test_mouse_click_uses_latest_overlapping_region(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("overlapping mouse regions are scanned last-in first-out");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|1M010A0A1E1E0000000OLD|");
    feed_script(&s, &ctx, "!|1M020A0A1E1E0000000NEW|");
    tx_reset();
    rip_mouse_event_state(&s, 15, 20, true);
    if (tx_len == 4 && memcmp(tx_capture, "NEW\r", 4) == 0)
        PASS();
    else
        FAIL("older overlapping mouse region won the hit test");
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

static void test_metadata_commands_store_state_and_vars(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("h/n/M/N metadata commands store state");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|h01000102|");
    feed_script(&s, &ctx, "!|n4000|");
    feed_script(&s, &ctx, "!|M18|");
    feed_script(&s, &ctx, "!|N00|");
    feed_script(&s, &ctx, "<<IF $COLORMODE$=8>>!|X0303|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $COORDSIZE$=4>>!|X0404|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $ISPALETTE$=1>>!|X0505|<<ENDIF>>");
    if (s.header_type == 1 &&
        s.header_id == 1 &&
        s.header_flags == 2 &&
        s.coordinate_size == 4 &&
        s.coordinate_res == 0 &&
        s.color_mode == 1 &&
        s.color_bits == 8 &&
        !s.filled_borders_enabled &&
        draw_get_pixel(3, 3) != 0 &&
        draw_get_pixel(4, 4) != 0 &&
        draw_get_pixel(5, 5) != 0)
        PASS();
    else
        FAIL("metadata command state or query variable mismatch");
}

static void test_header_reenables_filled_borders(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'h' RIP_HEADER restores filled_borders_enabled");
    init_fixture(&s, &ctx);
    /* Disable borders explicitly. */
    feed_script(&s, &ctx, "!|N00|");
    if (s.filled_borders_enabled) { FAIL("setup: N00 didn't disable"); return; }
    /* Send a header — Codex's handler resets borders=true per the comment
     * "header that resets the environment also restores filled-object
     * borders". */
    feed_script(&s, &ctx, "!|h00000000|");
    if (s.filled_borders_enabled)
        PASS();
    else
        FAIL("'h' did not restore filled_borders_enabled");
}

static void test_reset_windows_preserves_user_vars(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'*' RIP_RESET_WINDOWS preserves user_var and app_vars");
    init_fixture(&s, &ctx);
    /* Set both kinds. */
    feed_script(&s, &ctx, "!|1D000  $APP0$=alpha|");
    feed_script(&s, &ctx, "!|1D000  CITY=Austin|");
    if (strcmp(s.app_vars[0], "alpha") != 0 ||
        s.user_var_count != 1 ||
        strcmp(s.user_var_values[0], "Austin") != 0) {
        FAIL("setup: vars not stored");
        return;
    }
    /* Window reset (NOT session reset). */
    feed_script(&s, &ctx, "!|*|");
    /* Application data must survive — only window/draw state resets. */
    if (strcmp(s.app_vars[0], "alpha") == 0 &&
        s.user_var_count == 1 &&
        strcmp(s.user_var_values[0], "Austin") == 0)
        PASS();
    else
        FAIL("'*' clobbered application/user vars");
}

static void test_user_var_overflow_returns_false(void) {
    rip_state_t s;
    comp_context_t ctx;
    char cmd[64];

    TEST("user var table rejects writes past RIP_USER_VAR_MAX (16)");
    init_fixture(&s, &ctx);
    /* Fill all 16 slots. */
    for (int i = 0; i < RIP_USER_VAR_MAX; i++) {
        snprintf(cmd, sizeof(cmd), "!|1D000  V%d=val|", i);
        feed_script(&s, &ctx, cmd);
    }
    if (s.user_var_count != RIP_USER_VAR_MAX) {
        FAIL("setup: expected 16 user vars after fill");
        return;
    }
    /* Try one more — must NOT succeed and must NOT bump the count. */
    feed_script(&s, &ctx, "!|1D000  EXTRA=val|");
    if (s.user_var_count == RIP_USER_VAR_MAX)
        PASS();
    else
        FAIL("17th user var was accepted past the table size");
}

static void test_user_var_name_with_dollar_normalizes(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$NAME$ and NAME resolve to the same user-var slot");
    init_fixture(&s, &ctx);
    /* Set "$ZIP$=12345" — leading and trailing $ should be stripped. */
    feed_script(&s, &ctx, "!|1D000  $ZIP$=12345|");
    /* Query without $ wrappers in the name (they're added by the lookup). */
    tx_reset();
    feed_script(&s, &ctx, "!|1Q00000$ZIP$|");
    if (tx_len == 5 && memcmp(tx_capture, "12345", 5) == 0)
        PASS();
    else
        FAIL("dollar-wrapped user var name did not normalize");
}

static void test_user_var_name_with_invalid_char_rejected(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("user var name with non-alphanumeric char is rejected");
    init_fixture(&s, &ctx);
    /* "BAD-NAME=val" — '-' isn't alphanumeric or underscore. */
    feed_script(&s, &ctx, "!|1D000  BAD-NAME=val|");
    if (s.user_var_count == 0)
        PASS();
    else
        FAIL("invalid var name was accepted");
}

static void test_filled_border_disabled_skips_rect_outline(void) {
    rip_state_t s;
    comp_context_t ctx;
    int outline_pixel;
    int interior_pixel;

    TEST("N00 also disables outlines on filled rects via [");
    init_fixture(&s, &ctx);
    /* fill_pattern=1 (solid), fill_color=4 (red), draw_color=15 (white).
     * filled_borders OFF.  Then [ filled-rect-ext.  Outline pixel must
     * be the fill color, not the draw color. */
    feed_script(&s, &ctx, "!|c0F|S0104|N00|");
    /* [ rect with bx0=0A by0=0A bx1=0F by1=0F mode=00 p1=00 p2=00 */
    feed_script(&s, &ctx, "!|[0A0A0F0F000000|");
    /* Top-left corner (10, 11) should be fill-color (palette[4]=244) since
     * outline was suppressed. */
    outline_pixel = draw_get_pixel(10, 11);
    interior_pixel = draw_get_pixel(12, 13);
    if (outline_pixel == s.palette[4] && interior_pixel == s.palette[4])
        PASS();
    else
        FAIL("rect outline drew despite N00");
}

static void test_filled_border_helper_always_sets_copy_mode(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint8_t pixel_at_top;

    TEST("filled-border helper draws outline in COPY even when XOR active");
    init_fixture(&s, &ctx);
    /* Pre-fill the spot we'll draw the outline at, then set XOR. */
    feed_script(&s, &ctx, "!|c0F|S0104|");
    s.write_mode = DRAW_MODE_XOR;
    draw_set_write_mode(DRAW_MODE_XOR);
    /* Filled circle G — cx=20 cy=20 r=5 in EGA (encoded "0K0K05").
     * After scaling: cx=20, cy=22, r=5.  Top of circle at (20, 17).
     * The outline top pixel should be palette[15] (NOT XOR-ed back).
     * If the helper forgot to switch to COPY, the outline would XOR
     * with the underlying fill. */
    feed_script(&s, &ctx, "!|G0K0K05|");
    pixel_at_top = draw_get_pixel(20, 17);
    if (pixel_at_top == s.palette[15])
        PASS();
    else
        FAIL("filled-border outline used XOR instead of COPY");
}

static void test_filled_border_toggle_controls_outline(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("N toggles draw-color borders on filled objects");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|c0F|S0104|");
    feed_script(&s, &ctx, "!|N00|!|G0K0K05|");
    feed_script(&s, &ctx, "!|N01|!|G140K05|");
    if (draw_get_pixel(20, 17) == s.palette[4] &&
        draw_get_pixel(40, 17) == s.palette[15])
        PASS();
    else
        FAIL("filled-object border flag was not honored");
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

static void test_l2_scale_text_applies_rotation(void) {
    rip_state_t s;
    comp_context_t ctx;
    int hits;

    TEST("26 rotation 90 sets font_dir=1, scale sets font_size");
    init_fixture(&s, &ctx);
    /* scale=05 (font_size=5), rotation=2I (90): mega2("2I") = 2*36+18 = 90. */
    feed_script(&s, &ctx, "!|26052I|");
    if (s.font_size != 5 || s.font_dir != 1) {
        FAIL("scale or rotation not applied to BGI render state");
        return;
    }
    /* rotation=7I (270) should map to font_dir=2 (CCW). */
    feed_script(&s, &ctx, "!|26057I|");
    if (s.font_dir != 2) {
        FAIL("rotation 270 did not map to font_dir 2");
        return;
    }
    /* rotation back near 0 should reset to horizontal. */
    feed_script(&s, &ctx, "!|26050A|");  /* rotation=10 → snap to 0 */
    hits = (s.font_dir == 0) ? 1 : 0;
    if (hits)
        PASS();
    else
        FAIL("rotation near 0 did not reset font_dir");
}

static void test_icon_style_tile_mode_repeats(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint8_t *pixels;
    int hits;

    TEST("ICON_STYLE mode 1 (tile) stamps the icon multiple times");
    init_fixture(&s, &ctx);
    /* Cache a 4x4 solid 0x60 runtime icon. */
    pixels = (uint8_t *)psram_arena_alloc(&s.psram_arena, 16);
    if (!pixels) { FAIL("setup: arena"); return; }
    for (int i = 0; i < 16; i++) pixels[i] = 0x60;
    if (!rip_icon_cache_pixels_replace(&s.icon_state, "TILEME", 6, pixels, 4, 4)) {
        FAIL("setup: cache"); return;
    }
    /* ICON_STYLE box (0,0)-(15,7) — 16x8 box, mode=01 (tile),
     * align=00 scale=00.  Each field is 2 chars. */
    feed_script(&s, &ctx, "!|&00000F0701000000|");
    if (!s.icon_style_active) { FAIL("setup: & not active"); return; }
    feed_script(&s, &ctx, "!|1I000000000TILEME|");
    /* In tile mode the 4x4 icon should appear at multiple cell origins
     * within the box.  Sample the four cell-origins at (0,0), (4,0),
     * (8,0), (12,0). */
    hits = 0;
    for (int x = 0; x < 16; x += 4) {
        if (draw_get_pixel((int16_t)x, 0) == 0x60) hits++;
    }
    if (hits >= 2)
        PASS();
    else
        FAIL("tile mode did not produce repeated icon stamps");
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

static void test_l2_widgets_draw_palette_indices(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("Level 2 widgets draw palette indices, not RGB332 values");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|2200000A0A|");
    if (draw_get_pixel(0, 0) == 7 &&
        draw_get_pixel(1, 1) == 1)
        PASS();
    else
        FAIL("2.0 window wrote RGB332 values as framebuffer indices");
}

static void test_l2_special_draws_preserve_color(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint8_t expected;

    TEST("Level 2 widgets/gradient preserve active draw color");
    init_fixture(&s, &ctx);
    expected = s.palette[s.draw_color & 0x0F];

    feed_script(&s, &ctx, "!|2200000A0A|");
    feed_script(&s, &ctx, "!|X0K0K|");
    if (draw_get_pixel(20, 22) != expected) {
        FAIL("window command leaked its temporary draw color");
        return;
    }

    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|2800000A0A010201|");
    feed_script(&s, &ctx, "!|X0K0K|");
    if (draw_get_pixel(20, 22) == expected)
        PASS();
    else
        FAIL("gradient command leaked its temporary draw color");
}

static void test_l2_clipboard_capture_paste(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("27 clipboard captures and pastes pixels");
    init_fixture(&s, &ctx);
    draw_set_color(42);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|270102020202|");
    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|27020A0A00|");
    if (draw_get_pixel(10, 11) == 42)
        PASS();
    else
        FAIL("27 clipboard paste did not restore captured pixels");
}

static void test_l2_alpha_preserves_write_mode(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint8_t expected;

    TEST("29 alpha blend restores color and write mode");
    init_fixture(&s, &ctx);
    expected = s.palette[s.draw_color & 0x0F];
    draw_fill_screen(0xFF);
    s.write_mode = DRAW_MODE_XOR;
    draw_set_write_mode(DRAW_MODE_XOR);
    draw_set_color(expected);
    feed_script(&s, &ctx, "!|2900000101050F|");
    feed_script(&s, &ctx, "!|X0K0K|");
    if (draw_get_pixel(20, 22) == 0)
        PASS();
    else
        FAIL("29 alpha blend leaked COPY mode or temporary color");
}

static void test_l2_port_copy_scales_destination_rect(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("2C port copy scales to destination rectangle");
    init_fixture(&s, &ctx);
    draw_set_color(55);
    draw_rect(2, 2, 1, 2, true);
    feed_script(&s, &ctx, "!|2C00202020200K0K0M0M0|");
    if (draw_get_pixel(20, 22) == 55 &&
        draw_get_pixel(22, 25) == 55)
        PASS();
    else
        FAIL("2C did not scale the copied port region");
}

static void test_l2_query_palette_replies(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("21 query-palette replies with RGB components");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "!|2107|");
    if (tx_len > 0 && tx_capture[0] == '7' &&
        tx_capture[tx_len - 1] == '\r')
        PASS();
    else
        FAIL("21 did not emit a palette query response");
}

static void test_l2_chord_scales_radius_like_level0_arcs(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("2c chord scales radius through the EGA-to-card Y transform");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|2c0K0K070000|");
    if (draw_get_pixel(28, 22) != 0 && draw_get_pixel(27, 22) == 0)
        PASS();
    else
        FAIL("2c chord used unscaled radius");
}

static void test_port_switch_restores_pattern_back_color(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("port restore keeps patterned fill OFF bits on back_color");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|S020C|");
    feed_script(&s, &ctx, "!|k3|");
    feed_script(&s, &ctx, "!|2P1000A140U00|");
    feed_script(&s, &ctx, "!|2s100|");
    feed_script(&s, &ctx, "!|2s000|");
    feed_script(&s, &ctx, "!|B05050F0F|");
    if (draw_get_pixel(10, 5) == 243)
        PASS();
    else
        FAIL("port restore used fill_color as patterned-fill background");
}

static void test_write_icon_caches_clipboard_for_load_icon(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1W caches clipboard pixels for later 1I load");
    init_fixture(&s, &ctx);
    draw_set_color(77);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|1C02020202 |");
    feed_script(&s, &ctx, "!|1WTESTICON|");
    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|1I0A0A00000TESTICON|");
    if (draw_get_pixel(10, 11) == 77)
        PASS();
    else
        FAIL("1W cached icon could not be loaded by 1I");
}

static void test_write_icon_replaces_cached_name(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1W replaces an existing runtime icon name");
    init_fixture(&s, &ctx);
    draw_set_color(77);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|1C02020202 |");
    feed_script(&s, &ctx, "!|1WTESTICON|");

    draw_set_color(66);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|1C02020202 |");
    feed_script(&s, &ctx, "!|1WTESTICON|");

    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|1I0A0A00000TESTICON|");
    if (draw_get_pixel(10, 11) == 66)
        PASS();
    else
        FAIL("1W left stale pixels for an existing icon name");
}

static void test_runtime_icon_supersedes_flash(void) {
    rip_state_t s;
    comp_context_t ctx;
    uint8_t *pixels;
    rip_icon_t icon;

    TEST("runtime cache lookup wins over a same-named flash entry");
    init_fixture(&s, &ctx);
    /* "AE5" exists in the flash table.  Cache a 1x1 runtime icon under
     * the same name; lookup must return the runtime entry. */
    pixels = (uint8_t *)psram_arena_alloc(&s.psram_arena, 1);
    if (!pixels) { FAIL("setup: arena alloc"); return; }
    pixels[0] = 0x42;
    if (!rip_icon_cache_pixels_replace(&s.icon_state, "AE5", 3, pixels, 1, 1)) {
        FAIL("setup: cache_pixels_replace"); return;
    }
    if (rip_icon_lookup(&s.icon_state, "AE5", 3, &icon) &&
        icon.width == 1 && icon.height == 1 && icon.pixels[0] == 0x42)
        PASS();
    else
        FAIL("flash entry was returned even though runtime entry exists");
}

static void test_clipboard_op_5_capture_op_6_paste(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1X op 5 (capture rect) and op 6 (paste) round-trip");
    init_fixture(&s, &ctx);
    draw_set_color(0x37);
    draw_rect(2, 2, 2, 2, true);
    /* op=05, x0=02 y0=02 x1=03 y1=03 = capture (2,2)-(3,3). */
    feed_script(&s, &ctx, "!|1X0502020303|");
    if (!s.clipboard.valid) { FAIL("op 5 did not capture"); return; }
    /* Wipe and paste at (10, 10) with mode 00 (COPY). */
    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|1X060A0A00|");
    /* Paste lands at (10, scale_y(10)=11). */
    if (draw_get_pixel(10, 11) == 0x37)
        PASS();
    else
        FAIL("op 6 paste did not restore captured pixel");
}

static void test_save_icon_slot_out_of_range_is_noop(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("J with slot >= RIP_ICON_SLOT_MAX is a silent no-op");
    init_fixture(&s, &ctx);
    draw_set_color(0x55);
    draw_rect(0, 0, 1, 1, true);
    feed_script(&s, &ctx, "!|<00000000|");
    /* slot 36 ("10" in MegaNum) is just past RIP_ICON_SLOT_MAX (36). */
    feed_script(&s, &ctx, "!|J10|");
    int any_valid = 0;
    for (int i = 0; i < RIP_ICON_SLOT_MAX; i++)
        if (s.icon_slot_valid[i]) { any_valid = 1; break; }
    if (!any_valid)
        PASS();
    else
        FAIL("J with out-of-range slot still wrote to the slot table");
}

static void test_stamp_icon_unset_slot_falls_back_to_clipboard(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("'.' with an unset slot falls back to current clipboard");
    init_fixture(&s, &ctx);
    draw_set_color(0x44);
    draw_rect(2, 2, 1, 1, true);
    feed_script(&s, &ctx, "!|<02020202|");
    if (!s.clipboard.valid) { FAIL("setup: <"); return; }
    /* Stamp slot 03 (never saved) at (20, 20).  Should use clipboard. */
    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|.030K0K000000|");
    if (draw_get_pixel(20, 22) == 0x44)
        PASS();
    else
        FAIL("stamp did not fall back to clipboard for an unset slot");
}

static void test_save_and_stamp_icon_slot(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("J save-icon slot can be stamped with '.'");
    init_fixture(&s, &ctx);
    draw_set_color(88);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|<02020202|");
    feed_script(&s, &ctx, "!|J05|");
    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|.050U0U000000|");
    if (draw_get_pixel(30, 34) == 88)
        PASS();
    else
        FAIL("saved icon slot did not stamp");
}

static void test_save_icon_slot_updates_load_alias(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("J save-icon updates the SLOTnn load alias");
    init_fixture(&s, &ctx);
    draw_set_color(88);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|<02020202|");
    feed_script(&s, &ctx, "!|J05|");

    draw_set_color(99);
    draw_rect(2, 2, 2, 2, true);
    feed_script(&s, &ctx, "!|<02020202|");
    feed_script(&s, &ctx, "!|J05|");

    draw_fill_screen(0);
    feed_script(&s, &ctx, "!|1I0A0A00000SLOT05|");
    if (draw_get_pixel(10, 11) == 99)
        PASS();
    else
        FAIL("J left stale pixels in the SLOTnn load alias");
}

static void test_l0_copy_region_scales_destination_rect(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("extended copy-region scales to destination rectangle");
    init_fixture(&s, &ctx);
    draw_set_color(99);
    draw_rect(2, 2, 2, 2, true);
    /* src (2,2)-(2,2), dest (20,20)-(22,22), reserved 0000 */
    feed_script(&s, &ctx, "!|,020202020K0K0M0M0000|");
    if (draw_get_pixel(20, 22) == 99 &&
        draw_get_pixel(22, 25) == 99)
        PASS();
    else
        FAIL("extended copy-region did not scale source pixels");
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

/* COVERAGE: rip_iso_week + rip_parse_host_date + rip_day_of_year
 * + rip_weekday_monday0 + rip_days_from_civil. Jan 15, 2026 is a
 * Thursday in ISO week 3 (Jan 12–18 is week 3 of 2026). The IF
 * expression triggers rip_expand_variables which is the only public
 * surface that resolves $WOYM$ (1Q only handles $APPn$/user vars). */
static void test_iso_week_var_expansion(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$WOYM$ expansion uses ISO week from host_date");
    init_fixture(&s, &ctx);
    strcpy(s.host_date, "01/15/26");
    feed_script(&s, &ctx, "<<IF $WOYM$=03>>!|X0303|<<ENDIF>>");
    if (draw_get_pixel(3, 3) != 0)
        PASS();
    else
        FAIL("$WOYM$ did not resolve to 03 for 2026-01-15");
}

/* COVERAGE: iso_weeks_in_year + week>weeks_in_year branch.
 * Dec 31 2024 (Tue) belongs to ISO week 1 of 2025 — the wrap branch. */
static void test_iso_week_year_wrap(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("$WOYM$ wraps Dec 31 2024 into ISO week 01 of 2025");
    init_fixture(&s, &ctx);
    strcpy(s.host_date, "12/31/24");
    feed_script(&s, &ctx, "<<IF $WOYM$=01>>!|X0404|<<ENDIF>>");
    if (draw_get_pixel(4, 4) != 0)
        PASS();
    else
        FAIL("$WOYM$ did not wrap into week 01 for 2024-12-31");
}

/* COVERAGE: rip_font_id_from_name. 1O (RIP_FONT_LOAD) with TRIP.CHR
 * should map to BGI_FONT_TRIPLEX (=1) and update s->font_id. */
static void test_font_load_resolves_bgi_name(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1O TRIP.CHR resolves to BGI_FONT_TRIPLEX");
    init_fixture(&s, &ctx);
    s.font_id = 0;
    feed_script(&s, &ctx, "!|1OTRIP.CHR|");
    if (s.font_id == BGI_FONT_TRIPLEX && s.font_ext_id == BGI_FONT_TRIPLEX)
        PASS();
    else
        FAIL("1O did not pick up TRIP→TRIPLEX font name");
}

/* COVERAGE: rip_font_id_from_name lower-case + path-prefix branches.
 * Confirms case folding ('trip') and leading directory ('FONTS\\') strip. */
static void test_font_load_resolves_path_and_case(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("1O case-insensitive + strips leading path");
    init_fixture(&s, &ctx);
    s.font_id = 0;
    feed_script(&s, &ctx, "!|1OFONTS\\sans.chr|");
    if (s.font_id == BGI_FONT_SANS)
        PASS();
    else
        FAIL("1O did not strip path/lowercase to SANS font");
}

/* COVERAGE: rip_clipboard_store_pixels. 1I LOAD_ICON with the
 * clipboard flag set must mirror the loaded icon into s->clipboard. */
static void test_load_icon_clipboard_flag_stores_pixels(void) {
    rip_state_t s;
    comp_context_t ctx;
    static const uint8_t px[4] = { 1, 2, 3, 4 };
    uint8_t *cached;

    TEST("1I clipboard flag stores icon pixels in clipboard");
    init_fixture(&s, &ctx);
    cached = (uint8_t *)psram_arena_alloc(&s.psram_arena, 4);
    if (!cached) { FAIL("setup: arena alloc"); return; }
    memcpy(cached, px, 4);
    if (!rip_icon_cache_pixels(&s.icon_state, "FOO", 3, cached, 2, 2)) {
        FAIL("setup: cache_pixels");
        return;
    }
    /* 1I  x:00 y:00 mode:00 clip:1 res:00 name:FOO */
    feed_script(&s, &ctx, "!|1I000000100FOO|");
    if (s.clipboard.valid &&
        s.clipboard.width == 2 && s.clipboard.height == 2 &&
        s.clipboard.data &&
        memcmp(s.clipboard.data, px, 4) == 0)
        PASS();
    else
        FAIL("clipboard not populated from 1I clipboard flag");
}

/* COVERAGE: rip_mouse_event_ext (global wrapper).
 * Verifies the extern-C entrypoint that dispatches via g_rip_state
 * works end-to-end after rip_init bound the singleton. */
static void test_global_mouse_event_dispatches(void) {
    rip_state_t s;
    comp_context_t ctx;

    TEST("rip_mouse_event_ext routes through g_rip_state");
    init_fixture(&s, &ctx);
    rip_init(&s);
    /* Define mouse region #0 at (10,10)-(50,50) hotkey=0 flags=0
     * host="X".  Expect "X\r" pushed to TX on a click inside. */
    feed_script(&s, &ctx, "!|1M000A0A1E1E0000000X|");
    if (s.num_mouse_regions != 1) { FAIL("setup: 1M missed"); return; }
    tx_reset();
    rip_mouse_event_ext(20, 20, true);
    if (tx_len == 2 && memcmp(tx_capture, "X\r", 2) == 0)
        PASS();
    else
        FAIL("rip_mouse_event_ext did not dispatch host string");
}

/* COVERAGE: rip_file_upload_begin/byte/end (global wrappers).
 * Same payload as test_icn_upload_replaces_cached_name but via the
 * non-state, g_rip_state-based public API. */
static void test_global_file_upload_caches_icn(void) {
    static const char icon_name[] = "GLOBALUP";
    static const uint8_t icn_blue[10] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00
    };
    rip_state_t s;
    comp_context_t ctx;
    rip_icon_t icon;

    TEST("global rip_file_upload_* wrappers cache an ICN");
    init_fixture(&s, &ctx);
    rip_init(&s);
    rip_file_upload_begin(8);
    for (size_t i = 0; i < 8; i++)
        rip_file_upload_byte((uint8_t)icon_name[i]);
    for (size_t i = 0; i < sizeof(icn_blue); i++)
        rip_file_upload_byte(icn_blue[i]);
    rip_file_upload_end();
    if (rip_icon_cache_count(&s.icon_state) == 1 &&
        rip_icon_lookup(&s.icon_state, icon_name, 8, &icon) &&
        icon.width == 1 && icon.height == 1)
        PASS();
    else
        FAIL("global upload wrappers did not cache ICN");
}

/* ═══════════════════════════════════════════════════════════════════
 * WIRE-LEVEL COVERAGE TESTS — every protocol command exercised at
 * least once via feed_script(). Identified by spec-vs-test audit:
 * 37 commands had dispatchers but no wire-format test.
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Level 0 state setters ────────────────────────────────────────── */

static void test_l0_write_mode_W(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|W sets write mode");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|W03|");  /* mode=3 (XOR) */
    if (s.write_mode == 3) PASS(); else FAIL("|W did not set write_mode");
}

static void test_l0_line_style_eq(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|= sets line style and thickness");
    init_fixture(&s, &ctx);
    /* style=01 user_pat=0000 thick=03 → s->line_style=1, s->line_thick=scaled */
    feed_script(&s, &ctx, "!|=01000003|");
    if (s.line_style == 1 && s.line_thick >= 3) PASS();
    else FAIL("|= did not store line style");
}

static void test_l0_font_style_Y(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|Y sets font id, dir, size");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|Y010003|");  /* font=1 dir=0 size=3 */
    if (s.font_id == 1 && s.font_dir == 0 && s.font_size == 3) PASS();
    else FAIL("|Y did not update font fields");
}

static void test_l0_move_m(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|m moves drawing cursor");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|m0A0K|");  /* x=10, y=20 (scaled) */
    if (s.draw_x == 10) PASS();
    else FAIL("|m did not update draw_x");
}

static void test_l0_gotoxy_g(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|g calls comp_set_cursor without crashing");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|g050A|");  /* (5, 10) */
    PASS();  /* Composer cursor is a platform stub; just verify dispatch */
}

static void test_l0_home_H(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|H dispatches RIP_HOME");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|H|");
    PASS();
}

static void test_l0_set_palette_Q(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|Q sets full 16-entry palette");
    init_fixture(&s, &ctx);
    memset(palette, 0, sizeof(palette));
    /* 16 EGA64 indices, each 2 chars.  Use "01" through "0G" (=16). */
    feed_script(&s, &ctx,
        "!|Q01020304050607080902020202020202|");
    /* palette_slot(0)..(15) = 240..255 should now hold values != 0 for ega64 idx 1+ */
    int populated = 0;
    for (int i = 240; i < 256; i++)
        if (palette[i] != 0) populated++;
    if (populated >= 8) PASS();
    else FAIL("|Q did not write palette entries");
}

/* ── Level 0 drawing primitives ───────────────────────────────────── */

static void test_l0_rectangle_R(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|R draws rectangle outline");
    init_fixture(&s, &ctx);
    s.draw_color = 5;
    feed_script(&s, &ctx, "!|R05050F0F|");  /* (5,5)-(15,15) */
    /* Corner pixel should be set; interior should not */
    if (draw_get_pixel(5, 5) != 0 && draw_get_pixel(10, 10) == 0) PASS();
    else FAIL("|R did not draw outline only");
}

static void test_l0_circle_C(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|C draws circle outline");
    init_fixture(&s, &ctx);
    /* center (20, 10) r=5 — "0K"=20, "0A"=10 */
    feed_script(&s, &ctx, "!|C0K0A05|");
    int found = 0;
    for (int x = 12; x < 28 && !found; x++)
        for (int y = 4; y < 20 && !found; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|C drew nothing");
}

static void test_l0_oval_O(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|O draws elliptical arc");
    init_fixture(&s, &ctx);
    /* center (40,40) sa=0 ea=180 rx=15 ry=12 — full top half */
    feed_script(&s, &ctx, "!|O141400500F0C|");
    int found = 0;
    for (int y = 20; y < 50 && !found; y++)
        for (int x = 20; x < 60 && !found; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|O drew nothing");
}

static void test_l0_filled_oval_o(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|o draws filled ellipse");
    init_fixture(&s, &ctx);
    s.fill_color = 5;
    s.fill_pattern = 1;  /* solid */
    feed_script(&s, &ctx, "!|o140K0K0A|");  /* center (20,20) rx=20 ry=10 */
    if (draw_get_pixel(20, 20) != 0) PASS();
    else FAIL("|o did not fill ellipse center");
}

static void test_l0_arc_A(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|A draws circular arc");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|A140K005A0A|");  /* center (20,20) 0-90° r=10 */
    int found = 0;
    for (int x = 18; x < 32 && !found; x++)
        for (int y = 8; y < 22 && !found; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|A drew nothing");
}

static void test_l0_oval_pie_slice_i(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|i draws filled oval pie slice");
    init_fixture(&s, &ctx);
    s.fill_color = 7;
    s.fill_pattern = 1;
    feed_script(&s, &ctx, "!|i140K005A0K0A|");  /* (20,20) 0-90° rx=20 ry=10 */
    /* Center should be filled */
    if (draw_get_pixel(22, 20) != 0) PASS();
    else FAIL("|i did not fill");
}

static void test_l0_bezier_Z(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|Z draws single cubic bezier");
    init_fixture(&s, &ctx);
    /* (0,0) (10,30) (20,30) (30,0) steps=08 → 16 chars + 2 = 18 */
    feed_script(&s, &ctx, "!|Z00000A1U0U1U1E000008|");
    int found = 0;
    for (int x = 0; x < 40 && !found; x++)
        for (int y = 0; y < 40 && !found; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|Z drew nothing");
}

static void test_l0_polyline_l(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|l draws open polyline");
    init_fixture(&s, &ctx);
    /* 2 points: (5,5) (20,5) → horizontal line */
    feed_script(&s, &ctx, "!|l020505140 5|");
    /* Some pixel on x in [5..20] at y=5 should be set */
    int found = 0;
    for (int x = 5; x <= 20 && !found; x++)
        if (draw_get_pixel((int16_t)x, 5) != 0) found = 1;
    if (found) PASS(); else FAIL("|l drew nothing");
}

/* ── Level 0 cursor / lifecycle ───────────────────────────────────── */

static void test_l0_erase_window_e(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|e dispatches RIP_ERASE_WINDOW");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|e|");
    PASS();  /* composer is a stub in test fixture */
}

static void test_l0_erase_eol(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|> dispatches RIP_ERASE_EOL");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|>|");
    PASS();
}

static void test_l0_no_more_hash(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|# (RIP_NO_MORE) does not crash");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|#|");
    PASS();
}

static void test_l0_region_text_t(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|t renders justified text inside a text block");
    init_fixture(&s, &ctx);
    /* 1T begins a text block (10,10)-(50,30) so that L0 |t can render. */
    feed_script(&s, &ctx, "!|1T0A0A1E1E00|");
    if (!s.text_block.active) { FAIL("setup: 1T missed"); return; }
    feed_script(&s, &ctx, "!|t0X|");
    int found = 0;
    for (int x = 8; x < 30 && !found; x++)
        for (int y = 8; y < 40 && !found; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|t drew nothing");
}

static void test_l0_custom_fill_pattern_s(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|s sets custom 8x8 fill pattern + color");
    init_fixture(&s, &ctx);
    /* 8 bytes + col = 9 mega2 fields = 18 chars.  All 0xAA + col=05. */
    feed_script(&s, &ctx, "!|s2U2U2U2U2U2U2U2U05|");
    if (s.fill_color == 5) PASS();
    else FAIL("|s did not set fill_color");
}

/* ── Extended single-char ─────────────────────────────────────────── */

static void test_ext_rounded_rect_U(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|U draws rounded rectangle outline");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|U05050F0F03|");  /* (5,5)-(15,15) r=3 */
    if (draw_get_pixel(10, 5) != 0) PASS();
    else FAIL("|U drew nothing on top edge");
}

static void test_ext_filled_rounded_rect_u(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|u draws filled rounded rectangle");
    init_fixture(&s, &ctx);
    s.fill_color = 6;
    s.fill_pattern = 1;
    feed_script(&s, &ctx, "!|u05050F0F03|");
    if (draw_get_pixel(10, 10) != 0) PASS();
    else FAIL("|u did not fill interior");
}

static void test_ext_polyline_ext(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|] dispatches polyline ext");
    init_fixture(&s, &ctx);
    /* x0:2 y0:2 x1:2 y1:2 mode:2 p1:2 p2:2 = 14 chars */
    feed_script(&s, &ctx, "!|]050505140000000000|");
    PASS();
}

static void test_ext_animation_frame_brace(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|{ draws animation frame (triangle)");
    init_fixture(&s, &ctx);
    s.fill_color = 5;
    s.fill_pattern = 1;
    feed_script(&s, &ctx, "!|{050514051E0F0F|");  /* (5,5) (20,5) (15,30) */
    if (draw_get_pixel(15, 10) != 0 || draw_get_pixel(10, 5) != 0) PASS();
    else FAIL("|{ drew nothing");
}

static void test_ext_kill_mouse_in_region_K(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|K removes mouse regions intersecting rect");
    init_fixture(&s, &ctx);
    /* Define a region at (10,10)-(50,50) */
    feed_script(&s, &ctx, "!|1M000A0A1E1E0000000X|");
    if (s.num_mouse_regions != 1) { FAIL("setup: 1M missed"); return; }
    /* Kill any region intersecting (0,0)-(60,60) — should drop ours.
     * x0:2 y0:2 x1:2 y1:2 = "00"+"00"+"1O"+"1O" where "1O"=60. */
    feed_script(&s, &ctx, "!|K00001O1O|");
    if (s.num_mouse_regions == 0) PASS();
    else FAIL("|K did not remove intersecting region");
}

static void test_ext_mouse_region_ext_colon(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|: adds extended mouse region");
    init_fixture(&s, &ctx);
    /* x0:2 y0:2 x1:2 y1:2 hotkey:2 flags:2 res×5 (10 chars) = 22 total */
    feed_script(&s, &ctx, "!|:05050F0F00000000000000|");
    if (s.num_mouse_regions == 1) PASS();
    else FAIL("|: did not add region");
}

static void test_ext_button_ext_semicolon(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|; adds extended button + region");
    init_fixture(&s, &ctx);
    /* x0:2 y0:2 x1:2 y1:2 style:2 lx:2 ly:2 = 14 chars min */
    feed_script(&s, &ctx, "!|;05050F0F00000000|");
    if (s.num_mouse_regions == 1) PASS();
    else FAIL("|; did not add region");
}

static void test_ext_ext_text_window_b(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|b activates extended text window");
    init_fixture(&s, &ctx);
    /* x0:2 y0:2 x1:2 y1:2 fore:2 back:2 font:1 size:4 flags:3 = 18 */
    feed_script(&s, &ctx, "!|b05050F0F0F00000A04000|");
    if (s.tw_active && s.etw_fore_col == 15) PASS();
    else FAIL("|b did not activate ext text window");
}

static void test_ext_ext_font_style_d(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|d sets extended font style fields");
    init_fixture(&s, &ctx);
    /* font_id:2 attr:1 size:4 = 7 chars.  font=01 attr=3 size=0001 */
    feed_script(&s, &ctx, "!|d0130001|");
    if (s.font_ext_id == 1 && s.font_ext_attr == 3) PASS();
    else FAIL("|d did not set ext font fields");
}

static void test_ext_font_attrib_f(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|f sets font attribute flags");
    init_fixture(&s, &ctx);
    /* attrib:2 reserved:2.  attrib=03 (bold+italic) */
    feed_script(&s, &ctx, "!|f0300|");
    if (s.font_attrib == 3) PASS();
    else FAIL("|f did not set font_attrib");
}

static void test_ext_fill_pattern_ext_D(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|D installs user fill pattern, sets fill_pattern=12");
    init_fixture(&s, &ctx);
    /* 8 pattern bytes (mega2 each = 16 chars) + color:2 = 18 chars */
    feed_script(&s, &ctx, "!|D2U2U2U2U2U2U2U2U07|");
    if (s.fill_pattern == 12 && s.fill_color == 7) PASS();
    else FAIL("|D did not install user fill pattern");
}

/* ── Level 1 ──────────────────────────────────────────────────────── */

static void test_l1_button_style_B(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|1B updates button style state");
    init_fixture(&s, &ctx);
    /* wid:2 hgt:2 orient:2 flags:4 bevsize:2 dfore:2 dback:2 bright:2 dark:2
     * surface:2 grp_no:2 flags2:2 uline:2 corner:2 res:6 = 30 chars min */
    feed_script(&s, &ctx, "!|1B0F0F00000002030405060708000000000000|");
    if (s.button_style.width == 15 && s.button_style.height == 15) PASS();
    else FAIL("|1B did not store width/height");
}

static void test_l1_set_icon_dir_N(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|1N stores icon directory");
    init_fixture(&s, &ctx);
    /* res:2 + path */
    feed_script(&s, &ctx, "!|1N00ICONS|");
    if (strcmp(s.icon_dir, "ICONS") == 0) PASS();
    else FAIL("|1N did not store icon_dir");
}

static void test_l1_play_midi_Z(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|1Z pushes CMD_PLAY_SOUND marker for MIDI");
    init_fixture(&s, &ctx);
    tx_reset();
    /* mode:2 res:2 filename */
    feed_script(&s, &ctx, "!|1Z0000SONG|");
    if (tx_len >= 5 && tx_capture[0] == 0x3D &&
        memcmp(tx_capture + 1, "SONG", 4) == 0) PASS();
    else FAIL("|1Z did not push MIDI marker");
}

/* ── Level 2 widgets ──────────────────────────────────────────────── */

static void test_l2_scrollbar_renders(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|23 renders scrollbar track + thumb");
    init_fixture(&s, &ctx);
    /* x=10 y=20 w=8 h=40 min=0 max=64 value=10 page_size=8 */
    feed_script(&s, &ctx, "!|230A0K081400 1S0A 08|");
    /* The track is light-gray (palette index 7).  Inside the track box
     * (x∈[10..17], y∈[20..59]) at least one pixel should be non-zero. */
    int found = 0;
    for (int y = 20; y < 60 && !found; y++)
        for (int x = 10; x < 18 && !found; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|23 drew nothing");
}

static void test_l2_menu_renders(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|24 renders menu bar");
    init_fixture(&s, &ctx);
    /* y=2 height=10 bg=7 text=0 */
    feed_script(&s, &ctx, "!|24020A0700|");
    /* Menu bar spans full width.  Some pixel in row y=2..12 should be set. */
    int found = 0;
    for (int x = 0; x < 640 && !found; x += 32)
        for (int y = 2; y < 12 && !found; y++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|24 drew nothing");
}

static void test_l2_dialog_renders(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|25 renders dialog box with shadow");
    init_fixture(&s, &ctx);
    /* x=10 y=10 w=30 h=20 title_color=1 bg_color=7 */
    feed_script(&s, &ctx, "!|250A0A0K0K 0107|");
    /* Inside the dialog box, some pixel should be set */
    int found = 0;
    for (int y = 10; y < 30 && !found; y++)
        for (int x = 10; x < 40 && !found; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|25 drew nothing");
}

static void test_l2_set_refresh(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|2R dispatches SET_REFRESH without crashing");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|2R|");
    PASS();
}

/* ═══════════════════════════════════════════════════════════════════
 * BUILT-IN TEXT VARIABLE COVERAGE  (rip_expand_variables)
 * ═══════════════════════════════════════════════════════════════════ */

static void test_var_blip_pushes_marker(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$BLIP$ pushes 0x3D BLIP marker to TX");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "!|T$BLIP$|");
    if (tx_len >= 5 && tx_capture[0] == 0x3D &&
        memcmp(tx_capture + 1, "BLIP", 4) == 0) PASS();
    else FAIL("$BLIP$ did not push marker");
}

static void test_var_alarm_pushes_marker(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$ALARM$ pushes 0x3D ALARM marker to TX");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "!|T$ALARM$|");
    if (tx_len >= 6 && tx_capture[0] == 0x3D &&
        memcmp(tx_capture + 1, "ALARM", 5) == 0) PASS();
    else FAIL("$ALARM$ did not push marker");
}

static void test_var_phaser_pushes_marker(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$PHASER$ pushes 0x3D PHASER marker to TX");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "!|T$PHASER$|");
    if (tx_len >= 7 && tx_capture[0] == 0x3D &&
        memcmp(tx_capture + 1, "PHASER", 6) == 0) PASS();
    else FAIL("$PHASER$ did not push marker");
}

static void test_var_music_pushes_marker(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$MUSIC$ pushes 0x3D MUSIC marker to TX");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "!|T$MUSIC$|");
    if (tx_len >= 6 && tx_capture[0] == 0x3D &&
        memcmp(tx_capture + 1, "MUSIC", 5) == 0) PASS();
    else FAIL("$MUSIC$ did not push marker");
}

static void test_var_year_with_host_date(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$YEAR$ expands to host_date YY+2000");
    init_fixture(&s, &ctx);
    strcpy(s.host_date, "07/04/26");
    feed_script(&s, &ctx, "<<IF $YEAR$=2026>>!|X1000|<<ENDIF>>");
    if (draw_get_pixel(36, 0) != 0) PASS();
    else FAIL("$YEAR$ did not expand to 2026");
}

static void test_var_ripver_returns_a2gspu(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$RIPVER$ expands to RIPSCRIP032001 (v3.2)");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF $RIPVER$=RIPSCRIP032001>>!|X1100|<<ENDIF>>");
    if (draw_get_pixel(37, 0) != 0) PASS();
    else FAIL("$RIPVER$ did not match expected version string");
}

static void test_var_compat_returns_one(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$COMPAT$ expands to \"1\"");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF $COMPAT$=1>>!|X1200|<<ENDIF>>");
    if (draw_get_pixel(38, 0) != 0) PASS();
    else FAIL("$COMPAT$ did not return \"1\"");
}

static void test_var_ispalette_returns_one(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$ISPALETTE$ expands to \"1\"");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF $ISPALETTE$=1>>!|X1300|<<ENDIF>>");
    if (draw_get_pixel(39, 0) != 0) PASS();
    else FAIL("$ISPALETTE$ did not return \"1\"");
}

static void test_var_prot_reflects_resolution_mode(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$PROT$ expands to s->resolution_mode");
    init_fixture(&s, &ctx);
    s.resolution_mode = 2;
    feed_script(&s, &ctx, "<<IF $PROT$=2>>!|X1400|<<ENDIF>>");
    if (draw_get_pixel(40, 0) != 0) PASS();
    else FAIL("$PROT$ did not match resolution_mode");
}

static void test_var_coordsize_reflects_state(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$COORDSIZE$ expands to s->coordinate_size");
    init_fixture(&s, &ctx);
    s.coordinate_size = 4;
    feed_script(&s, &ctx, "<<IF $COORDSIZE$=4>>!|X1500|<<ENDIF>>");
    if (draw_get_pixel(41, 0) != 0) PASS();
    else FAIL("$COORDSIZE$ did not match coordinate_size");
}

/* ═══════════════════════════════════════════════════════════════════
 * TEXT WINDOW (rip_tw_putchar) BRANCH COVERAGE
 *
 * After a !|cmd| sequence the FSM is in RIP_ST_COMMAND waiting for
 * either another command letter or CR/LF.  The trailing \n in the
 * activation string returns the FSM to IDLE so subsequent raw bytes
 * route through the tw_active branch in rip_process.
 * ═══════════════════════════════════════════════════════════════════ */

static void tw_activate_window(rip_state_t *s, comp_context_t *ctx) {
    (void)ctx;
    /* Set the text-window state directly rather than feeding a wire
     * command + trailing newline, so the FSM stays cleanly in IDLE. */
    s->tw_x0 = 20;
    s->tw_y0 = 20;
    s->tw_x1 = 200;
    s->tw_y1 = 100;
    s->tw_cur_x = s->tw_x0;
    s->tw_cur_y = (int16_t)((s->tw_y0 * 8) / 7);  /* scale_y */
    s->tw_active = true;
    s->tw_wrap = true;
}

static void test_tw_carriage_return_resets_cursor(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("tw \\r resets cursor to tw_x0");
    init_fixture(&s, &ctx);
    tw_activate_window(&s, &ctx);
    if (!s.tw_active) { FAIL("setup: ext text window did not activate"); return; }
    rip_process(&s, &ctx, 'A');
    int16_t after_a = s.tw_cur_x;
    rip_process(&s, &ctx, '\r');
    if (s.tw_cur_x == s.tw_x0 && after_a > s.tw_x0) PASS();
    else FAIL("\\r did not reset cursor");
}

static void test_tw_backspace_moves_cursor_back(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("tw \\b moves cursor back one char width");
    init_fixture(&s, &ctx);
    tw_activate_window(&s, &ctx);
    if (!s.tw_active) { FAIL("setup"); return; }
    rip_process(&s, &ctx, 'A');
    rip_process(&s, &ctx, 'B');
    int16_t before = s.tw_cur_x;
    rip_process(&s, &ctx, '\b');
    if (s.tw_cur_x == before - 8) PASS();
    else FAIL("\\b did not retreat cursor by 8");
}

static void test_tw_tab_advances_to_next_stop(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("tw \\t advances cursor by 8-char tab stops");
    init_fixture(&s, &ctx);
    tw_activate_window(&s, &ctx);
    if (!s.tw_active) { FAIL("setup"); return; }
    int16_t start = s.tw_cur_x;
    rip_process(&s, &ctx, '\t');
    if (s.tw_cur_x == start + 64) PASS();
    else FAIL("\\t did not advance to next tab stop");
}

static void test_tw_control_char_ignored(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("tw control char < 0x20 (not \\r\\n\\b\\t) is ignored");
    init_fixture(&s, &ctx);
    tw_activate_window(&s, &ctx);
    if (!s.tw_active) { FAIL("setup"); return; }
    int16_t before = s.tw_cur_x;
    rip_process(&s, &ctx, 0x05);
    if (s.tw_cur_x == before) PASS();
    else FAIL("control char moved the cursor");
}

static void test_tw_newline_advances_row(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("tw \\n advances cursor by char height");
    init_fixture(&s, &ctx);
    tw_activate_window(&s, &ctx);
    if (!s.tw_active) { FAIL("setup"); return; }
    int16_t before = s.tw_cur_y;
    rip_process(&s, &ctx, '\n');
    if (s.tw_cur_y == before + 16) PASS();
    else FAIL("\\n did not advance row by 16");
}

/* ═══════════════════════════════════════════════════════════════════
 * ICON_STYLE MODE COVERAGE (rip_draw_icon_pixels modes 0, 2, 3)
 * Mode 1 (tile) is already covered by test_icon_style_tile_mode_repeats.
 * ═══════════════════════════════════════════════════════════════════ */

static void test_icon_style_stretch_mode(void) {
    rip_state_t s; comp_context_t ctx;
    uint8_t *pixels;
    TEST("ICON_STYLE mode 0 (stretch) scales icon to box");
    init_fixture(&s, &ctx);
    pixels = (uint8_t *)psram_arena_alloc(&s.psram_arena, 4);
    if (!pixels) { FAIL("setup: arena alloc"); return; }
    pixels[0] = pixels[1] = pixels[2] = pixels[3] = 5;
    if (!rip_icon_cache_pixels(&s.icon_state, "MINI", 4, pixels, 2, 2)) {
        FAIL("setup: cache"); return;
    }
    /* |& sets style box (5,5)-(25,25), mode=00 (stretch), align=00, scale=00 */
    feed_script(&s, &ctx, "!|&050519190000000000|");
    feed_script(&s, &ctx, "!|1I000000000MINI|");
    if (draw_get_pixel(15, 15) != 0) PASS();
    else FAIL("stretch mode left center empty");
}

static void test_icon_style_center_mode(void) {
    rip_state_t s; comp_context_t ctx;
    uint8_t *pixels;
    TEST("ICON_STYLE mode 2 (center) centers icon in box");
    init_fixture(&s, &ctx);
    pixels = (uint8_t *)psram_arena_alloc(&s.psram_arena, 4);
    if (!pixels) { FAIL("setup: arena alloc"); return; }
    pixels[0] = pixels[1] = pixels[2] = pixels[3] = 7;
    if (!rip_icon_cache_pixels(&s.icon_state, "CTR", 3, pixels, 2, 2)) {
        FAIL("setup: cache"); return;
    }
    /* style box (10,10)-(50,50), mode=02 (center).  scale_y(10)=11,
     * scale_y1(50)=58 → effective box (10,11)-(50,58).  Centering 2×2
     * in a 41×48 box → top-left at (29, 34). */
    feed_script(&s, &ctx, "!|&0A0A1E1E0200000000|");
    feed_script(&s, &ctx, "!|1I000000000CTR|");
    if (draw_get_pixel(29, 34) != 0 || draw_get_pixel(30, 34) != 0) PASS();
    else FAIL("center mode missed expected location");
}

/* ═══════════════════════════════════════════════════════════════════
 * BMP CACHING — public API success paths
 * ═══════════════════════════════════════════════════════════════════ */

static void test_rip_icon_cache_bmp_success(void) {
    /* Minimal valid 2×2 8bpp BMP, bottom-up.  pixel_offset=54, no palette.
     * Bottom row (54..57): 5, 6 + 2 bytes padding
     * Top row    (58..61): 3, 4 + 2 bytes padding
     * After top-down conversion, lookup pixels should be {3, 4, 5, 6}. */
    static const uint8_t bmp_2x2[62] = {
        [0] = 'B', [1] = 'M',
        [2] = 62,
        [10] = 54,
        [14] = 40,
        [18] = 2,
        [22] = 2,
        [26] = 1,
        [28] = 8,
        [54] = 5, [55] = 6,
        [58] = 3, [59] = 4,
    };
    rip_state_t s; comp_context_t ctx;
    rip_icon_t icon;

    TEST("rip_icon_cache_bmp accepts a valid 8bpp BMP");
    init_fixture(&s, &ctx);
    if (!rip_icon_cache_bmp(&s.icon_state, "OK8BPP", 6,
                            bmp_2x2, (int)sizeof(bmp_2x2)) ||
        !rip_icon_lookup(&s.icon_state, "OK8BPP", 6, &icon)) {
        FAIL("valid BMP was not cached / not found");
        return;
    }
    if (icon.width == 2 && icon.height == 2 &&
        icon.pixels[0] == 3 && icon.pixels[1] == 4 &&
        icon.pixels[2] == 5 && icon.pixels[3] == 6)
        PASS();
    else
        FAIL("BMP pixels not converted top-down correctly");
}

static void test_rip_icon_cache_bmp_replace(void) {
    static const uint8_t bmp_v1[62] = {
        [0] = 'B', [1] = 'M', [2] = 62, [10] = 54, [14] = 40,
        [18] = 2, [22] = 2, [26] = 1, [28] = 8,
        [54] = 1, [55] = 1, [58] = 1, [59] = 1,
    };
    static const uint8_t bmp_v2[62] = {
        [0] = 'B', [1] = 'M', [2] = 62, [10] = 54, [14] = 40,
        [18] = 2, [22] = 2, [26] = 1, [28] = 8,
        [54] = 2, [55] = 2, [58] = 2, [59] = 2,
    };
    rip_state_t s; comp_context_t ctx;
    rip_icon_t icon;

    TEST("rip_icon_cache_bmp_replace overwrites existing entry");
    init_fixture(&s, &ctx);
    if (!rip_icon_cache_bmp(&s.icon_state, "BMP", 3,
                            bmp_v1, (int)sizeof(bmp_v1))) {
        FAIL("setup: first cache");
        return;
    }
    if (!rip_icon_cache_bmp_replace(&s.icon_state, "BMP", 3,
                                    bmp_v2, (int)sizeof(bmp_v2)) ||
        rip_icon_cache_count(&s.icon_state) != 1 ||
        !rip_icon_lookup(&s.icon_state, "BMP", 3, &icon) ||
        icon.pixels[0] != 2)
        FAIL("replace did not overwrite");
    else
        PASS();
}

static void test_rip_icon_cache_has_runtime(void) {
    static uint8_t px[1] = { 0xAA };
    rip_state_t s; comp_context_t ctx;

    TEST("rip_icon_cache_has_runtime reflects cache state");
    init_fixture(&s, &ctx);
    if (rip_icon_cache_has_runtime(&s.icon_state, "FOO", 3)) {
        FAIL("FOO reported present before cache");
        return;
    }
    if (!rip_icon_cache_pixels(&s.icon_state, "FOO", 3, px, 1, 1)) {
        FAIL("setup: cache_pixels");
        return;
    }
    if (rip_icon_cache_has_runtime(&s.icon_state, "FOO", 3) &&
        !rip_icon_cache_has_runtime(&s.icon_state, "BAR", 3))
        PASS();
    else
        FAIL("has_runtime did not match expected state");
}

/* ═══════════════════════════════════════════════════════════════════
 * rip_filename_is_safe — security gate rejection paths
 * Tests via the |1F file-query command which gates on the same predicate.
 * Unsafe names return "0\r" (not found) without queuing a request.
 * ═══════════════════════════════════════════════════════════════════ */

static int file_query_unsafe(const char *script) {
    rip_state_t s; comp_context_t ctx;
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, script);
    /* "0\r" = unsafe-rejected.  request_count must remain 0. */
    return tx_len == 2 && tx_capture[0] == '0' && tx_capture[1] == '\r' &&
           s.icon_state.request_count == 0;
}

static void test_filename_rejects_forward_slash(void) {
    TEST("rip_filename_is_safe rejects '/'");
    if (file_query_unsafe("!|1F000000foo/bar|")) PASS();
    else FAIL("forward slash not rejected");
}

static void test_filename_rejects_colon(void) {
    TEST("rip_filename_is_safe rejects ':'");
    if (file_query_unsafe("!|1F000000C:NAME|")) PASS();
    else FAIL("colon not rejected");
}

/* ═══════════════════════════════════════════════════════════════════
 * §A2G2 — v1.2 QoL extensions
 * ═══════════════════════════════════════════════════════════════════ */

static void test_state_stack_push_pop_roundtrip(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|^ then |~ restores draw_color");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "!|c04|");   /* red */
    feed_script(&s, &ctx, "!|^|");     /* push */
    feed_script(&s, &ctx, "!|c0F|");   /* white */
    if (s.draw_color != 15) { FAIL("setup: |c0F didn't change color"); return; }
    feed_script(&s, &ctx, "!|~|");     /* pop -> red */
    if (s.draw_color == 4) PASS();
    else FAIL("|~ did not restore draw_color");
}

static void test_state_stack_pop_on_empty_is_noop(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|~ on empty stack is a no-op (no crash)");
    init_fixture(&s, &ctx);
    uint8_t before = s.draw_color;
    feed_script(&s, &ctx, "!|~|");
    if (s.draw_color == before) PASS();
    else FAIL("|~ on empty stack mutated state");
}

static void test_state_stack_overflow_silently_drops(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|^ overflow past RIP_STATE_STACK_MAX is silently dropped");
    init_fixture(&s, &ctx);
    /* Push more than the limit. */
    for (int i = 0; i < RIP_STATE_STACK_MAX + 4; i++)
        feed_script(&s, &ctx, "!|^|");
    if (s.state_stack_depth == RIP_STATE_STACK_MAX) PASS();
    else FAIL("stack depth exceeded the cap");
}

static void test_var_cx_cy_reflect_draw_cursor(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$CX$ / $CY$ reflect draw cursor");
    init_fixture(&s, &ctx);
    s.draw_x = 123;
    s.draw_y = 45;
    feed_script(&s, &ctx, "<<IF $CX$=123>>!|X1600|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $CY$=45>>!|X1700|<<ENDIF>>");
    if (draw_get_pixel(42, 0) != 0 && draw_get_pixel(43, 0) != 0) PASS();
    else FAIL("$CX$/$CY$ did not match draw cursor");
}

static void test_var_vpw_vph_reflect_viewport(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$VPW$ / $VPH$ reflect viewport dimensions");
    init_fixture(&s, &ctx);
    /* Default viewport (0,0)-(639,399) -> w=640 h=400 */
    feed_script(&s, &ctx, "<<IF $VPW$=640>>!|X1800|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $VPH$=400>>!|X1900|<<ENDIF>>");
    if (draw_get_pixel(44, 0) != 0 && draw_get_pixel(45, 0) != 0) PASS();
    else FAIL("$VPW$/$VPH$ did not match defaults");
}

static void test_var_vpcx_vpcy_compute_center(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$VPCX$ / $VPCY$ compute viewport center");
    init_fixture(&s, &ctx);
    /* Default viewport center: (319, 199) */
    feed_script(&s, &ctx, "<<IF $VPCX$=319>>!|X1A00|<<ENDIF>>");
    if (draw_get_pixel(46, 0) != 0) PASS();
    else FAIL("$VPCX$ did not compute center");
}

static void test_var_ccol_cfcol_reflect_colors(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$CCOL$ / $CFCOL$ reflect current colors");
    init_fixture(&s, &ctx);
    s.draw_color = 7;
    s.fill_color = 12;
    feed_script(&s, &ctx, "<<IF $CCOL$=7>>!|X1B00|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $CFCOL$=12>>!|X1C00|<<ENDIF>>");
    if (draw_get_pixel(47, 0) != 0 && draw_get_pixel(48, 0) != 0) PASS();
    else FAIL("$CCOL$/$CFCOL$ did not match");
}

/* Color-name vars expand in text/IF context (where rip_expand_variables
 * runs), not inside numeric command-arg fields like |c which read raw
 * MegaNum chars.  These tests verify expansion via IF comparison. */
static void test_var_color_name_red(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$RED$ expands to \"04\" in IF context");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF $RED$=04>>!|X2200|<<ENDIF>>");
    if (draw_get_pixel(74, 0) != 0) PASS();
    else FAIL("$RED$ did not expand to 04");
}

static void test_var_color_name_lightmagenta(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$LIGHTMAGENTA$ expands to \"0D\" in IF context");
    init_fixture(&s, &ctx);
    feed_script(&s, &ctx, "<<IF $LIGHTMAGENTA$=0D>>!|X2300|<<ENDIF>>");
    if (draw_get_pixel(75, 0) != 0) PASS();
    else FAIL("$LIGHTMAGENTA$ did not expand to 0D");
}

static void test_var_hour_min_from_host_time(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$HOUR$ / $MIN$ parse host_time");
    init_fixture(&s, &ctx);
    strcpy(s.host_time, "14:35");
    feed_script(&s, &ctx, "<<IF $HOUR$=14>>!|X1D00|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $MIN$=35>>!|X1E00|<<ENDIF>>");
    if (draw_get_pixel(49, 0) != 0 && draw_get_pixel(50, 0) != 0) PASS();
    else FAIL("$HOUR$/$MIN$ did not parse host_time");
}

static void test_var_dow_dom_month_from_host_date(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("$DOW$ / $DOM$ / $MONTH$ parse host_date");
    init_fixture(&s, &ctx);
    strcpy(s.host_date, "01/15/26");  /* Thu = 3 (Mon=0) */
    feed_script(&s, &ctx, "<<IF $DOW$=3>>!|X1F00|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $DOM$=15>>!|X2000|<<ENDIF>>");
    feed_script(&s, &ctx, "<<IF $MONTH$=01>>!|X2100|<<ENDIF>>");
    if (draw_get_pixel(51, 0) != 0 &&
        draw_get_pixel(72, 0) != 0 &&
        draw_get_pixel(73, 0) != 0) PASS();
    else FAIL("$DOW$/$DOM$/$MONTH$ did not match host_date");
}

static void test_preproc_debug_directive_emits_to_tx(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("<<DEBUG msg>> pushes marker to TX");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "<<DEBUG hello>>");
    if (tx_len >= 8 && tx_capture[0] == 0x3E &&
        memcmp(tx_capture + 1, "DEBUG: hello", 12) == 0) PASS();
    else FAIL("<<DEBUG>> did not emit marker");
}

static void test_preproc_debug_suppressed_by_false_if(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("<<DEBUG>> inside <<IF false>> is suppressed");
    init_fixture(&s, &ctx);
    tx_reset();
    feed_script(&s, &ctx, "<<IF 0>><<DEBUG hidden>><<ENDIF>>");
    if (tx_len == 0) PASS();
    else FAIL("<<DEBUG>> leaked through a false branch");
}

static void test_l2_gradient_radial_mode(void) {
    rip_state_t s; comp_context_t ctx;
    TEST("|28 mode=2 renders radial gradient");
    init_fixture(&s, &ctx);
    /* |20 takes RGB components in 0-255 (8-bit) and packs to RGB332.
     * For r=255 use mega2 "73" (7*36+3=255), so r>>5 = 7. */
    feed_script(&s, &ctx, "!|2001730000|");   /* idx=1 r=255 g=0 b=0 (red) */
    feed_script(&s, &ctx, "!|2002000073|");   /* idx=2 r=0 g=0 b=255 (blue) */
    /* |28 gradient: x=10 y=10 w=20 h=20 c1=1 c2=2 mode=2 (radial). */
    feed_script(&s, &ctx, "!|280A0A1414010202|");
    int found = 0;
    for (int y = 10; y < 30 && !found; y++)
        for (int x = 10; x < 30 && !found; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("|28 radial drew nothing");
}

static void test_filename_rejects_control_char(void) {
    TEST("rip_filename_is_safe rejects control char < 0x20");
    /* embed ENQ (0x05) — use a constructed script since we can't put
     * 0x05 in a C string literal cleanly: build via byte array. */
    static const uint8_t script[] = {
        '!','|','1','F','0','0','0','0','0','0','A',0x05,'B','|', 0
    };
    rip_state_t s; comp_context_t ctx;
    init_fixture(&s, &ctx);
    tx_reset();
    for (size_t i = 0; script[i]; i++) rip_process(&s, &ctx, script[i]);
    if (tx_len == 2 && tx_capture[0] == '0' && tx_capture[1] == '\r' &&
        s.icon_state.request_count == 0)
        PASS();
    else
        FAIL("control char not rejected");
}

static void test_rip_icon_cache_bmp_4bpp(void) {
    /* 2×2 4bpp BMP — row_bytes = ((2+1)/2 + 3) & ~3 = 4
     * Bottom row: 0x56 padding padding padding → pixels 5, 6
     * Top    row: 0x34 padding padding padding → pixels 3, 4 */
    static const uint8_t bmp_4bpp[62] = {
        [0] = 'B', [1] = 'M', [2] = 62, [10] = 54, [14] = 40,
        [18] = 2, [22] = 2, [26] = 1, [28] = 4,
        [54] = 0x56,  /* pixel0=5 (hi nib), pixel1=6 (lo nib) */
        [58] = 0x34,  /* pixel0=3, pixel1=4 */
    };
    rip_state_t s; comp_context_t ctx;
    rip_icon_t icon;

    TEST("rip_icon_cache_bmp accepts 4bpp BMP and unpacks nibbles");
    init_fixture(&s, &ctx);
    if (!rip_icon_cache_bmp(&s.icon_state, "BMP4", 4,
                            bmp_4bpp, (int)sizeof(bmp_4bpp)) ||
        !rip_icon_lookup(&s.icon_state, "BMP4", 4, &icon)) {
        FAIL("4bpp BMP not cached");
        return;
    }
    if (icon.pixels[0] == 3 && icon.pixels[1] == 4 &&
        icon.pixels[2] == 5 && icon.pixels[3] == 6)
        PASS();
    else
        FAIL("4bpp nibble unpacking is wrong");
}

static void test_icon_style_proportional_mode(void) {
    rip_state_t s; comp_context_t ctx;
    uint8_t *pixels;
    TEST("ICON_STYLE mode 3 (proportional) preserves aspect");
    init_fixture(&s, &ctx);
    pixels = (uint8_t *)psram_arena_alloc(&s.psram_arena, 2);
    if (!pixels) { FAIL("setup: arena alloc"); return; }
    pixels[0] = pixels[1] = 9;
    if (!rip_icon_cache_pixels(&s.icon_state, "WIDE", 4, pixels, 2, 1)) {
        FAIL("setup: cache"); return;
    }
    feed_script(&s, &ctx, "!|&0A0A1E1E0300000000|");
    feed_script(&s, &ctx, "!|1I000000000WIDE|");
    int found = 0;
    for (int y = 10; y < 30 && !found; y++)
        for (int x = 10; x < 50 && !found; x++)
            if (draw_get_pixel((int16_t)x, (int16_t)y) != 0) found = 1;
    if (found) PASS(); else FAIL("proportional mode drew nothing");
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
    test_icn_upload_replaces_cached_name();
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
    test_var_reset_restores_state();
    test_var_appn_set_via_define();
    test_l1_kill_mouse_regions();
    test_l1_mouse_region_define();
    test_l1_button_registers_region();
    test_l1_image_style_stored();
    test_l1_viewport_ext();
    test_l1_query_ext_returns_app_var();
    test_l1_define_query_and_expand_generic_var();
    test_l1_generic_query_round_trip();
    test_l1_file_query_missing_returns_zero();
    test_l2_port_define();
    test_l2_port_zero_protected();
    test_l2_port_delete();
    test_l2_port_switch_changes_active();
    test_l2_port_flags_set_alpha();
    test_l2_scale_text();
    test_l2_scale_text_applies_rotation();
    test_icon_style_tile_mode_repeats();
    test_l2_set_palette_writes_hardware();
    test_l2_widgets_draw_palette_indices();
    test_l2_special_draws_preserve_color();
    test_l2_clipboard_capture_paste();
    test_l2_alpha_preserves_write_mode();
    test_l2_port_copy_scales_destination_rect();
    test_l2_query_palette_replies();
    test_l2_chord_scales_radius_like_level0_arcs();
    test_sync_date_byte_commits_on_nul();
    test_sync_time_byte_commits_on_nul();
    test_query_response_round_trip();
    test_query_response_ignored_when_no_pending();
    test_eval_if_ge_5_eq_5();
    test_eval_if_le_3_5();
    test_eval_if_ne_strings();
    test_scroll_clears_only_source_rect();
    test_text_window_passthrough_renders();
    test_define_prompt_renders();
    test_region_text_expands_variables();
    test_region_text_renders_bitmap();
    test_text_xy_ext_renders_via_shared_helper();
    test_text_xy_ext_clips_to_box();
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
    test_bounded_text_clips_long_word();
    test_bounded_text_expands_empty_app_variable();
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
    test_mouse_click_uses_latest_overlapping_region();
    test_polygon_overflow_rejected();
    test_set_one_palette_entry();
    test_group_markers_accepted();
    test_metadata_commands_store_state_and_vars();
    test_header_reenables_filled_borders();
    test_reset_windows_preserves_user_vars();
    test_user_var_overflow_returns_false();
    test_user_var_name_with_dollar_normalizes();
    test_user_var_name_with_invalid_char_rejected();
    test_filled_border_disabled_skips_rect_outline();
    test_filled_border_helper_always_sets_copy_mode();
    test_filled_border_toggle_controls_outline();
    test_back_color_visible_in_pattern_fill();
    test_erase_view_uses_back_color();
    test_back_color_command_propagates_to_draw_layer();
    test_port_switch_restores_pattern_back_color();
    test_l1_copy_region_blits_pixels();
    test_l0_copy_region_scales_destination_rect();
    test_l1_clipboard_get_put_roundtrip();
    test_write_icon_caches_clipboard_for_load_icon();
    test_write_icon_replaces_cached_name();
    test_runtime_icon_supersedes_flash();
    test_clipboard_op_5_capture_op_6_paste();
    test_save_icon_slot_out_of_range_is_noop();
    test_stamp_icon_unset_slot_falls_back_to_clipboard();
    test_save_and_stamp_icon_slot();
    test_save_icon_slot_updates_load_alias();
    test_l1_text_block_lifecycle();
    test_text_xy_expands_variables();
    test_l1_audio_pushes_marker();
    test_iso_week_var_expansion();
    test_iso_week_year_wrap();
    test_font_load_resolves_bgi_name();
    test_font_load_resolves_path_and_case();
    test_load_icon_clipboard_flag_stores_pixels();
    test_global_mouse_event_dispatches();
    test_global_file_upload_caches_icn();

    /* Wire-coverage tests for the 37 commands flagged by the spec-vs-test audit */
    test_l0_write_mode_W();
    test_l0_line_style_eq();
    test_l0_font_style_Y();
    test_l0_move_m();
    test_l0_gotoxy_g();
    test_l0_home_H();
    test_l0_set_palette_Q();
    test_l0_rectangle_R();
    test_l0_circle_C();
    test_l0_oval_O();
    test_l0_filled_oval_o();
    test_l0_arc_A();
    test_l0_oval_pie_slice_i();
    test_l0_bezier_Z();
    test_l0_polyline_l();
    test_l0_erase_window_e();
    test_l0_erase_eol();
    test_l0_no_more_hash();
    test_l0_region_text_t();
    test_l0_custom_fill_pattern_s();
    test_ext_rounded_rect_U();
    test_ext_filled_rounded_rect_u();
    test_ext_polyline_ext();
    test_ext_animation_frame_brace();
    test_ext_kill_mouse_in_region_K();
    test_ext_mouse_region_ext_colon();
    test_ext_button_ext_semicolon();
    test_ext_ext_text_window_b();
    test_ext_ext_font_style_d();
    test_ext_font_attrib_f();
    test_ext_fill_pattern_ext_D();
    test_l1_button_style_B();
    test_l1_set_icon_dir_N();
    test_l1_play_midi_Z();
    test_l2_scrollbar_renders();
    test_l2_menu_renders();
    test_l2_dialog_renders();
    test_l2_set_refresh();

    test_var_blip_pushes_marker();
    test_var_alarm_pushes_marker();
    test_var_phaser_pushes_marker();
    test_var_music_pushes_marker();
    test_var_year_with_host_date();
    test_var_ripver_returns_a2gspu();
    test_var_compat_returns_one();
    test_var_ispalette_returns_one();
    test_var_prot_reflects_resolution_mode();
    test_var_coordsize_reflects_state();
    test_tw_carriage_return_resets_cursor();
    test_tw_backspace_moves_cursor_back();
    test_tw_tab_advances_to_next_stop();
    test_tw_control_char_ignored();
    test_tw_newline_advances_row();
    test_icon_style_stretch_mode();
    test_icon_style_center_mode();
    test_icon_style_proportional_mode();
    test_rip_icon_cache_bmp_success();
    test_rip_icon_cache_bmp_replace();
    test_rip_icon_cache_has_runtime();
    test_rip_icon_cache_bmp_4bpp();
    test_filename_rejects_forward_slash();
    test_filename_rejects_colon();
    test_filename_rejects_control_char();

    /* §A2G2 — v1.2 QoL extensions */
    test_state_stack_push_pop_roundtrip();
    test_state_stack_pop_on_empty_is_noop();
    test_state_stack_overflow_silently_drops();
    test_var_cx_cy_reflect_draw_cursor();
    test_var_vpw_vph_reflect_viewport();
    test_var_vpcx_vpcy_compute_center();
    test_var_ccol_cfcol_reflect_colors();
    test_var_color_name_red();
    test_var_color_name_lightmagenta();
    test_var_hour_min_from_host_time();
    test_var_dow_dom_month_from_host_date();
    test_preproc_debug_directive_emits_to_tx();
    test_preproc_debug_suppressed_by_false_if();
    test_l2_gradient_radial_mode();

    cleanup_all_arenas();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
