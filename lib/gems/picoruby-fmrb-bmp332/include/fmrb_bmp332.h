#ifndef FMRB_BMP332_H
#define FMRB_BMP332_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The largest width and height the parser takes. Wider than any screen a
// Family mruby machine drives (the widest is NARYAv4's 1280 px HDMI panel), so
// a sheet or strip of frames may be laid out wider than the screen. What a
// loader can afford to read is its own limit on the file size.
#define FMRB_BMP332_MAX_DIM 2048

// Parsed BMP332 result
typedef struct {
    uint16_t width;
    uint16_t height;
    const uint8_t *pixels;   // Points into buf (RGB332 pixel data, top-down)
    size_t pixels_size;      // width * height
} fmrb_bmp332_t;

/**
 * @brief Parse an 8-bit indexed BMP from memory buffer
 * Pixel data is converted to top-down row order (BMP is bottom-up).
 * The work buffer is used to flip rows in place.
 *
 * @param buf       BMP file data (will be modified: pixel rows are flipped in place)
 * @param buf_size  Size of buf
 * @param out       Output structure (pixels points into buf)
 * @return 0 on success, -1 on error
 */
int fmrb_bmp332_parse(uint8_t *buf, size_t buf_size, fmrb_bmp332_t *out);

#ifdef __cplusplus
}
#endif

#endif // FMRB_BMP332_H
