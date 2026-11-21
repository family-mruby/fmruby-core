// A picture's size and kind from its head, and a picture rewritten as the
// palette PNG (fmrb_picture332.h). Pure C over buffers, like png332.c: the
// host test (test/png332) builds this file.

#include "fmrb_picture332.h"
#include "fmrb_bmp332.h"
#include <string.h>

static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fmrb_picture332_head(const uint8_t *buf, size_t len, fmrb_picture332_head_t *out)
{
    if (!buf || !out) return -1;
    uint32_t w, h;
    if (fmrb_png332_is_png(buf, len)) {
        // The signature, then IHDR first: length 13, the type, width, height.
        if (len < 24 || rd32be(buf + 8) != 13 || memcmp(buf + 12, "IHDR", 4) != 0) return -1;
        w = rd32be(buf + 16);
        h = rd32be(buf + 20);
        out->format = FMRB_PICTURE332_PNG;
    } else if (len >= FMRB_PICTURE332_HEAD_BYTES && buf[0] == 'B' && buf[1] == 'M') {
        // What fmrb_bmp332_parse takes: BITMAPINFOHEADER or larger, 8 bits,
        // no compression; a negative height is a top-down picture.
        int32_t sw = (int32_t)rd32le(buf + 18);
        int32_t sh = (int32_t)rd32le(buf + 22);
        uint16_t bpp = (uint16_t)(buf[28] | (buf[29] << 8));
        if (rd32le(buf + 14) < 40 || bpp != 8 || rd32le(buf + 30) != 0) return -1;
        if (sw <= 0 || sh == 0 || sh == INT32_MIN) return -1;
        w = (uint32_t)sw;
        h = (uint32_t)(sh < 0 ? -sh : sh);
        out->format = FMRB_PICTURE332_BMP;
    } else {
        return -1;
    }
    if (w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF) return -1;
    out->width = (uint16_t)w;
    out->height = (uint16_t)h;
    return 0;
}

int fmrb_picture332_to_png(const uint8_t *buf, size_t len, uint16_t max_w, uint16_t max_h, size_t max_bytes,
                           const fmrb_png332_allocator_t *a, uint8_t **out, size_t *out_len,
                           uint16_t *width, uint16_t *height)
{
    fmrb_picture332_head_t head;
    *out = NULL;
    *out_len = 0;
    if (fmrb_picture332_head(buf, len, &head) != 0) return FMRB_PICTURE332_E_UNREADABLE;
    *width = head.width;
    *height = head.height;
    // Nothing past what the decoders take, whatever the caller allows.
    if (max_w > FMRB_PNG332_MAX_DIM) max_w = FMRB_PNG332_MAX_DIM;
    if (max_h > FMRB_PNG332_MAX_DIM) max_h = FMRB_PNG332_MAX_DIM;
    if (head.width > max_w || head.height > max_h) return FMRB_PICTURE332_E_TOO_BIG;

    uint16_t w = 0, h = 0;
    uint8_t *px = NULL;        // the pixels, when decoded into a buffer of their own (PNG)
    uint8_t *copy = NULL;      // the BMP decoded in place
    const uint8_t *pixels = NULL;
    if (head.format == FMRB_PICTURE332_PNG) {
        int r = fmrb_png332_decode(buf, len, a, &w, &h, &px);
        if (r == FMRB_PNG332_E_NOMEM) return FMRB_PICTURE332_E_NOMEM;
        if (r != FMRB_PNG332_OK) return FMRB_PICTURE332_E_UNREADABLE;
        pixels = px;
    } else {
        copy = (uint8_t *)a->alloc(a->ud, len);
        if (!copy) return FMRB_PICTURE332_E_NOMEM;
        memcpy(copy, buf, len);
        fmrb_bmp332_t bmp;
        if (fmrb_bmp332_parse(copy, len, &bmp) != 0) {
            a->free(a->ud, copy);
            return FMRB_PICTURE332_E_UNREADABLE;
        }
        w = bmp.width;
        h = bmp.height;
        pixels = bmp.pixels;
    }
    *width = w;
    *height = h;

    uint8_t *png = NULL;
    size_t png_len = 0;
    int r = fmrb_png332_encode(pixels, w, h, a, &png, &png_len);
    if (px) a->free(a->ud, px);
    if (copy) a->free(a->ud, copy);
    if (r == FMRB_PNG332_E_NOMEM) return FMRB_PICTURE332_E_NOMEM;
    if (r != FMRB_PNG332_OK) return FMRB_PICTURE332_E_UNREADABLE;
    if (max_bytes > 0 && png_len > max_bytes) {
        a->free(a->ud, png);
        *out_len = png_len;
        return FMRB_PICTURE332_E_TOO_HEAVY;
    }
    *out = png;
    *out_len = png_len;
    return FMRB_PICTURE332_OK;
}
