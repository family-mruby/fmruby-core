/*
 * fmrb_present.c - see fmrb_present.h.
 *
 * Plain C over memory, no output API, no allocation: an output hands in its
 * surface (an SDL texture it locked, a mapped DRM buffer, an fbdev mmap).
 * Called from one thread (the native program's main thread, or a test), so
 * the coordinate tables below are plain statics.
 */

#include "fmrb_present.h"

#include <string.h>

/* Largest view side the tables cover; beyond it the view is cropped. */
#define PRESENT_MAX_DIM 8192

static int s_map_x[PRESENT_MAX_DIM];   /* view column -> frame column, -1 = border */
static int s_map_y[PRESENT_MAX_DIM];   /* view row -> frame row, -1 = border */

int fmrb_present_bytes_per_pixel(fmrb_present_format_t format)
{
    return format == FMRB_PRESENT_XRGB8888 ? 4 : 2;
}

bool fmrb_present_rotate_valid(int rotate)
{
    return rotate == 0 || rotate == 90 || rotate == 180 || rotate == 270;
}

void fmrb_present_view_size(int w, int h, int rotate, int *view_w, int *view_h)
{
    if (rotate == 90 || rotate == 270) {
        *view_w = h;
        *view_h = w;
    } else {
        *view_w = w;
        *view_h = h;
    }
}

static int largest_fit(int view_w, int view_h, int fb_w, int fb_h)
{
    if (fb_w <= 0 || fb_h <= 0) return 1;
    int sx = view_w / fb_w;
    int sy = view_h / fb_h;
    int s = (sx < sy) ? sx : sy;
    return s < 1 ? 1 : s;
}

