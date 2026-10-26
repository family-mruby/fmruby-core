/*
 * fmrb_present.h - putting the composited screen on a bigger surface.
 *
 * The display task composites one RGB565 frame (426x240, or 640x360 while a
 * fullscreen high-resolution app is up). Every Linux output shows that frame
 * on a surface of its own size -- an SDL window today; the HDMI mode through
 * DRM and the LCD through fbdev in L2 -- and they all want the same thing done
 * to it: the largest whole-number scale that fits, centred, the rest black.
 * That is this file, with no output API in it, so the outputs share it
 * (doc/linux_native/plan.md, "画面の出力"). Rotation for panels mounted
 * sideways belongs here too when an output needs it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where a frame lands on an output surface. */
typedef struct {
    int scale;      /* whole-number magnification, at least 1 */
    int x, y;       /* top-left of the scaled frame on the surface */
    int w, h;       /* size of the scaled frame (fb size * scale) */
} fmrb_present_place_t;

/* The largest whole-number scale at which fb_w x fb_h fits out_w x out_h,
 * centred. A surface smaller than the frame still gets scale 1 (the frame is
 * then cropped by the blit). */
void fmrb_present_place(int out_w, int out_h, int fb_w, int fb_h,
                        fmrb_present_place_t *place);

/* The surface size an output should ask for when it can choose (a window):
 * the base frame at `scale`, widened to 16:9 when that is wider, so the
 * high-resolution frame (640x360) also gets a whole scale -- 3 gives
 * 1280x720, where 426x240 shows at 3x and 640x360 at 2x. */
void fmrb_present_surface_size(int base_w, int base_h, int scale,
                               int *out_w, int *out_h);

/* Scale fb (fb_w x fb_h RGB565, tightly packed) onto an RGB565 surface of
 * out_w x out_h with a row stride of dst_stride_px pixels, painting everything
 * outside the frame black. */
void fmrb_present_blit_rgb565(const uint16_t *fb, int fb_w, int fb_h,
                              uint16_t *dst, int out_w, int out_h,
                              int dst_stride_px,
                              const fmrb_present_place_t *place);

/* The same, for a 32-bit XRGB8888 surface (what DRM dumb buffers and most
 * fbdev consoles take). */
void fmrb_present_blit_xrgb8888(const uint16_t *fb, int fb_w, int fb_h,
                                uint32_t *dst, int out_w, int out_h,
                                int dst_stride_px,
                                const fmrb_present_place_t *place);

/* A point on the surface back to frame coordinates, clamped into the frame.
 * Returns false when the point is outside the frame (in the black border);
 * the clamped value is still written. */
bool fmrb_present_to_frame(const fmrb_present_place_t *place, int fb_w, int fb_h,
                           int sx, int sy, int *fx, int *fy);

#ifdef __cplusplus
}
#endif
