#include "fmrb_bmp332.h"
#include <string.h>

// Read little-endian uint16 from buffer
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Read little-endian uint32 from buffer
static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fmrb_bmp332_parse(uint8_t *buf, size_t buf_size, fmrb_bmp332_t *out) {
    if (!buf || !out || buf_size < 54) {
        return -1;
    }

    // BMP file header (14 bytes)
    if (buf[0] != 'B' || buf[1] != 'M') {
        return -1;  // Not a BMP
    }

    uint32_t pixel_offset = read_u32(buf + 10);

    // DIB header (BITMAPINFOHEADER, 40 bytes at offset 14)
    uint32_t dib_size = read_u32(buf + 14);
    if (dib_size < 40) {
        return -1;  // Unsupported DIB header
    }

    int32_t width = (int32_t)read_u32(buf + 18);
    int32_t height = (int32_t)read_u32(buf + 22);
    uint16_t bpp = read_u16(buf + 28);
    uint32_t compression = read_u32(buf + 30);

    if (width <= 0 || width > 256 || bpp != 8 || compression != 0) {
        return -1;  // Only 8-bit uncompressed, max 256px wide
    }

    // height > 0 means bottom-up (standard), height < 0 means top-down
    int bottom_up = 1;
    if (height < 0) {
        height = -height;
        bottom_up = 0;
    }
    if (height <= 0 || height > 256) {
        return -1;
    }

    // Validate pixel data fits
    uint32_t row_size = ((uint32_t)width + 3) & ~3u;  // Padded to 4 bytes
    uint32_t pixel_data_size = row_size * (uint32_t)height;

    if (pixel_offset + pixel_data_size > buf_size) {
        return -1;  // File too small
    }

    uint8_t *pixel_data = buf + pixel_offset;

    // Flip rows in place if bottom-up (convert to top-down)
    if (bottom_up) {
        uint8_t temp_row[256];  // Max 256 px wide
        for (int y = 0; y < height / 2; y++) {
            uint8_t *top = pixel_data + y * row_size;
            uint8_t *bot = pixel_data + (height - 1 - y) * row_size;
            memcpy(temp_row, top, row_size);
            memcpy(top, bot, row_size);
            memcpy(bot, temp_row, row_size);
        }
    }

    // Extract pixel data without padding (if row_size != width)
    // For simplicity, if padding exists, compact rows in place
    if (row_size != (uint32_t)width) {
        for (int y = 1; y < height; y++) {
            memmove(pixel_data + y * width, pixel_data + y * row_size, width);
        }
    }

    out->width = (uint16_t)width;
    out->height = (uint16_t)height;
    out->pixels = pixel_data;
    out->pixels_size = (size_t)width * (size_t)height;

    return 0;
}
