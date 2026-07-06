#pragma once

#include <stdint.h>
#include <stddef.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// P4 hardware JPEG encoder wrapper for the MJPEG remote-desktop stream.
// Input: RGB565 (non-swapped) frames from display_p4 capture. The source
// width (426) is row-padded to 432 internally (JPEG MCU alignment); the
// viewer crops the padding.

fmrb_err_t rd_encoder_jpeg_init(uint16_t src_w, uint16_t src_h, uint8_t quality);
void       rd_encoder_jpeg_deinit(void);

/** Encoded output width (source width padded up to a multiple of 16). */
uint16_t rd_encoder_jpeg_width(void);

/**
 * @brief Encode one RGB565 frame to JPEG.
 * @param pixels   Source RGB565 pixels (src_w x src_h, packed rows)
 * @param out_buf  Receives a pointer to the internal output buffer (valid
 *                 until the next encode call)
 * @param out_len  Encoded byte count
 */
fmrb_err_t rd_encoder_jpeg_encode(const uint16_t *pixels,
                                  const uint8_t **out_buf, size_t *out_len);

#ifdef __cplusplus
}
#endif
