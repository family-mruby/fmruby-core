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

/* How far the 426x240 frame is blown up to fill the panel. NARYA v4's HDMI
 * output is 800x600, where 426x240 goes on at 1.5x (the PPA backend's
 * geometry block explains the choice); Tab5's rotated 720x1280 takes 3x.
 * SCALE_FACTOR stays an integer because the cursor patch below replicates
 * pixels; the fractional case scales into a temporary of the same shape. */
#if defined(FMRB_HW_NARYAV4)
#define SCALE_NUM 3
#define SCALE_DEN 2
#define SCALE_FACTOR 2          /* patch temporary sizing: ceil(1.5) */
#define SCALE_FLOAT 1.5f
#else
#define SCALE_NUM 3
#define SCALE_DEN 1
#define SCALE_FACTOR 3
#define SCALE_FLOAT ((float)SCALE_FACTOR)
#endif

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

    int scaled_w = fb->width()  * SCALE_NUM / SCALE_DEN;
    int scaled_h = fb->height() * SCALE_NUM / SCALE_DEN;
    LGFX_Device *lcd = display_p4_lcd();
    int center_x = (lcd->width()  - scaled_w) / 2 + scaled_w / 2;
    int center_y = (lcd->height() - scaled_h) / 2 + scaled_h / 2;
    fb->pushRotateZoom(lcd, (float)center_x, (float)center_y, 0.0f,
                       SCALE_FLOAT, SCALE_FLOAT);
}

static void cpu_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                              int fb_w, int fb_h)
{
    static uint16_t scaled[DISPLAY_PATCH_MAX_W * SCALE_FACTOR *
                           DISPLAY_PATCH_MAX_H * SCALE_FACTOR];
    if (w > DISPLAY_PATCH_MAX_W || h > DISPLAY_PATCH_MAX_H) return;

    /* Nearest-neighbour, the same rule the full present's scaler uses: output
     * column ox comes from input column ox * DEN / NUM. */
    const int ox0 = x0 * SCALE_NUM / SCALE_DEN;
    const int oy0 = y0 * SCALE_NUM / SCALE_DEN;
    const int ow  = (x0 + w) * SCALE_NUM / SCALE_DEN - ox0;
    const int oh  = (y0 + h) * SCALE_NUM / SCALE_DEN - oy0;
    for (int oy = 0; oy < oh; oy++) {
        int sy = (oy0 + oy) * SCALE_DEN / SCALE_NUM - y0;
        if (sy < 0) sy = 0; else if (sy >= h) sy = h - 1;
        uint16_t *o = scaled + (size_t)oy * ow;
        for (int ox = 0; ox < ow; ox++) {
            int sx = (ox0 + ox) * SCALE_DEN / SCALE_NUM - x0;
            if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
            o[ox] = block[(size_t)sy * w + sx];
        }
    }
    LGFX_Device *lcd = display_p4_lcd();
    int offset_x = (lcd->width()  - fb_w * SCALE_NUM / SCALE_DEN) / 2;
    int offset_y = (lcd->height() - fb_h * SCALE_NUM / SCALE_DEN) / 2;
    lcd->pushImage(offset_x + ox0, offset_y + oy0, ow, oh,
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
