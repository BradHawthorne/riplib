/*
 * rip_clipboard.c — RIPlib clipboard + raster blit operations.
 *
 * Implements the API declared in rip_clipboard.h.  See that header
 * for module scope and extraction rationale (audit C-002 step 6).
 *
 * Copyright (c) 2026 SimVU (Brad Hawthorne)
 * Licensed under the MIT License.  See LICENSE.
 */

#include "rip_clipboard.h"
#include "rip_internal.h"
#include "drawing.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool rip_clipboard_alloc(rip_state_t *s) {
    if (!s)
        return false;
    if (!s->clipboard.data) {
        s->clipboard.data = (uint8_t *)psram_arena_alloc(&s->psram_arena,
                                                         RIP_CLIPBOARD_MAX);
    }
    return s->clipboard.data != NULL;
}

bool rip_clipboard_store_pixels(rip_state_t *s,
                                       const uint8_t *pixels,
                                       uint16_t width,
                                       uint16_t height) {
    size_t bytes;

    if (!s || !pixels || width == 0 || height == 0 ||
        width > INT16_MAX || height > INT16_MAX)
        return false;

    bytes = (size_t)width * (size_t)height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;
    if (!rip_clipboard_alloc(s))
        return false;

    memmove(s->clipboard.data, pixels, bytes);
    s->clipboard.width = (int16_t)width;
    s->clipboard.height = (int16_t)height;
    s->clipboard.valid = true;
    return true;
}

bool rip_clipboard_capture(rip_state_t *s,
                                  int16_t x, int16_t y,
                                  int16_t width, int16_t height) {
    size_t bytes;

    if (!s || width <= 0 || height <= 0)
        return false;

    bytes = (size_t)(uint16_t)width * (size_t)(uint16_t)height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;
    if (!rip_clipboard_alloc(s))
        return false;

    /* draw_save_region preserves cells outside the framebuffer. A new
     * capture must not inherit those cells from an earlier clipboard. */
    memset(s->clipboard.data, 0, bytes);
    draw_save_region(x, y, width, height, s->clipboard.data);
    s->clipboard.width = width;
    s->clipboard.height = height;
    s->clipboard.valid = true;
    return true;
}

static bool rip_gdi_copy_samples(int32_t sw, int32_t sh, int32_t dw, int32_t dh) {
    /* The native shortcut requires BOTH dimensions to differ by at most one. */
    return dw - sw >= -1 && dw - sw <= 1 && dh - sh >= -1 && dh - sh <= 1;
}

uint16_t rip_gdi_sample(uint16_t position, uint16_t source, uint16_t destination,
                        bool copy_samples) {
    if (!source || !destination) return 0;
    uint32_t sample = copy_samples ? position
        : ((uint32_t)position * source + source / 2u) / destination;
    return (uint16_t)(sample < source ? sample : source - 1u);
}