void fmrb_present_place(const fmrb_present_surface_t *surface, int fb_w, int fb_h,
                        int fixed_scale, fmrb_present_place_t *place)
{
    int rotate = fmrb_present_rotate_valid(surface->rotate) ? surface->rotate : 0;
    int vw = 0, vh = 0;
    fmrb_present_view_size(surface->w, surface->h, rotate, &vw, &vh);
    int fit = largest_fit(vw, vh, fb_w, fb_h);
    int s = (fixed_scale >= 1 && fixed_scale <= fit) ? fixed_scale : fit;
    place->scale = s;
    place->w = fb_w * s;
    place->h = fb_h * s;
    place->x = (vw - place->w) / 2;
    place->y = (vh - place->h) / 2;
    if (place->x < 0) place->x = 0;
    if (place->y < 0) place->y = 0;
    place->view_w = vw;
    place->view_h = vh;
    place->rotate = rotate;
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

/* One view axis: offset, scale and frame length to a table of frame
 * indices. Returns the number of entries filled. */
static int build_map(int *map, int view_len, int off, int scaled_len, int s, int fb_len)
{
    int n = view_len < PRESENT_MAX_DIM ? view_len : PRESENT_MAX_DIM;
    for (int v = 0; v < n; v++) {
        int d = v - off;
        int f = (d < 0 || d >= scaled_len) ? -1 : d / s;
        map[v] = (f >= fb_len) ? -1 : f;
    }
    return n;
}

/* Upright: rows of the frame repeat `s` times, so the first copy of each is
 * built and the rest are memcpy'd from it. */
#define PRESENT_UPRIGHT(PIX_T, CONVERT)                                         \
    do {                                                                        \
        const int s = place->scale;                                             \
        uint8_t *base = (uint8_t *)surface->pixels;                             \
        for (int y = 0; y < out_h; y++) {                                       \
            PIX_T *row = (PIX_T *)(base + (size_t)y * stride);                  \
            int fy = (y < max_y) ? s_map_y[y] : -1;                             \
            if (fy < 0) {                                                       \
                memset(row, 0, sizeof(PIX_T) * (size_t)out_w);                  \
                continue;                                                       \
            }                                                                   \
            if (y > 0 && (y - place->y) % s != 0) {                             \
                memcpy(row, base + (size_t)(y - 1) * stride,                    \
                       sizeof(PIX_T) * (size_t)out_w);                          \
                continue;                                                       \
            }                                                                   \
            const uint16_t *src = fb + (size_t)fy * fb_w;                       \
            int x = 0;                                                          \
            for (; x < place->x && x < out_w; x++) row[x] = 0;                  \
            for (int fx = 0; fx < fb_w && x < out_w && x - place->x < place->w; fx++) { \
                PIX_T v = CONVERT(src[fx]);                                     \
                for (int k = 0; k < s && x < out_w; k++) row[x++] = v;          \
            }                                                                   \
            for (; x < out_w; x++) row[x] = 0;                                  \
        }                                                                       \
    } while (0)

/* Turned: every surface pixel is mapped back to the view, then to the frame
 * through the two tables. Written row by row in surface order. */
#define PRESENT_TURNED(PIX_T, CONVERT)                                          \
    do {                                                                        \
        uint8_t *base = (uint8_t *)surface->pixels;                             \
        const int W = out_w, H = out_h;                                         \
        for (int py = 0; py < H; py++) {                                        \
            PIX_T *row = (PIX_T *)(base + (size_t)py * stride);                 \
            for (int px = 0; px < W; px++) {                                    \
                int vx, vy;                                                     \
                if (rot == 90) { vx = py; vy = W - 1 - px; }                    \
                else if (rot == 180) { vx = W - 1 - px; vy = H - 1 - py; }      \
                else { vx = H - 1 - py; vy = px; }                              \
                int fx = (vx < max_x) ? s_map_x[vx] : -1;                       \
                int fy = (vy < max_y) ? s_map_y[vy] : -1;                       \
                row[px] = (fx < 0 || fy < 0) ? 0                                \
                          : CONVERT(fb[(size_t)fy * fb_w + fx]);                \
            }                                                                   \
        }                                                                       \
    } while (0)

#define CONVERT_565(px)  (px)
#define CONVERT_8888(px) fmrb_present_565_to_8888(px)

void fmrb_present_draw(const uint16_t *fb, int fb_w, int fb_h,
                       const fmrb_present_surface_t *surface,
                       const fmrb_present_place_t *place)
{
    if (fb == NULL || surface->pixels == NULL || fb_w <= 0 || fb_h <= 0) return;
    const int out_w = surface->w;
    const int out_h = surface->h;
    const size_t stride = (size_t)surface->stride_bytes;
    const int rot = place->rotate;
    int max_x = build_map(s_map_x, place->view_w, place->x, place->w, place->scale, fb_w);
    int max_y = build_map(s_map_y, place->view_h, place->y, place->h, place->scale, fb_h);

    if (surface->format == FMRB_PRESENT_XRGB8888) {
        if (rot == 0) PRESENT_UPRIGHT(uint32_t, CONVERT_8888);
        else PRESENT_TURNED(uint32_t, CONVERT_8888);
    } else {
        if (rot == 0) PRESENT_UPRIGHT(uint16_t, CONVERT_565);
        else PRESENT_TURNED(uint16_t, CONVERT_565);
    }
}

bool fmrb_present_to_frame(const fmrb_present_place_t *place, int fb_w, int fb_h,
                           int sx, int sy, int *fx, int *fy)
{
    /* Physical to view: the inverse of the turn the draw makes. */
    int vx = sx, vy = sy;
    int W = 0, H = 0;   /* the physical surface */
    fmrb_present_view_size(place->view_w, place->view_h, place->rotate, &W, &H);
    if (place->rotate == 90) { vx = sy; vy = W - 1 - sx; }
    else if (place->rotate == 180) { vx = W - 1 - sx; vy = H - 1 - sy; }
    else if (place->rotate == 270) { vx = H - 1 - sy; vy = sx; }

    const int s = place->scale > 0 ? place->scale : 1;
    int x = vx - place->x;
    int y = vy - place->y;
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
