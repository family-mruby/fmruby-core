/*
 * display_backend_native.cpp - the native program's display output behind
 * the display_backend_t table (doc/linux_native/).
 *
 * Compositing is the shared software core (display_blend_cpu.c, the same
 * file the P4 CPU backend and the browser run). "Presenting" hands the
 * finished RGB565 frame to native_frame.c, from where the program's main
 * thread puts it on its output (an SDL window, fbdev or DRM: native/output/,
 * scaling with native/present/) and takes screenshots. The frame changes size with the
 * fullscreen high-resolution mode; the output follows the size it is given
 * and paints the border black.
 */

#include "display_backend.h"

#include "fmrb_log.h"

extern "C" {
#include "native_frame.h"
}

static const char *TAG = "display_native";

static void native_init(int fb_w, int fb_h)
{
    if (!native_frame_init(fb_w, fb_h)) {
        FMRB_LOGE(TAG, "no memory for the %dx%d frame", fb_w, fb_h);
        return;
    }
    FMRB_LOGI(TAG, "native display backend: %dx%d, software blend", fb_w, fb_h);
}

static void native_first_frame(void)
{
}

static void native_blend_block(const display_blend_req_t *r)
{
    display_blend_cpu_block(r);
}

static void native_present(LGFX_Sprite *fb, size_t fb_size)
{
    (void)fb_size;
    const uint16_t *pixels = (const uint16_t *)fb->getBuffer();
    if (!pixels) return;
    native_frame_put(pixels, fb->width(), fb->height());
}

static void native_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                                 int fb_w, int fb_h)
{
    native_frame_put_rect(block, x0, y0, w, h, fb_w, fb_h);
}

static void native_shutdown(void)
{
}

static const display_backend_t s_backend_native = {
    .name          = "native",
    .init          = native_init,
    .first_frame   = native_first_frame,
    .blend_block   = native_blend_block,
    .present       = native_present,
    .present_patch = native_present_patch,
    .shutdown      = native_shutdown,
};

const display_backend_t *display_backend_native(void)
{
    return &s_backend_native;
}
