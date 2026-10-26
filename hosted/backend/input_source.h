/*
 * input_source.h - the input side of the hosted builds, as a table.
 *
 * The screen and the sound already have a function table each
 * (display_backend_t, audio_backend_t); this is the same thing for input.
 * An input source turns whatever its host delivers (the browser's event ring,
 * SDL events, the injection socket, later evdev and wlroots) into
 * fmrb_host_send_* calls, the one funnel every input path on every target
 * ends in. Key semantics are the ones rd_input.c and the browser established:
 * key_code = scancode = HID usage ID, modifiers in FMRB_KEYMAP_MOD_* bits.
 *
 * The build lists its sources in fmrb_input_sources (NULL-terminated; the
 * browser's list is fixed, the native program's is filled from the machine
 * definition before the machine starts), and input_hosted.c starts each from
 * usb_task_init -- the call boot.c already makes on every target. That keeps the device path (usb_task.c, the USB host
 * stack with its device enumeration) untouched: on the hosted builds the
 * usb_task_* names are only the door, there is no USB bus behind them.
 *
 * A source must never block its task in a system call: the scheduler is
 * cooperative (hosted/port/), so a blocked task stops every task. Poll
 * without waiting and wait with a FreeRTOS delay.
 */
#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    /* Bring the source up and start its task. A failure is logged and the
     * other sources still start. */
    fmrb_err_t (*start)(void);
} fmrb_input_source_t;

/* The sources this build runs, NULL-terminated. Defined by the platform. */
extern const fmrb_input_source_t *const *fmrb_input_sources;

#ifdef __cplusplus
}
#endif
