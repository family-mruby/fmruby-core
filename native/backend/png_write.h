/*
 * png_write.h - an RGB565 frame as a PNG, with no library.
 *
 * Truecolour 8-bit RGB, every 5/6-bit channel expanded to 8 bits by bit
 * replication, so the RGB565 value of every pixel can be read back exactly.
 * Deflate "stored" blocks (no compression): a 640x360 screen is ~690 KB,
 * which a screenshot can afford, and the writer stays free of zlib for the
 * cross builds to come.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encode w x h RGB565 pixels. Returns a malloc'd buffer (free() it) and its
 * size in *out_len, or NULL. */
uint8_t *png_write_rgb565(const uint16_t *px, int w, int h, size_t *out_len);

#ifdef __cplusplus
}
#endif