bool rip_clipboard_capture_icon(rip_state_t *s, const rip_image_rect_t *rect) {
    if (!s || !rect || rect->width <= 0 || rect->height <= 0 ||
        rect->width == INT16_MAX || rect->height == INT16_MAX)
        return false;
    int16_t width = (int16_t)(rect->width + 1), height = (int16_t)(rect->height + 1);
    size_t bytes = (size_t)width * (size_t)height;
    if (bytes > RIP_CLIPBOARD_MAX || !rip_clipboard_alloc(s)) return false;
    memset(s->clipboard.data, 0, bytes);
    /* PortCopy trims the right/bottom source edges without reducing its
     * destination allocation. Left/top source coordinates remain intact;
     * draw_get_pixel supplies zero for positions outside the framebuffer. */
    int32_t sw = rect->width, sh = rect->height;
    int32_t right = (int32_t)draw_get_clip_x1() + 1;
    int32_t bottom = (int32_t)draw_get_clip_y1() + 1;
    if ((int32_t)rect->x + sw > right) sw = right - rect->x;
    if ((int32_t)rect->y + sh > bottom) sh = bottom - rect->y;
    bool copy_samples = rip_gdi_copy_samples(sw, sh, width, height);
    if (sw > 0 && sh > 0 &&
        (int32_t)rect->x + sw >= draw_get_clip_x0() &&
        (int32_t)rect->y + sh >= draw_get_clip_y0()) {
        if (copy_samples && sw == rect->width && sh == rect->height &&
            rect->x >= 0 && rect->y >= 0) {
            /* The common N->N+1 capture is row copies plus repeated edges;
             * avoid resampling every pixel (especially on PSRAM targets). */
            for (int16_t y = 0; y < rect->height; y++) {
                uint8_t *row = s->clipboard.data + (size_t)y * width;
                draw_save_region(rect->x, (int16_t)(rect->y + y), rect->width, 1, row);
                row[rect->width] = row[rect->width - 1];
            }
            memcpy(s->clipboard.data + (size_t)rect->height * width,
                   s->clipboard.data + (size_t)(rect->height - 1) * width, (size_t)width);
        } else {
            for (int16_t y = 0; y < height; y++) {
                int16_t sy = (int16_t)(rect->y + rip_gdi_sample(
                    (uint16_t)y, (uint16_t)sh, (uint16_t)height, copy_samples));
                for (int16_t x = 0; x < width; x++) {
                    int16_t sx = (int16_t)(rect->x + rip_gdi_sample(
                        (uint16_t)x, (uint16_t)sw, (uint16_t)width, copy_samples));
                    s->clipboard.data[(size_t)y * width + x] = draw_get_pixel(sx, sy);
                }
            }
        }
    }
    s->clipboard.width = width;
    s->clipboard.height = height;
    s->clipboard.valid = true;
    return true;
}

bool rip_cache_clipboard_as_icon(rip_state_t *s,
                                        const char *name,
                                        int name_len,
                                        rip_icon_t *out_icon) {
    size_t bytes;
    uint8_t *pixels;

    if (!s || !s->clipboard.valid || !s->clipboard.data ||
        !rip_filename_is_safe(name, name_len))
        return false;

    bytes = (size_t)(uint16_t)s->clipboard.width *
            (size_t)(uint16_t)s->clipboard.height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;

    pixels = (uint8_t *)psram_arena_alloc(&s->psram_arena, (uint32_t)bytes);
    if (!pixels)
        return false;

    memcpy(pixels, s->clipboard.data, bytes);
    if (!rip_icon_cache_pixels_replace(&s->icon_state, name, name_len, pixels,
                                       (uint16_t)s->clipboard.width,
                                       (uint16_t)s->clipboard.height))
        return false;

    if (out_icon) {
        out_icon->pixels = pixels;
        out_icon->width = (uint16_t)s->clipboard.width;
        out_icon->height = (uint16_t)s->clipboard.height;
    }
    return true;
}

bool rip_save_clipboard_slot(rip_state_t *s, uint16_t slot) {
    size_t bytes;
    uint8_t *pixels;
    char slot_name[RIP_ICON_NAME_MAX + 1];

    if (!s || slot >= RIP_ICON_SLOT_MAX ||
        !s->clipboard.valid || !s->clipboard.data)
        return false;

    bytes = (size_t)(uint16_t)s->clipboard.width *
            (size_t)(uint16_t)s->clipboard.height;
    if (bytes == 0 || bytes > RIP_CLIPBOARD_MAX)
        return false;

    pixels = (uint8_t *)psram_arena_alloc(&s->psram_arena, (uint32_t)bytes);
    if (!pixels)
        return false;
    memcpy(pixels, s->clipboard.data, bytes);

    s->icon_slots[slot].pixels = pixels;
    s->icon_slots[slot].width = (uint16_t)s->clipboard.width;
    s->icon_slots[slot].height = (uint16_t)s->clipboard.height;
    s->icon_slot_valid[slot] = true;

    snprintf(slot_name, sizeof(slot_name), "SLOT%02u", (unsigned)slot);
    (void)rip_icon_cache_pixels_replace(&s->icon_state, slot_name,
                                        (int)strlen(slot_name),
                                        pixels,
                                        (uint16_t)s->clipboard.width,
                                        (uint16_t)s->clipboard.height);
    return true;
}

