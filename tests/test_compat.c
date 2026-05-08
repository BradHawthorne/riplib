/*
 * test_compat.c — Replay-based compatibility fixtures for RIPlib
 *
 * Replays fixture byte streams through rip_process(), captures outbound
 * host responses, and compares framebuffer + TX output against checked-in
 * expectations. This is the safety net for compatibility-first refactors.
 */

#include "drawing.h"
#include "ripscrip.h"
#include "compat_fixture_path.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef COMPAT_FIXTURE_DIR
#error "COMPAT_FIXTURE_DIR must be defined"
#endif

#define W 640
#define H 400
#define TX_CAPTURE_MAX 8192

static uint8_t fb[W * H];
static uint16_t palette[256];
static uint8_t tx_capture[TX_CAPTURE_MAX];
static size_t tx_len = 0;
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-40s ", name); \
} while (0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

typedef struct {
    uint64_t frame_hash;
    uint8_t tx_bytes[TX_CAPTURE_MAX];
    size_t tx_len;
    int has_frame_hash;
} compat_expect_t;

void palette_write_rgb565(uint8_t i, uint16_t v) { palette[i] = v; }
uint16_t palette_read_rgb565(uint8_t i) { return palette[i]; }
void *gpu_psram_alloc(uint32_t size) { return malloc(size); }

void card_tx_push(const char *buf, int len) {
    size_t n;

    if (!buf || len <= 0 || tx_len >= TX_CAPTURE_MAX)
        return;
    n = (size_t)len;
    if (n > TX_CAPTURE_MAX - tx_len)
        n = TX_CAPTURE_MAX - tx_len;
    memcpy(tx_capture + tx_len, buf, n);
    tx_len += n;
}

static void init_fixture(rip_state_t *s, comp_context_t *ctx) {
    memset(fb, 0, sizeof(fb));
    memset(palette, 0, sizeof(palette));
    memset(tx_capture, 0, sizeof(tx_capture));
    tx_len = 0;
    memset(s, 0, sizeof(*s));
    memset(ctx, 0, sizeof(*ctx));
    ctx->target = fb;
    draw_init(fb, W, W, H);
    rip_init_first(s);
}

static void cleanup_fixture(rip_state_t *s) {
    if (s) psram_arena_destroy(&s->psram_arena);
}

