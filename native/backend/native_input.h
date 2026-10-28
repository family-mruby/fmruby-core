/*
 * native_input.h - the native program's input sources (input_source.h).
 *
 *   sdl     the window's keys, buttons, motion and wheel. The main thread
 *           (output_sdl.c, outside FreeRTOS) pushes them into a ring; the
 *           source's task drains it into fmrb_host_send_*. The browser's ring
 *           (wasm/backend/input_wasm.c), with a real thread on the far side.
 *   inject  the headless way in: the simulation's packet format
 *           ([type u8][len u16 LE][payload], main/include/fmrb_hid_event.h)
 *           on a Unix datagram socket of this program's own
 *           (<run dir>/inject), so tools/fmrb_input.rb drives this build the
 *           way it drives the simulation, side by side with it.
 *
 * Both deliver HID usage IDs as scancode and key code alike, with modifiers
 * in FMRB_KEYMAP_MOD_* bits -- the browser's convention, which a key handler
 * written for any target already reads (ev[:scancode]).
 */
#pragma once

#include <stdint.h>

#include "input_source.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NATIVE_INPUT_KEY_DOWN = 1,   /* a=HID usage, b=FMRB_KEYMAP_MOD_* */
    NATIVE_INPUT_KEY_UP,         /* a=HID usage, b=mod */
    NATIVE_INPUT_MOUSE_BUTTON,   /* a=x, b=y, c=button (1=L 2=M 3=R), d=state */
    NATIVE_INPUT_MOUSE_MOVE,     /* a=x, b=y (frame coordinates) */
    NATIVE_INPUT_MOUSE_WHEEL,    /* a=x, b=y, c=notches (signed) */
    NATIVE_INPUT_CURSOR,         /* a=1 show / 0 hide Family mruby's cursor (the
                                  * compositor, over a Linux window) */
    NATIVE_INPUT_TEXT,           /* a=byte count (<= 12), b..d=the bytes: text the
                                  * PC's IME committed (native_ime.h) */
    NATIVE_INPUT_WINOP,          /* a=winops_do_t: a window operation on the
                                  * window in front (the compositor took Super
                                  * and an arrow, L13; native_winops.h) */
};

/* From the main thread: one event for the sdl source. Never blocks; when the
 * machine has fallen 256 events behind the oldest are dropped. */
void native_input_push(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d);

/* Where the inject source binds (set before the machine starts). */
void native_input_set_inject_path(const char *path);

extern const fmrb_input_source_t fmrb_input_source_sdl;
extern const fmrb_input_source_t fmrb_input_source_inject;

#ifdef __cplusplus
}
#endif
