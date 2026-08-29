/*
 * display_backend_cpu.cpp - the software output path (ESP32-P4)
 *
 * Compositing is display_blend_cpu.c; the finished frame reaches the panel
 * through LovyanGFX pushRotateZoom, the same software path the PPA backend
 * falls back to when the SRM is unavailable. This backend exists to validate
 * the wasm drawing path's compositing on real hardware (doc/wasm/, P3 T2):
 * correctness over speed, selected at build time with
 * FMRB_DISPLAY_BACKEND=cpu.
 *
 * No cache maintenance anywhere here: every read and write is by the CPU, so
 * the caches stay coherent on their own, and LovyanGFX handles the panel side
 * the same way it always has on the PPA-less fallback.
 */

#include "display_backend.h"

#include "fmrb_log.h"

static const char *TAG = "display_cpu";

#define SCALE_FACTOR 3

static void cpu_init(int fb_w, int fb_h)
{
    FMRB_LOGI(TAG, "CPU display backend: %dx%d, software blend + pushRotateZoom",
              fb_w, fb_h);
}

static void cpu_first_frame(void)
{
    display_p4_lcd()->fillScreen(0);
}

static void cpu_blend_block(const display_blend_req_t *r)
{
    display_blend_cpu_block(r);
}

static void cpu_present(LGFX_Sprite *fb, size_t fb_size)
{
    (void)fb_size;

    int scaled_w = fb->width() * SCALE_FACTOR;
    int scaled_h = fb->height() * SCALE_FACTOR;
    LGFX_Device *lcd = display_p4_lcd();
    int center_x = (lcd->width()  - scaled_w) / 2 + scaled_w / 2;
    int center_y = (lcd->height() - scaled_h) / 2 + scaled_h / 2;
    fb->pushRotateZoom(lcd, (float)center_x, (float)center_y, 0.0f,
                       (float)SCALE_FACTOR, (float)SCALE_FACTOR);
}

static void cpu_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                              int fb_w, int fb_h)
{
    (void)fb_h;

    static uint16_t scaled[DISPLAY_PATCH_MAX_W * SCALE_FACTOR *
                           DISPLAY_PATCH_MAX_H * SCALE_FACTOR];
    if (w > DISPLAY_PATCH_MAX_W || h > DISPLAY_PATCH_MAX_H) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t px = block[(size_t)y * w + x];
            for (int dy = 0; dy < SCALE_FACTOR; dy++) {
                uint16_t *o = scaled + (size_t)(y * SCALE_FACTOR + dy) * (w * SCALE_FACTOR)
                            + x * SCALE_FACTOR;
                for (int dx = 0; dx < SCALE_FACTOR; dx++) o[dx] = px;
            }
        }
    }
    LGFX_Device *lcd = display_p4_lcd();
    int offset_x = (lcd->width() - fb_w * SCALE_FACTOR) / 2;
    lcd->pushImage(offset_x + x0 * SCALE_FACTOR, y0 * SCALE_FACTOR,
                   w * SCALE_FACTOR, h * SCALE_FACTOR,
                   (lgfx::rgb565_t *)scaled);
}

static void cpu_shutdown(void)
{
}

/* ------------------------------------------------------------------------ */

static const display_backend_t s_backend_cpu = {
    .name        = "cpu",
    .init        = cpu_init,
    .first_frame = cpu_first_frame,
    .blend_block = cpu_blend_block,
    .present       = cpu_present,
    .present_patch = cpu_present_patch,
    .shutdown      = cpu_shutdown,
};

const display_backend_t *display_backend_cpu(void)
{
    return &s_backend_cpu;
}
