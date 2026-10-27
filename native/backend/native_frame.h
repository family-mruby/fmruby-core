/*
 * native_frame.h - the composited screen, handed from the display task to
 * the output.
 *
 * The display task (a FreeRTOS task, one of the cooperative port's threads)
 * puts each finished frame here; the native program's main thread -- which
 * owns the output (SDL, fbdev, DRM), and is not a FreeRTOS task -- takes the newest one when the
 * sequence number moves, presents it, and answers the screenshot request from
 * the same copy. The same split the browser has between the machine and the
 * page (wasm/backend/display_backend_wasm.cpp), with a lock because these are
 * two real threads.
 *
 * The pixels stay RGB565 as composited (PPA-native order: one uint16_t per
 * pixel, red in the top bits), so nothing is lost before a screenshot.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest frame the display task can hand over (the fullscreen
 * high-resolution size or the base size, whichever is larger). Called from
 * the display backend's init. */
bool native_frame_init(int base_w, int base_h);

/* A whole frame. */
void native_frame_put(const uint16_t *fb, int w, int h);

/* The canvases a frame was composited from (bottom to top, as the display
 * task blended them), for the view (L9e). */
typedef struct {
    uint16_t canvas_id;
    int x, y, w, h;     /* the whole canvas on the frame */
} native_frame_canvas_t;

/* Decides the part of a frame to show from its canvases: true and view
 * (x, y, w, h) for a part, false for all of it. Called on the display task
 * with each whole frame. */
typedef bool (*native_frame_view_fn)(const native_frame_canvas_t *c, int n, int fb_w, int fb_h, int view[4]);
void native_frame_set_view_fn(native_frame_view_fn fn);

/* A whole frame with the canvases it was made of: its view is decided
 * (native_frame_set_view_fn) and kept with it. */
void native_frame_put_canvases(const uint16_t *fb, int w, int h, const native_frame_canvas_t *c, int n);

/* One already-composited rectangle of a frame of fb_w x fb_h (the cursor fast
 * path). Ignored when the frame on hand is of another size. */
void native_frame_put_rect(const uint16_t *block, int x0, int y0, int w, int h,
                           int fb_w, int fb_h);

/* Sequence number of the frame on hand; moves on every put. 0 before the
 * first frame. */
uint32_t native_frame_seq(void);

/* The base (desktop) frame size the display was brought up with, 0x0 before
 * that. */
void native_frame_base_size(int *w, int *h);

/* Size of the frame on hand (changes with the high-resolution mode), 0x0
 * before the first frame. */
void native_frame_size(int *w, int *h);

/* An eventfd (or any descriptor a uint64_t can be written to) to wake the
 * output's thread with on every put, so a loop waiting in poll sees a new
 * frame at once (the compositor, L8). -1 for none. Set before the machine
 * starts. */
void native_frame_set_wake_fd(int fd);

/* Copy the frame on hand into dst (room for cap_px pixels). Returns false
 * when there is none yet or it does not fit. */
bool native_frame_copy(uint16_t *dst, int cap_px, int *w, int *h, uint32_t *seq);

/* The view of the frame the calling thread copied last (0x0: all of it). */
void native_frame_copied_view(int view[4]);

#ifdef __cplusplus
}
#endif
