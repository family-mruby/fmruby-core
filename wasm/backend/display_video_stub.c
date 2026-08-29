/*
 * display_video_stub.c - display_p4_video.h for the wasm build.
 *
 * Video playback and hardware JPEG decoding are PPA/P4 hardware features;
 * the plan keeps them out of scope (doc/wasm/implementation_plan.md,
 * scope table). Everything answers "not supported / inactive" so the
 * display task's unmodified loop and command dispatch keep working.
 */

#include "display_p4_video.h"

#include <stddef.h>

bool display_p4_jpeg_is_jpeg(const uint8_t *data, size_t len)
{
    return len >= 2 && data && data[0] == 0xFF && data[1] == 0xD8;
}

uint8_t *display_p4_jpeg_decode(const uint8_t *jpeg, size_t len,
                                uint16_t *out_w, uint16_t *out_h,
                                uint16_t *out_stride_px)
{
    (void)jpeg; (void)len; (void)out_w; (void)out_h; (void)out_stride_px;
    return NULL;
}

void display_p4_jpeg_free_output(uint8_t *buf) { (void)buf; }

uint8_t *display_p4_jpeg_alloc_input(size_t size, size_t *out_alloc)
{
    (void)size;
    if (out_alloc) *out_alloc = 0;
    return NULL;
}

void display_p4_jpeg_free_input(uint8_t *buf) { (void)buf; }

fmrb_err_t display_p4_video_open(uint16_t canvas_id, int16_t x, int16_t y,
                                 const char *path, uint16_t fps, uint8_t flags,
                                 uint16_t *out_w, uint16_t *out_h)
{
    (void)canvas_id; (void)x; (void)y; (void)path; (void)fps; (void)flags;
    (void)out_w; (void)out_h;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t display_p4_video_control(uint8_t action)
{
    (void)action;
    return FMRB_ERR_NOT_SUPPORTED;
}

void display_p4_video_get_status(display_p4_video_status_t *out)
{
    if (out) {
        out->state = 0;
        out->canvas_id = 0;
        out->frames_shown = 0;
        out->frames_dropped = 0;
    }
}

bool display_p4_video_is_active(void) { return false; }

bool display_p4_video_take_frame(const uint8_t **pixels, uint16_t *w,
                                 uint16_t *h, uint16_t *stride_px,
                                 uint16_t *canvas_id, int16_t *x, int16_t *y)
{
    (void)pixels; (void)w; (void)h; (void)stride_px;
    (void)canvas_id; (void)x; (void)y;
    return false;
}

void display_p4_video_release_frame(void) {}
void display_p4_video_note_copy_us(uint32_t us) { (void)us; }
void display_p4_video_canvas_gone(uint16_t canvas_id) { (void)canvas_id; }
