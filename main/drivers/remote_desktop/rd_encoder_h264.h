#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// P4 hardware H.264 encoder wrapper (Phase 2).
// The hardware encoder accepts RGB565_LE directly (internal color
// conversion), so frames go straight from the display capture buffer
// after 426 -> 432 row padding. Output is an Annex B access unit; IDR
// frames carry SPS/PPS automatically.

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
