/*
 * fmrb_present.c - see fmrb_present.h.
 *
 * Plain C over memory, no output API, no allocation: an output hands in its
 * surface (an SDL texture it locked, a mapped DRM buffer, an fbdev mmap).
 */

#include "fmrb_present.h"

#include <string.h>

void fmrb_present_place(int out_w, int out_h, int fb_w, int fb_h,
                        fmrb_present_place_t *place)
{
    int s = 1;
    if (fb_w > 0 && fb_h > 0) {
        int sx = out_w / fb_w;
        int sy = out_h / fb_h;
        s = (sx < sy) ? sx : sy;
        if (s < 1) s = 1;
    }
    place->scale = s;
    place->w = fb_w * s;
    place->h = fb_h * s;
    place->x = (out_w - place->w) / 2;
    place->y = (out_h - place->h) / 2;
    if (place->x < 0) place->x = 0;
    if (place->y < 0) place->y = 0;
}

void fmrb_present_surface_size(int base_w, int base_h, int scale,
                               int *out_w, int *out_h)
{
    if (scale < 1) scale = 1;
    int w = base_w * scale;
    int h = base_h * scale;
    int wide = (h * 16 + 8) / 9;
    if (wide > w) w = wide;
    *out_w = w;
    *out_h = h;
}

/* One generic body for both pixel formats: the conversion is the only thing
 * that differs. */
#define PRESENT_BLIT(PIX_T, CONVERT)                                            \
    do {                                                                        \
        const int s = place->scale;                                             \
        for (int y = 0; y < out_h; y++) {                                       \
            PIX_T *row = dst + (size_t)y * dst_stride_px;                       \
            int fy = y - place->y;                                              \
            if (fy < 0 || fy >= place->h) {                                     \
                memset(row, 0, sizeof(PIX_T) * (size_t)out_w);                  \
                continue;                                                       \
            }                                                                   \
            fy /= s;                                                            \
            if (fy >= fb_h) {                                                   \
                memset(row, 0, sizeof(PIX_T) * (size_t)out_w);                  \
                continue;                                                       \
            }                                                                   \
            /* A row of the frame repeats s times; the first copy is built,  */ \
            /* the rest are memcpy'd from it.                                 */ \
            if (y > 0 && (y - place->y) % s != 0) {                             \
                memcpy(row, row - dst_stride_px, sizeof(PIX_T) * (size_t)out_w); \
                continue;                                                       \
            }                                                                   \
            const uint16_t *src = fb + (size_t)fy * fb_w;                       \
            int x = 0;                                                          \
            for (; x < place->x && x < out_w; x++) row[x] = 0;                  \
            for (int fx = 0; fx < fb_w && x < out_w; fx++) {                    \
                PIX_T v = CONVERT(src[fx]);                                     \
                for (int k = 0; k < s && x < out_w; k++) row[x++] = v;          \
            }                                                                   \
            for (; x < out_w; x++) row[x] = 0;                                  \
        }                                                                       \
    } while (0)

#define CONVERT_565(px) (px)
#define CONVERT_8888(px)                                                        \
    ((uint32_t)((((px) >> 11) & 0x1f) * 255 / 31) << 16 |                       \
     (uint32_t)((((px) >> 5) & 0x3f) * 255 / 63) << 8 |                         \
     (uint32_t)(((px) & 0x1f) * 255 / 31))

void fmrb_present_blit_rgb565(const uint16_t *fb, int fb_w, int fb_h,
                              uint16_t *dst, int out_w, int out_h,
                              int dst_stride_px,
                              const fmrb_present_place_t *place)
{
    PRESENT_BLIT(uint16_t, CONVERT_565);
}

void fmrb_present_blit_xrgb8888(const uint16_t *fb, int fb_w, int fb_h,
                                uint32_t *dst, int out_w, int out_h,
                                int dst_stride_px,
                                const fmrb_present_place_t *place)
{
    PRESENT_BLIT(uint32_t, CONVERT_8888);
}

bool fmrb_present_to_frame(const fmrb_present_place_t *place, int fb_w, int fb_h,
                           int sx, int sy, int *fx, int *fy)
{
    const int s = place->scale > 0 ? place->scale : 1;
    int x = sx - place->x;
    int y = sy - place->y;
    bool inside = (x >= 0 && y >= 0 && x < place->w && y < place->h);
    x = (x < 0) ? 0 : x / s;
    y = (y < 0) ? 0 : y / s;
    if (x >= fb_w) x = fb_w - 1;
    if (y >= fb_h) y = fb_h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *fx = x;
    *fy = y;
    return inside;
}
