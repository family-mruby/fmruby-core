/*
 * native_backends.h - the one place the native program turns a machine
 * definition (native_machine.h) into backends.
 *
 * Three kinds, all driven from the process's main thread (outside FreeRTOS):
 *
 *   output       the screen: takes the composited frame from native_frame.c
 *                and puts it on a surface (SDL window, fbdev, DRM) through
 *                the shared present core (native/present/). Also owns the
 *                main loop's wait (its own events: SDL's queue, DRM's page
 *                flips).
 *   audio sink   drains native_audio.h's ring into a device (SDL, ALSA) on a
 *                thread of its own, or nothing.
 *   input        the machine's input sources (input_source.h): the inject
 *                socket always, plus the window (sdl), the devices (evdev)
 *                or the compositor's seat (wlroots; it hands its events to
 *                the same main-thread ring as the window).
 *
 * A backend the definition names but this build does not have (compiled
 * without libdrm or ALSA, or one of a later phase) is refused with the
 * reason -- never swapped for another. Nothing above this file knows which
 * backend runs.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "input_source.h"
#include "native_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    /* Open the device before the machine starts. False, with the reason,
     * when it cannot be had. */
    bool (*open)(const native_machine_t *m, char *why, size_t why_len);
    /* Wait up to wait_ms for the output's own events. True to quit. */
    bool (*pump)(int wait_ms);
    /* The frame on hand (w x h RGB565, stays valid until the next call);
     * changed is true when it differs from the last call's. */
    void (*present)(const uint16_t *px, int w, int h, bool changed);
    void (*close)(void);
    /* Requests of its own on the control socket (native_ctl.h), or NULL. */
    bool (*ctl)(const char *request, char *reply, size_t cap);
    /* The output's size once open (as the picture is laid out on it), for
     * the screen form "output"; NULL where the form is not offered. */
    bool (*size)(int *w, int *h);
} native_output_t;

typedef struct {
    const char *name;
    /* Open the device and start draining. False, with the reason. */
    bool (*open)(const native_machine_t *m, char *why, size_t why_len);
    void (*close)(void);
} native_audio_sink_t;

typedef struct {
    const native_output_t *output;
    const native_audio_sink_t *audio;
} native_backends_t;

/* The backends of m, checked against this build. False, with the reason. */
bool native_backends_select(const native_machine_t *m, native_backends_t *out,
                            char *why, size_t why_len);

/* Open the input side of m (evdev devices) and fill the machine's source
 * list. False, with the reason. */
bool native_backends_open_input(const native_machine_t *m, char *why, size_t why_len);

/* What this build has, for --help and the refusals ("sdl, fbdev, drm"). */
const char *native_backends_outputs_built(void);
const char *native_backends_audio_built(void);
const char *native_backends_inputs_built(void);

/* The individual backends (native/output/, native/audio/). */
extern const native_output_t native_output_sdl;
extern const native_output_t native_output_fbdev;
#if NATIVE_HAVE_DRM
extern const native_output_t native_output_drm;
#endif
#if NATIVE_HAVE_WLROOTS
/* The program as a Wayland compositor (native/wlr/, L8): output and input
 * at once. */
extern const native_output_t native_output_wlroots;
#endif
extern const native_audio_sink_t native_audio_sdl;
extern const native_audio_sink_t native_audio_none;
#if NATIVE_HAVE_ALSA
extern const native_audio_sink_t native_audio_alsa;
#endif

#ifdef __cplusplus
}
#endif
