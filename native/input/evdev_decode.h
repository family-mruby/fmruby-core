/*
 * evdev_decode.h - the processing half of the evdev input source: Linux
 * input events (struct input_event, as read from /dev/input/event*) to the
 * firmware's key and pointer events.
 *
 * No device, no FreeRTOS, no allocation: input_evdev.c opens the devices and
 * hands what it reads here with the screen geometry; the host tests feed
 * recorded events through a pipe and look at what comes out (test/native/).
 *
 *   keys     Linux code -> HID usage (linux_keys.c, or a machine's extra
 *            table first, keymap_extra.h), with the modifier bits each
 *            device keeps for itself. Auto-repeat (value 2) is dropped: the
 *            firmware repeats on its own, as with the SDL window.
 *   pointer  relative motion moves one cursor all mice share, in screen
 *            coordinates, with the USB mouse's rule (usb_task.c): starts at
 *            the centre, mouse_scale_x/y with the fractional part carried,
 *            scaled by the current screen against the base one, clamped.
 *            Motion, buttons and the wheel of one report (up to SYN_REPORT)
 *            go out in that order, so a click lands where the report moved.
 */
#pragma once

#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>

#include "keymap_extra.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*key)(void *ud, bool down, uint8_t usage, uint8_t mods, bool force_us);
    void (*move)(void *ud, int x, int y);
    void (*button)(void *ud, int x, int y, int button, bool down);  /* 1=L 2=M 3=R */
    void (*wheel)(void *ud, int x, int y, int notches);             /* + away from the user */
} evdev_sink_t;

/* The screen the pointer moves on, read again for every batch. */
typedef struct {
    int screen_w, screen_h;     /* current (640x360 in the high-resolution mode) */
    int base_w, base_h;         /* the system config's display size */
    double scale_x, scale_y;    /* mouse_scale_x / mouse_scale_y */
} evdev_geometry_t;

/* The cursor every pointing device moves. */
typedef struct {
    int x, y;
    bool placed;
    double ax, ay;              /* fractional motion carried over */
} evdev_pointer_t;

#define EVDEV_PENDING_BUTTONS 8

typedef struct {
    fmrb_keymap_extra_fn extra; /* NULL: linux_keys.c only */
    evdev_pointer_t *pointer;
    uint8_t mods;
    int dx, dy, wheel;
    int n_buttons;
    struct { int button; bool down; } buttons[EVDEV_PENDING_BUTTONS];
} evdev_decoder_t;

void evdev_decoder_init(evdev_decoder_t *d, evdev_pointer_t *pointer, fmrb_keymap_extra_fn extra);

/* One event. */
void evdev_decode(evdev_decoder_t *d, const struct input_event *ev,
                  const evdev_geometry_t *geo, const evdev_sink_t *sink, void *ud);

/* Every event waiting on a non-blocking descriptor (a device, or a pipe in
 * the tests). Returns the number of events read, or -1 when the device has
 * gone (ENODEV, or end of file). Never blocks. */
int evdev_drain_fd(int fd, evdev_decoder_t *d, const evdev_geometry_t *geo,
                   const evdev_sink_t *sink, void *ud);

#ifdef __cplusplus
}
#endif
