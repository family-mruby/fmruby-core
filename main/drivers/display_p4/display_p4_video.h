// Motion-JPEG playback for Family mruby Modern (ESP32-P4).
//
// Two things live here:
//   1. a thin wrapper over the SoC's JPEG decoder (one blob -> RGB565), used
//      by CREATE_IMAGE_FROM_FILE for still pictures too;
//   2. a player task that walks a file of concatenated JPEG frames on its own
//      clock and hands finished frames to the display task.
//
// The player never touches canvas memory. It publishes a decoded frame and
// display_p4_task copies it into the canvas from its own loop, so canvas
// buffers stay single-writer exactly as the compositor assumes.

#pragma once

#include "fmrb_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Still decoding ---------------------------------------------------------

/** @brief True when the blob starts with a JPEG SOI marker. */
bool display_p4_jpeg_is_jpeg(const uint8_t *data, size_t len);

/**
 * @brief Decode one JPEG blob into a freshly allocated RGB565 buffer.
 *
 * The decoder writes rows padded up to a multiple of 16 pixels, so the caller
 * must step by *out_stride_px, not by *out_w. Free with
 * display_p4_jpeg_free_output().
 *
 * @return the pixel buffer, or NULL on failure.
 */
uint8_t *display_p4_jpeg_decode(const uint8_t *jpeg, size_t len,
                                uint16_t *out_w, uint16_t *out_h,
                                uint16_t *out_stride_px);

/** @brief Free a buffer returned by display_p4_jpeg_decode(). */
void display_p4_jpeg_free_output(uint8_t *buf);

/**
 * @brief Allocate a buffer the decoder can read a JPEG from.
 *
 * The input buffer has its own alignment rules; a plain malloc is not enough.
 */
uint8_t *display_p4_jpeg_alloc_input(size_t size, size_t *out_alloc);

/** @brief Free a buffer returned by display_p4_jpeg_alloc_input(). */
void display_p4_jpeg_free_input(uint8_t *buf);

// ---- Player -----------------------------------------------------------------

typedef struct {
    uint8_t  state;            // fmrb_link_graphics_video_state_t
    uint16_t canvas_id;
    uint32_t frames_shown;
    uint32_t frames_dropped;
} display_p4_video_status_t;

/**
 * @brief Point the player at a file and at a rect inside a canvas.
 *
 * Reads the first frame's header to learn the picture size; playback starts
 * paused, so the caller can lay the window out before the first frame lands.
 * Opening while another file plays replaces it.
 */
fmrb_err_t display_p4_video_open(uint16_t canvas_id, int16_t x, int16_t y,
                                 const char *path, uint16_t fps, uint8_t flags,
                                 uint16_t *out_w, uint16_t *out_h);

/** @brief Play / pause / stop / rewind (fmrb_link_graphics_video_action_t). */
fmrb_err_t display_p4_video_control(uint8_t action);

/** @brief Current player state. Always fills @p out. */
void display_p4_video_get_status(display_p4_video_status_t *out);

/** @brief True while a file is open (playing or paused). */
bool display_p4_video_is_active(void);

/**
 * @brief Take the frame waiting to be shown, if any.
 *
 * Called from the display task. On true the caller owns the pixels until it
 * calls display_p4_video_release_frame().
 */
bool display_p4_video_take_frame(const uint8_t **pixels, uint16_t *w,
                                 uint16_t *h, uint16_t *stride_px,
                                 uint16_t *canvas_id, int16_t *x, int16_t *y);

/** @brief Return the buffer taken by display_p4_video_take_frame(). */
void display_p4_video_release_frame(void);

/**
 * @brief Report how long the display task took to copy one frame into the
 *        canvas (microseconds).
 *
 * Feeds the periodic "profile" log line, which reports read / decode / copy
 * side by side -- the three stages live in two tasks, so the copy time has to
 * be handed back here to be logged with the others.
 */
void display_p4_video_note_copy_us(uint32_t us);

/** @brief Stop playback if it owns this canvas (called when it is deleted). */
void display_p4_video_canvas_gone(uint16_t canvas_id);

#ifdef __cplusplus
}
#endif