static uint64_t fnv1a64(const uint8_t *data, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static int hex_digit_value(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static size_t hex_encode(const uint8_t *data, size_t len, char *out, size_t out_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;

    if (out_size == 0)
        return 0;
    for (size_t i = 0; i < len && o + 2 < out_size; i++) {
        out[o++] = hex[data[i] >> 4];
        out[o++] = hex[data[i] & 0x0F];
    }
    out[o] = '\0';
    return o;
}

static int load_file_bytes(const char *path, uint8_t **out_data, size_t *out_size) {
    FILE *f;
    long file_size;
    uint8_t *buf;
    size_t got;

    *out_data = NULL;
    *out_size = 0;

#if defined(_MSC_VER)
    if (fopen_s(&f, path, "rb") != 0)
        f = NULL;
#else
    f = fopen(path, "rb");
#endif
    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    buf = (uint8_t *)malloc((size_t)file_size + 1u);
    if (!buf) {
        fclose(f);
        return 0;
    }

    got = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    if (got != (size_t)file_size) {
        free(buf);
        return 0;
    }

    *out_data = buf;
    *out_size = got;
    return 1;
}

static char *trim_in_place(char *s) {
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int parse_hex_bytes(const char *value, uint8_t *out, size_t *out_len) {
    size_t len = 0;

    while (*value != '\0') {
        int hi;
        int lo;

        while (*value != '\0' && isspace((unsigned char)*value))
            value++;
        if (*value == '\0')
            break;
        hi = hex_digit_value((unsigned char)value[0]);
        lo = hex_digit_value((unsigned char)value[1]);
        if (hi < 0 || lo < 0)
            return 0;
        if (len >= TX_CAPTURE_MAX)
            return 0;
        out[len++] = (uint8_t)((hi << 4) | lo);
        value += 2;
    }

    *out_len = len;
    return 1;
}

static int load_expectations(const char *path, compat_expect_t *expect) {
    uint8_t *text;
    size_t size;
    char *cursor;
    int ok = 1;

    memset(expect, 0, sizeof(*expect));
    if (!load_file_bytes(path, &text, &size))
        return 0;

    text[size] = '\0';
    cursor = (char *)text;
    while (*cursor != '\0') {
        char *line = cursor;
        char *next = strchr(cursor, '\n');
        char *eq;
        char *key;
        char *value;

        if (next) {
            *next = '\0';
            cursor = next + 1;
        } else {
            cursor += strlen(cursor);
        }

        key = trim_in_place(line);
        if (*key == '\0' || *key == '#')
            continue;

        eq = strchr(key, '=');
        if (!eq) {
            ok = 0;
            break;
        }
        *eq = '\0';
        value = trim_in_place(eq + 1);
        key = trim_in_place(key);

        if (strcmp(key, "frame_hash") == 0) {
            unsigned long long parsed = strtoull(value, NULL, 0);
            expect->frame_hash = (uint64_t)parsed;
            expect->has_frame_hash = 1;
        } else if (strcmp(key, "tx_hex") == 0) {
            if (!parse_hex_bytes(value, expect->tx_bytes, &expect->tx_len)) {
                ok = 0;
                break;
            }
        }
    }

    free(text);
    return ok && expect->has_frame_hash;
}

static int run_case(const char *name) {
    char script_path[512];
    char expect_path[512];
    uint8_t *script = NULL;
    size_t script_len = 0;
    compat_expect_t expect;
    rip_state_t s;
    comp_context_t ctx;
    uint64_t actual_frame_hash;
    char actual_tx_hex[TX_CAPTURE_MAX * 2 + 1];

    snprintf(script_path, sizeof(script_path), "%s/%s.rip", COMPAT_FIXTURE_DIR, name);
    snprintf(expect_path, sizeof(expect_path), "%s/%s.expect", COMPAT_FIXTURE_DIR, name);

    TEST(name);
    if (!load_file_bytes(script_path, &script, &script_len)) {
        FAIL("could not load script fixture");
        return 0;
    }
    if (!load_expectations(expect_path, &expect)) {
        free(script);
        FAIL("could not load expectation file");
        return 0;
    }

    init_fixture(&s, &ctx);
    for (size_t i = 0; i < script_len; i++)
        rip_process(&s, &ctx, script[i]);

    actual_frame_hash = fnv1a64(fb, sizeof(fb));
    if (actual_frame_hash != expect.frame_hash) {
        printf("FAIL: frame hash mismatch (expected 0x%016llX got 0x%016llX)\n",
               (unsigned long long)expect.frame_hash,
               (unsigned long long)actual_frame_hash);
        free(script);
        cleanup_fixture(&s);
        return 0;
    }

    if (tx_len != expect.tx_len || memcmp(tx_capture, expect.tx_bytes, tx_len) != 0) {
        hex_encode(tx_capture, tx_len, actual_tx_hex, sizeof(actual_tx_hex));
        printf("FAIL: tx mismatch (expected %zu bytes got %zu, actual hex %s)\n",
               expect.tx_len, tx_len, actual_tx_hex);
        free(script);
        cleanup_fixture(&s);
        return 0;
    }

    PASS();
    free(script);
    cleanup_fixture(&s);
    return 1;
}

int main(void) {
    static const char *cases[] = {
        "legacy_draw_scene",
        "missing_file_query",
        "fill_and_shapes",
        "palette_pixels",
        "erase_and_reset",
    };

    printf("RIPlib v1.0 — Compatibility Fixture Tests\n");
    printf("=========================================\n\n");

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        (void)run_case(cases[i]);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
