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

/** @brief Same, at a quality of the caller's choosing. */
fmrb_err_t rd_encoder_jpeg_encode_q(const uint16_t *pixels, uint8_t quality,
                                    const uint8_t **out_buf, size_t *out_len);

/**
 * @brief Claim the encoder for one encode-and-use.
 *
 * There is one staging buffer and one output buffer, and the output stays
 * the caller's until the next encode -- so a second user has to wait not
 * just for the encode but for whatever the first one does with the bytes.
 * Every caller of rd_encoder_jpeg_encode* takes this first and holds it
 * until it is done reading out_buf. Init and deinit take it too.
 *
 * @return FMRB_ERR_TIMEOUT when it was not free in time.
 */
fmrb_err_t rd_encoder_jpeg_lock(uint32_t timeout_ms);
void       rd_encoder_jpeg_unlock(void);

#ifdef __cplusplus
}
#endif
