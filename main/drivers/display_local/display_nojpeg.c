// display_nojpeg.c - the JPEG side of the local display for a chip without
// the P4's JPEG engine
//
// display_local_task.cpp decodes JPEG stills and plays motion JPEG through
// display_local_video.cpp, and exports frames through the remote desktop's
// encoder (rd_encoder_jpeg.c). Both sit on the ESP32-P4's JPEG hardware. A
// board that draws locally on another chip (the AtomS3R, an ESP32-S3) builds
// this file instead: a JPEG is reported as not decodable, a video does not
// open, and an export answers an error -- the same answers the commands give
// on a board where the file is missing. PNG and BMP images are unaffected.

#include "display_local_video.h"
#include "rd_encoder_jpeg.h"
#include "fmrb_link_protocol.h"

#include <string.h>

// ---- Still decoding ---------------------------------------------------------

bool display_local_jpeg_is_jpeg(const uint8_t *data, size_t len)
{
    return len >= 2 && data && data[0] == 0xFF && data[1] == 0xD8;
}

uint8_t *display_local_jpeg_decode(const uint8_t *jpeg, size_t len,
                                uint16_t *out_w, uint16_t *out_h,
                                uint16_t *out_stride_px)
{
    (void)jpeg; (void)len; (void)out_w; (void)out_h; (void)out_stride_px;
    return NULL;
}

void display_local_jpeg_free_output(uint8_t *buf)
{
    (void)buf;
}

uint8_t *display_local_jpeg_alloc_input(size_t size, size_t *out_alloc)
{
    (void)size;
    if (out_alloc) *out_alloc = 0;
    return NULL;
}

void display_local_jpeg_free_input(uint8_t *buf)
{
    (void)buf;
}

// ---- Player -----------------------------------------------------------------

fmrb_err_t display_local_video_open(uint16_t canvas_id, int16_t x, int16_t y,
                                 const char *path, uint16_t fps, uint8_t flags,
                                 uint16_t *out_w, uint16_t *out_h)
{
    (void)canvas_id; (void)x; (void)y; (void)path; (void)fps; (void)flags;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t display_local_video_control(uint8_t action)
{
    (void)action;
    return FMRB_ERR_NOT_SUPPORTED;
}

void display_local_video_get_status(display_local_video_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->state = FMRB_LINK_GFX_VIDEO_STATE_IDLE;
}

bool display_local_video_is_active(void)
{
    return false;
}

bool display_local_video_take_frame(const uint8_t **pixels, uint16_t *w,
                                 uint16_t *h, uint16_t *stride_px,
                                 uint16_t *canvas_id, int16_t *x, int16_t *y)
{
    (void)pixels; (void)w; (void)h; (void)stride_px;
    (void)canvas_id; (void)x; (void)y;
    return false;
}

void display_local_video_release_frame(void) {}

void display_local_video_note_copy_us(uint32_t us)
{
    (void)us;
}

void display_local_video_canvas_gone(uint16_t canvas_id)
{
    (void)canvas_id;
}

// ---- Frame export (EXPORT_FRAME) --------------------------------------------

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

uint16_t rd_encoder_jpeg_width(void)
{
    return 0;
}

fmrb_err_t rd_encoder_jpeg_encode(const uint16_t *pixels,
                                  const uint8_t **out_buf, size_t *out_len)
{
    (void)pixels; (void)out_buf; (void)out_len;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t rd_encoder_jpeg_encode_q(const uint16_t *pixels, uint8_t quality,
                                    const uint8_t **out_buf, size_t *out_len)
{
    (void)pixels; (void)quality; (void)out_buf; (void)out_len;
    return FMRB_ERR_NOT_SUPPORTED;
}
