#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// P4 hardware H.264 encoder wrapper (Phase 2).
// On chip rev < v3.0 the hardware encoder only accepts the
// O_UYY_E_VYY YUV420 layout, so each RGB565 capture frame is first
// color-converted (and 426 -> 432 padded) by the PPA SRM engine.
// Output is an Annex B access unit; IDR frames carry SPS/PPS
// automatically.

typedef struct {
    uint16_t src_w, src_h;
    uint8_t  fps;
    uint8_t  gop;
    uint32_t bitrate;       // bits per second
} rd_h264_config_t;

fmrb_err_t rd_encoder_h264_init(const rd_h264_config_t *cfg);
void       rd_encoder_h264_deinit(void);

/** Encoded output width (source width padded up to a multiple of 16). */
uint16_t rd_encoder_h264_width(void);

/** Force the next encoded frame to be an IDR (client join / recovery). */
void rd_encoder_h264_request_idr(void);

/**
 * @brief Encode one RGB565 frame.
 * @param pixels    Source RGB565 pixels (src_w x src_h)
 * @param pts_ms    Presentation timestamp in milliseconds
 * @param out_buf   Receives internal output buffer (valid until next call)
 * @param out_len   Encoded byte count
 * @param out_is_idr True when the encoded frame is an IDR
 */
fmrb_err_t rd_encoder_h264_encode(const uint16_t *pixels, uint32_t pts_ms,
                                  const uint8_t **out_buf, size_t *out_len,
                                  bool *out_is_idr);

#ifdef __cplusplus
}
#endif
