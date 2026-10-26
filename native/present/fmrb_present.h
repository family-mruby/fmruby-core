/*
 * fmrb_present.h - putting the composited screen on an output surface.
 *
 * The display task composites one RGB565 frame (426x240, or 640x360 while a
 * fullscreen high-resolution app is up). Every Linux output shows that frame
 * on a surface of its own -- an SDL texture, a DRM dumb buffer, an fbdev
 * mmap -- and they all want the same thing done to it: a whole-number scale
 * (the largest that fits, or a fixed one), centred, the rest black, turned
 * for a panel mounted sideways, written in the surface's pixel format. That
 * is this file, with no output API in it, so the outputs share it
 * (doc/linux_native/plan.md, "画面の出力") and the host tests can check it on
 * surfaces in memory (test/native/).
 *
 * Rotation: `rotate` is how far the picture is turned clockwise on the
 * surface (0, 90, 180, 270). The frame is laid out in the "view", the surface
 * as the viewer sees it: a 720x1280 portrait panel with rotate 90 is a
 * 1280x720 view.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FMRB_PRESENT_RGB565 = 0,    /* 16 bit, red in the top bits */
    FMRB_PRESENT_XRGB8888,      /* 32 bit, blue in the low byte, top byte 0 */
} fmrb_present_format_t;

/* An output surface: memory the output owns (a locked texture, a mapped
 * buffer). The present core only writes inside w x h. */
typedef struct {
    void *pixels;
    int w, h;                       /* the surface as stored (physical) */
    int stride_bytes;               /* bytes from one row to the next */
    fmrb_present_format_t format;
    int rotate;                     /* 0 / 90 / 180 / 270, clockwise */
} fmrb_present_surface_t;

/* Where a frame lands, in view coordinates. */
typedef struct {
    int scale;      /* whole-number magnification, at least 1 */
    int x, y;       /* top-left of the scaled frame in the view */
    int w, h;       /* size of the scaled frame (fb size * scale) */
    int view_w;     /* the view: the surface, turned by `rotate` */
    int view_h;
    int rotate;
} fmrb_present_place_t;

/* Bytes per pixel of a format. */
int fmrb_present_bytes_per_pixel(fmrb_present_format_t format);

/* True for 0, 90, 180 and 270. */
bool fmrb_present_rotate_valid(int rotate);

/* The view size of a w x h surface turned by rotate. */
void fmrb_present_view_size(int w, int h, int rotate, int *view_w, int *view_h);

/* Lay fb_w x fb_h out on the surface. fixed_scale 0 takes the largest
 * whole-number scale that fits the view; a fixed scale that does not fit
 * falls back to the largest that does (the high-resolution frame on a
 * surface sized for the base one). A view smaller than the frame still gets
 * scale 1, and the frame is cropped. */
void fmrb_present_place(const fmrb_present_surface_t *surface, int fb_w, int fb_h,
                        int fixed_scale, fmrb_present_place_t *place);

/* Draw fb (fb_w x fb_h RGB565, tightly packed) onto the surface at place,
 * painting everything outside the frame black. */
void fmrb_present_draw(const uint16_t *fb, int fb_w, int fb_h,
                       const fmrb_present_surface_t *surface,
                       const fmrb_present_place_t *place);

/* The surface size an output should ask for when it can choose (a window):
 * the base frame at `scale`, widened to 16:9 when that is wider, so the
 * high-resolution frame (640x360) also gets a whole scale -- 3 gives
 * 1280x720, where 426x240 shows at 3x and 640x360 at 2x. In view
 * coordinates; a rotated output swaps the two. */
void fmrb_present_surface_size(int base_w, int base_h, int scale,
                               int *out_w, int *out_h);

/* A point on the physical surface back to frame coordinates, clamped into
 * the frame. Returns false when the point is outside the frame (in the black
 * border); the clamped value is still written. */
bool fmrb_present_to_frame(const fmrb_present_place_t *place, int fb_w, int fb_h,
                           int sx, int sy, int *fx, int *fy);

/* One RGB565 pixel as XRGB8888 (each channel widened to 8 bits). */
static inline uint32_t fmrb_present_565_to_8888(uint16_t px)
{
    uint32_t r = (uint32_t)((px >> 11) & 0x1f);
    uint32_t g = (uint32_t)((px >> 5) & 0x3f);
    uint32_t b = (uint32_t)(px & 0x1f);
    return ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
}

#ifdef __cplusplus
}
#endif
