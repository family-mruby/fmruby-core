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
#include "fmrb_attr.h"

static const char *TAG = "display_cpu";

/* How far the frame is blown up to fill the panel follows the frame itself
 * (display_scale_for): 3x for 426x240 and 2x for the fullscreen
 * high-resolution 640x360 on the 1280x720 landscape surface both boards
 * present. Whole numbers only, because the cursor patch below replicates
 * pixels. */
FMRB_EXT_RAM_BSS_ATTR static int s_last_fb_w;   /* PSRAM: see the PPA backend */
FMRB_EXT_RAM_BSS_ATTR static int s_last_fb_h;

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

    LGFX_Device *lcd = display_p4_lcd();
    const int fb_w = fb->width();
    const int fb_h = fb->height();
    const int scale = display_scale_for(lcd->width(), lcd->height(), fb_w, fb_h);
    int scaled_w = fb_w * scale;
    int scaled_h = fb_h * scale;
    int center_x = (lcd->width()  - scaled_w) / 2 + scaled_w / 2;
    int center_y = (lcd->height() - scaled_h) / 2 + scaled_h / 2;
    fb->pushRotateZoom(lcd, (float)center_x, (float)center_y, 0.0f,
                       (float)scale, (float)scale);

    /* The frame changed size (fullscreen high-resolution mode on or off):
     * black out whatever of the previous picture the new one leaves. */
    if (fb_w != s_last_fb_w || fb_h != s_last_fb_h) {
        s_last_fb_w = fb_w;
        s_last_fb_h = fb_h;
        display_lcd_clear_outside(lcd, (lcd->width() - scaled_w) / 2,
                                  (lcd->height() - scaled_h) / 2,
                                  scaled_w, scaled_h);
    }
}

static void cpu_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                              int fb_w, int fb_h)
{
    static uint16_t scaled[DISPLAY_PATCH_MAX_W * DISPLAY_MAX_SCALE *
                           DISPLAY_PATCH_MAX_H * DISPLAY_MAX_SCALE];
    if (w > DISPLAY_PATCH_MAX_W || h > DISPLAY_PATCH_MAX_H) return;

    LGFX_Device *lcd = display_p4_lcd();
    const int scale = display_scale_for(lcd->width(), lcd->height(), fb_w, fb_h);
    if (scale > DISPLAY_MAX_SCALE) return;

    /* Nearest-neighbour, the same rule the full present's scaler uses. */
    const int ow = w * scale;
    const int oh = h * scale;
    for (int oy = 0; oy < oh; oy++) {
        const uint16_t *src = block + (size_t)(oy / scale) * w;
        uint16_t *o = scaled + (size_t)oy * ow;
        for (int ox = 0; ox < ow; ox++) o[ox] = src[ox / scale];
    }
    int offset_x = (lcd->width()  - fb_w * scale) / 2;
    int offset_y = (lcd->height() - fb_h * scale) / 2;
    lcd->pushImage(offset_x + x0 * scale, offset_y + y0 * scale, ow, oh,
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
