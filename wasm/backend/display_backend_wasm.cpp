/*
 * display_backend_wasm.cpp - the wasm output path behind the P3 backend
 * interface (doc/wasm/ P4a-P4b).
 *
 * Compositing is the shared software core (display_blend_cpu.c, the same file
 * the P4 CPU backend runs on the device). "Presenting" converts the finished
 * RGB565 framebuffer into a stable RGBA8888 buffer and bumps a frame counter:
 * the browser's main thread polls the counter from requestAnimationFrame and
 * putImageData()s the buffer straight out of wasm memory (P4b). Under node
 * nothing reads it, which is fine -- the frame is kept, as P4a asks.
 */

#include "display_backend.h"

#include "fmrb_log.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "display_wasm";

static uint8_t *s_rgba = nullptr;      /* fb_w * fb_h * 4, stable address */
static int s_fb_w = 0;
static int s_fb_h = 0;
static volatile uint32_t s_frame_seq = 0;

/* ---- the JS-facing surface (P4b polls these) ---------------------------- */

extern "C" {

EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_frame_seq(void) { return s_frame_seq; }
EMSCRIPTEN_KEEPALIVE const uint8_t *fmrb_wasm_frame_rgba(void) { return s_rgba; }
EMSCRIPTEN_KEEPALIVE int fmrb_wasm_frame_width(void) { return s_fb_w; }
EMSCRIPTEN_KEEPALIVE int fmrb_wasm_frame_height(void) { return s_fb_h; }

}

/* ------------------------------------------------------------------------ */

static void wasm_init(int fb_w, int fb_h)
{
    s_fb_w = fb_w;
    s_fb_h = fb_h;
    free(s_rgba);
    s_rgba = (uint8_t *)calloc((size_t)fb_w * fb_h, 4);
    FMRB_LOGI(TAG, "wasm display backend: %dx%d, software blend, RGBA out",
              fb_w, fb_h);
}

static void wasm_first_frame(void)
{
}

static void wasm_blend_block(const display_blend_req_t *r)
{
    display_blend_cpu_block(r);
}

static void convert_rect(const uint16_t *fb, int x0, int y0, int w, int h)
{
    if (!s_rgba) return;
    for (int y = y0; y < y0 + h; y++) {
        const uint16_t *src = fb + (size_t)y * s_fb_w + x0;
        uint8_t *dst = s_rgba + ((size_t)y * s_fb_w + x0) * 4;
        for (int x = 0; x < w; x++) {
            uint16_t px = src[x];
            dst[0] = (uint8_t)(((px >> 11) & 0x1f) * 255 / 31);
            dst[1] = (uint8_t)(((px >> 5) & 0x3f) * 255 / 63);
            dst[2] = (uint8_t)((px & 0x1f) * 255 / 31);
            dst[3] = 255;
            dst += 4;
        }
    }
}

static void wasm_present(LGFX_Sprite *fb, size_t fb_size)
{
    (void)fb_size;
    const uint16_t *pixels = (const uint16_t *)fb->getBuffer();
    if (!pixels || fb->width() != s_fb_w || fb->height() != s_fb_h) return;
    convert_rect(pixels, 0, 0, s_fb_w, s_fb_h);
    s_frame_seq++;
}

static void wasm_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                               int fb_w, int fb_h)
{
    (void)fb_w;
    (void)fb_h;
    /* The block is already composited framebuffer content (cursor fast path);
     * convert just that rectangle into the RGBA buffer. */
    if (!s_rgba) return;
    for (int y = 0; y < h; y++) {
        const uint16_t *src = block + (size_t)y * w;
        uint8_t *dst = s_rgba + ((size_t)(y0 + y) * s_fb_w + x0) * 4;
        for (int x = 0; x < w; x++) {
            uint16_t px = src[x];
            dst[0] = (uint8_t)(((px >> 11) & 0x1f) * 255 / 31);
            dst[1] = (uint8_t)(((px >> 5) & 0x3f) * 255 / 63);
            dst[2] = (uint8_t)((px & 0x1f) * 255 / 31);
            dst[3] = 255;
            dst += 4;
        }
    }
    s_frame_seq++;
}

static void wasm_shutdown(void)
{
}

static const display_backend_t s_backend_wasm = {
    .name        = "wasm",
    .init        = wasm_init,
    .first_frame = wasm_first_frame,
    .blend_block = wasm_blend_block,
    .present       = wasm_present,
    .present_patch = wasm_present_patch,
    .shutdown      = wasm_shutdown,
};

const display_backend_t *display_backend_wasm(void)
{
    return &s_backend_wasm;
}
