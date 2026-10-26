/*
 * native_frame.h - the composited screen, handed from the display task to
 * the output.
 *
 * The display task (a FreeRTOS task, one of the cooperative port's threads)
 * puts each finished frame here; the native program's main thread -- which
 * owns SDL, and is not a FreeRTOS task -- takes the newest one when the
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

/* Copy the frame on hand into dst (room for cap_px pixels). Returns false
 * when there is none yet or it does not fit. */
bool native_frame_copy(uint16_t *dst, int cap_px, int *w, int *h, uint32_t *seq);

#ifdef __cplusplus
}
#endif
