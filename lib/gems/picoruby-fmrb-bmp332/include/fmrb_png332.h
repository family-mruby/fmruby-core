#ifndef FMRB_PNG332_H
#define FMRB_PNG332_H

// Read and write the sprite pictures as PNG on the app side (the sprite
// editor's BMP332.load / BMP332.save), on every board: the Retro core has no
// PNG decoder of its own (its display is the WROVER's), so this is a small
// one that knows exactly the pictures the sprites use.
//
// Pixels are RGB332 bytes, top-down, width * height of them -- the same as
// fmrb_bmp332_parse gives. Writing makes the palette PNG of
// tools/rgb332_png.rb: only the colours used, each RGB332 value expanded to
// RGB888 with the top bits kept, 1/2/4 bits per pixel for small pictures and
// 8 for large ones, filter none. Reading takes that and the usual 8-bit
// pictures an image editor saves (palette 1/2/4/8 bit, grey 1/2/4/8 bit,
// RGB/RGBA/grey+alpha 8 bit, any filter; not interlaced, not 16 bit); a
// colour becomes RGB332 by its top bits, as LovyanGFX does on the display
// side. Transparency (tRNS, alpha) is ignored: the sprites key a colour.
//
// No static or global state: every buffer comes from the allocator passed in
// (the mruby binding hands it the app's VM pool), and the stack use is a few
// hundred bytes, so it adds nothing to internal RAM.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *(*alloc)(void *ud, size_t size);   // NULL when out of memory
    void (*free)(void *ud, void *p);
    void *ud;
} fmrb_png332_allocator_t;

enum {
    FMRB_PNG332_OK = 0,
    FMRB_PNG332_E_NOT_PNG = -1,
    FMRB_PNG332_E_FORMAT = -2,      // a chunk or the header is broken
    FMRB_PNG332_E_UNSUPPORTED = -3, // interlaced, 16 bit, a size past the limit
    FMRB_PNG332_E_DATA = -4,        // the compressed data does not decode
    FMRB_PNG332_E_NOMEM = -5,
};

// The largest width and height read or written (FMRB_BMP332_MAX_DIM's value).
#define FMRB_PNG332_MAX_DIM 2048

int fmrb_png332_is_png(const uint8_t *buf, size_t len);

// On success *pixels is width * height bytes from the allocator; the caller
// frees it.
int fmrb_png332_decode(const uint8_t *buf, size_t len,
                       const fmrb_png332_allocator_t *a,
                       uint16_t *width, uint16_t *height, uint8_t **pixels);

// On success *out is *out_len bytes of PNG from the allocator; the caller
// frees it.
int fmrb_png332_encode(const uint8_t *pixels, uint16_t width, uint16_t height,
                       const fmrb_png332_allocator_t *a,
                       uint8_t **out, size_t *out_len);

const char *fmrb_png332_error(int code);

#ifdef __cplusplus
}
#endif

#endif // FMRB_PNG332_H