static void rip_blit_pixels_sampled(rip_state_t *s,
                            int16_t dx, int16_t dy,
                            const uint8_t *pixels,
                            uint16_t src_w, uint16_t src_h,
                            int16_t dst_w, int16_t dst_h,
                            uint8_t write_mode, bool gdi_sampling) {
    uint8_t old_color;
    bool copy_samples = rip_gdi_copy_samples(src_w, src_h, dst_w, dst_h);

    if (!pixels || src_w == 0 || src_h == 0 || dst_w <= 0 || dst_h <= 0)
        return;
    if (write_mode > DRAW_MODE_NOT)
        write_mode = DRAW_MODE_COPY;

    old_color = draw_get_color();
    /* Image mode 4 is NOTSRCCOPY. The generic drawing mode with the same
     * number is destination inversion, so copy the complemented source. */
    draw_set_write_mode(write_mode == DRAW_MODE_NOT ? DRAW_MODE_COPY : write_mode);

    if (dst_w == (int16_t)src_w && dst_h == (int16_t)src_h &&
        write_mode != DRAW_MODE_NOT) {
        draw_restore_region(dx, dy, dst_w, dst_h, pixels);
    } else {
        for (int16_t yy = 0; yy < dst_h; yy++) {
            uint16_t sy = gdi_sampling ? rip_gdi_sample((uint16_t)yy, src_h, (uint16_t)dst_h, copy_samples)
                : (uint16_t)(((uint32_t)(uint16_t)yy * src_h) / (uint16_t)dst_h);
            for (int16_t xx = 0; xx < dst_w; xx++) {
                uint16_t sx = gdi_sampling ? rip_gdi_sample((uint16_t)xx, src_w, (uint16_t)dst_w, copy_samples)
                    : (uint16_t)(((uint32_t)(uint16_t)xx * src_w) / (uint16_t)dst_w);
                uint8_t pixel = pixels[(size_t)sy * src_w + sx];
                draw_set_color(write_mode == DRAW_MODE_NOT ? (uint8_t)~pixel : pixel);
                draw_pixel((int16_t)(dx + xx), (int16_t)(dy + yy));
            }
        }
    }

    draw_set_write_mode(s ? s->write_mode : DRAW_MODE_COPY);
    draw_set_color(old_color);
}

void rip_blit_pixels(rip_state_t *s, int16_t x, int16_t y,
                     const uint8_t *pixels, uint16_t sw, uint16_t sh,
                     int16_t dw, int16_t dh, uint8_t mode) {
    rip_blit_pixels_sampled(s, x, y, pixels, sw, sh, dw, dh, mode, false);
}

void rip_blit_pixels_gdi(rip_state_t *s, int16_t x, int16_t y,
                         const uint8_t *pixels, uint16_t sw, uint16_t sh,
                         int16_t dw, int16_t dh, uint8_t mode) {
    rip_blit_pixels_sampled(s, x, y, pixels, sw, sh, dw, dh, mode, true);
}

void rip_blit_pixels_tiled(rip_state_t *s,
                                  int16_t x0, int16_t y0,
                                  int16_t x1, int16_t y1,
                                  const uint8_t *pixels,
                                  uint16_t src_w, uint16_t src_h,
                                  uint8_t write_mode) {
    draw_clip_state_t saved_clip;
    int16_t cx0, cy0, cx1, cy1;

    if (!pixels || src_w == 0 || src_h == 0 || x1 < x0 || y1 < y0)
        return;

    draw_save_clip(&saved_clip);
    cx0 = x0 > saved_clip.x0 ? x0 : saved_clip.x0;
    cy0 = y0 > saved_clip.y0 ? y0 : saved_clip.y0;
    cx1 = x1 < saved_clip.x1 ? x1 : saved_clip.x1;
    cy1 = y1 < saved_clip.y1 ? y1 : saved_clip.y1;
    if (cx0 > cx1 || cy0 > cy1) return;
    draw_set_clip(cx0, cy0, cx1, cy1);
    /* Skip invisible tiles while preserving the original tile phase. Wide
     * counters also prevent the final increment from wrapping at 32767. */
    int32_t first_x = x0 + ((int32_t)cx0 - x0) / src_w * src_w;
    int32_t first_y = y0 + ((int32_t)cy0 - y0) / src_h * src_h;
    for (int32_t y = first_y; y <= cy1; y += src_h) {
        for (int32_t x = first_x; x <= cx1; x += src_w) {
            rip_blit_pixels(s, (int16_t)x, (int16_t)y, pixels, src_w, src_h,
                            (int16_t)src_w, (int16_t)src_h, write_mode);
        }
    }
    draw_restore_clip(&saved_clip);
}

