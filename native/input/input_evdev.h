/*
 * input_evdev.h - the device half of the evdev input source.
 *
 * native_evdev_open() runs on the main thread before the machine starts: it
 * finds the devices the machine definition names (input.devices), opens them
 * non-blocking and keeps the keyboards and mice. "auto" looks through
 * /dev/input/event* for anything with letter keys (a keyboard) or with
 * relative X/Y and a left button (a mouse); a path (/dev/input/event3,
 * /dev/input/by-path/...-event-kbd) is taken as it is. Devices plugged in
 * later are not picked up (L2: at start only).
 *
 * fmrb_input_source_evdev is then started by the machine (input_hosted.c);
 * its task polls the descriptors and decodes what it reads with
 * evdev_decode.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "input_source.h"
#include "native_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open the definition's devices. False, with the reason, when none could be
 * opened: no event devices, no permission, nothing that is a keyboard or a
 * mouse. */
bool native_evdev_open(const native_machine_t *m, char *why, size_t why_len);

extern const fmrb_input_source_t fmrb_input_source_evdev;

#ifdef __cplusplus
}
#endif
