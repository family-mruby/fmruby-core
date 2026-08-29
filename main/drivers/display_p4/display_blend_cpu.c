/*
 * display_blend_cpu.c - software canvas compositing core
 *
 * Plain C with nothing ESP-IDF or LovyanGFX in it: the same file compiles
 * behind the ESP32-P4 CPU backend (display_backend_cpu.cpp) and, later, the
 * wasm target (doc/wasm/). It implements exactly the operation the PPA Blend
 * is configured for in display_backend_ppa.cpp: place one already-clipped
 * RGB565 block over the framebuffer, fully opaque, optionally leaving alone
 * the pixels whose colour falls inside the colour-key band so the background
 * shows through.
 *
 * Colour-key equivalence with the PPA (what the tiles baseline checks): the
 * caller builds the band from a 565 colour as [c << k, (c << k) | mask]
 * (canvas creation in display_p4_task.cpp), so a foreground pixel is inside
 * the band exactly when its 565 components equal the key's -- whether the
 * 8-bit expansion pads with zeros (as here) or replicates high bits. Bounds
 * are inclusive on both ends.
 */

#include "display_backend.h"

#include <string.h>

void display_blend_cpu_block(const display_blend_req_t *r)
{
    const uint16_t *fg = (const uint16_t *)r->fg;
    uint16_t       *bg = (uint16_t *)r->bg;

    if (!r->color_key) {
        for (int y = 0; y < r->h; y++) {
            const uint16_t *src = fg + (size_t)(r->src_y + y) * r->fg_pic_w + r->src_x;
            uint16_t       *dst = bg + (size_t)(r->dst_y + y) * r->bg_pic_w + r->dst_x;
            memcpy(dst, src, (size_t)r->w * 2);
        }
        return;
    }

    for (int y = 0; y < r->h; y++) {
        const uint16_t *src = fg + (size_t)(r->src_y + y) * r->fg_pic_w + r->src_x;
        uint16_t       *dst = bg + (size_t)(r->dst_y + y) * r->bg_pic_w + r->dst_x;
        for (int x = 0; x < r->w; x++) {
            uint16_t px = src[x];
            uint8_t r8 = (uint8_t)(((px >> 11) & 0x1f) << 3);
            uint8_t g8 = (uint8_t)(((px >> 5) & 0x3f) << 2);
            uint8_t b8 = (uint8_t)((px & 0x1f) << 3);
            if (r8 >= r->ck_r_low && r8 <= r->ck_r_high &&
                g8 >= r->ck_g_low && g8 <= r->ck_g_high &&
                b8 >= r->ck_b_low && b8 <= r->ck_b_high) {
                continue;
            }
            dst[x] = px;
        }
    }
}