void rip_draw_icon_pixels(rip_state_t *s,
                                 int16_t x, int16_t y,
                                 const uint8_t *pixels,
                                 uint16_t src_w, uint16_t src_h,
                                 int16_t requested_w,
                                 int16_t requested_h,
                                 uint8_t write_mode, rip_image_rect_t *rendered) {
    bool has_box;
    int16_t bx0;
    int16_t by0;
    int16_t bx1;
    int16_t by1;
    int16_t dst_w;
    int16_t dst_h;
    uint8_t mode;

    if (rendered) *rendered = (rip_image_rect_t){0, 0, 0, 0};
    if (!s || !pixels || src_w == 0 || src_h == 0 ||
        src_w > INT16_MAX || src_h > INT16_MAX)
        return;

    has_box = (requested_w > 0 && requested_h > 0);
    if (has_box) {
        bx0 = x;
        by0 = y;
        bx1 = (int16_t)(x + requested_w - 1);
        by1 = (int16_t)(y + requested_h - 1);
    } else if (s->icon_style_active) {
        bx0 = s->icon_style_x0;
        by0 = s->icon_style_y0;
        bx1 = s->icon_style_x1;
        by1 = s->icon_style_y1;
    } else {
        if (rendered) *rendered = (rip_image_rect_t){x, y, (int16_t)src_w, (int16_t)src_h};
        rip_blit_pixels(s, x, y, pixels, src_w, src_h,
                        (int16_t)src_w, (int16_t)src_h, write_mode);
        return;
    }

    if (bx0 > bx1) { int16_t t = bx0; bx0 = bx1; bx1 = t; }
    if (by0 > by1) { int16_t t = by0; by0 = by1; by1 = t; }
    dst_w = (int16_t)(bx1 - bx0 + 1);
    dst_h = (int16_t)(by1 - by0 + 1);
    if (dst_w <= 0 || dst_h <= 0)
        return;

    mode = s->icon_style_active ? (uint8_t)(s->icon_style_style & 0x03u)
                                : (uint8_t)(s->image_style & 0x03u);

    if (mode == 1) {
        if (rendered) *rendered = (rip_image_rect_t){bx0, by0, dst_w, dst_h};
        rip_blit_pixels_tiled(s, bx0, by0, bx1, by1, pixels, src_w, src_h,
                              write_mode);
        return;
    }

    if (mode == 2) {
        dst_w = (int16_t)src_w;
        dst_h = (int16_t)src_h;
        if ((s->icon_style_align & 0x03u) == 2u) {
            bx0 = (int16_t)(bx1 - dst_w + 1);
            by0 = (int16_t)(by1 - dst_h + 1);
        } else {
            bx0 = (int16_t)(bx0 + ((bx1 - bx0 + 1) - dst_w) / 2);
            by0 = (int16_t)(by0 + ((by1 - by0 + 1) - dst_h) / 2);
        }
    } else if (mode == 3) {
        int32_t w_fit = (int32_t)(bx1 - bx0 + 1);
        int32_t h_fit = ((int32_t)w_fit * src_h) / src_w;
        if (h_fit > (int32_t)(by1 - by0 + 1)) {
            h_fit = (int32_t)(by1 - by0 + 1);
            w_fit = ((int32_t)h_fit * src_w) / src_h;
        }
        if (w_fit <= 0) w_fit = 1;
        if (h_fit <= 0) h_fit = 1;
        dst_w = (int16_t)w_fit;
        dst_h = (int16_t)h_fit;
        bx0 = (int16_t)(bx0 + ((bx1 - bx0 + 1) - dst_w) / 2);
        by0 = (int16_t)(by0 + ((by1 - by0 + 1) - dst_h) / 2);
    }

    if (rendered) *rendered = (rip_image_rect_t){bx0, by0, dst_w, dst_h};
    rip_blit_pixels(s, bx0, by0, pixels, src_w, src_h, dst_w, dst_h,
                    write_mode);
}
