/* rd_encoder_stub.c - rd_encoder_jpeg.h for the wasm build.
 *
 * EXPORT_FRAME rides the P4's hardware JPEG encoder; out of scope on wasm
 * (implementation plan scope table), so the encode fails cleanly and the
 * command answers an error.
 */

#include "rd_encoder_jpeg.h"

fmrb_err_t rd_encoder_jpeg_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return FMRB_OK;
}

void rd_encoder_jpeg_unlock(void) {}

fmrb_err_t rd_encoder_jpeg_init(uint16_t src_w, uint16_t src_h, uint8_t quality)
{
    (void)src_w; (void)src_h; (void)quality;
    return FMRB_ERR_NOT_SUPPORTED;
}

void rd_encoder_jpeg_deinit(void) {}

fmrb_err_t rd_encoder_jpeg_encode(const uint16_t *pixels,
                                  const uint8_t **out, size_t *out_len)
{
    (void)pixels; (void)out; (void)out_len;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t rd_encoder_jpeg_encode_q(const uint16_t *pixels, uint8_t quality,
                                    const uint8_t **out, size_t *out_len)
{
    (void)pixels; (void)quality; (void)out; (void)out_len;
    return FMRB_ERR_NOT_SUPPORTED;
}
