#ifndef FMRB_PNG332_PNGLE_H
#define FMRB_PNG332_PNGLE_H

// A PNG read into RGB332 pixels on a display side, with LovyanGFX's own PNG
// decoder (pngle) and the colour rule of fmrb_png332.h. The two display sides
// include this: graphics-audio (sim and Retro, through its include path to
// this fmruby-core checkout) and display_local (P4, the browser and the
// native builds). LovyanGFX's drawPng blends a translucent pixel into what
// the target held and leaves a transparent one as it was; a sprite image
// keys a colour instead, so here every pixel is written, the transparent
// ones as FMRB_PNG332_TRANSPARENT, as the app side's BMP332.load reads them.
//
// The pixels are handed to put() one at a time, rows top-down (an interlaced
// PNG comes in its passes; every pixel still arrives once, at its place).
// Pixels at or past (max_w, max_h) are not handed over.
//
// Memory: pngle's working state (about 44 KB: its 32 KB inflate window) from
// malloc, for the length of the call. LovyanGFX keeps one such state of its
// own for drawPng and never frees it; a caller in C++ calls
// releasePngMemory() on any LovyanGFX object first, so the two are never
// held at once (the peak stays what drawPng's was).
//
// Header only, so neither build list changes: include it from one C or C++
// file of a display side that already builds LovyanGFX.

#include <stdint.h>
#include <stddef.h>
#include "fmrb_png332.h"
#include "lgfx/utility/lgfx_pngle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fmrb_png332_put_t)(void *ud, uint32_t x, uint32_t y, uint8_t rgb332);

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    uint32_t max_w, max_h;
    fmrb_png332_put_t put;
    void *ud;
} fmrb_png332_pngle_job_t;

static inline uint32_t fmrb_png332_pngle_read(void *user, uint8_t *dst, uint32_t len)
{
    fmrb_png332_pngle_job_t *job = (fmrb_png332_pngle_job_t *)user;
    size_t left = job->len - job->pos;
    if (len > left) len = (uint32_t)left;
    // pngle skips a chunk's CRC by reading into NULL.
    if (dst && len) {
        const uint8_t *src = job->buf + job->pos;
        for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
    }
    job->pos += len;
    return len;
}

// argb is len pixels of 4 bytes each: alpha, red, green, blue.
static inline void fmrb_png332_pngle_draw(void *user, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t len,
                                          const uint8_t *argb)
{
    fmrb_png332_pngle_job_t *job = (fmrb_png332_pngle_job_t *)user;
    if (y >= job->max_h) return;
    for (size_t i = 0; i < len; i++, x += div_x, argb += 4) {
        if (x >= job->max_w) return;
        job->put(job->ud, x, y, fmrb_png332_from_argb(argb[0], argb[1], argb[2], argb[3]));
    }
}

enum {
    FMRB_PNG332_PNGLE_OK = 0,
    FMRB_PNG332_PNGLE_E_NOMEM = -1,
    FMRB_PNG332_PNGLE_E_DECODE = -2,   // not a PNG, or broken
};

// Decode buf (a whole PNG file) into put(). *width and *height (may be NULL)
// get the picture's own size from its header.
static inline int fmrb_png332_pngle_decode(const uint8_t *buf, size_t len, uint32_t max_w, uint32_t max_h,
                                           fmrb_png332_put_t put, void *ud, uint32_t *width, uint32_t *height)
{
    pngle_t *pngle = lgfx_pngle_new();
    if (!pngle) return FMRB_PNG332_PNGLE_E_NOMEM;
    fmrb_png332_pngle_job_t job = { buf, len, 0, max_w, max_h, put, ud };
    int ret = FMRB_PNG332_PNGLE_E_DECODE;
    if (lgfx_pngle_prepare(pngle, fmrb_png332_pngle_read, &job) >= 0) {
        if (width) *width = lgfx_pngle_get_width(pngle);
        if (height) *height = lgfx_pngle_get_height(pngle);
        if (lgfx_pngle_decomp(pngle, fmrb_png332_pngle_draw) >= 0) ret = FMRB_PNG332_PNGLE_OK;
    }
    lgfx_pngle_destroy(pngle);
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif // FMRB_PNG332_PNGLE_H
