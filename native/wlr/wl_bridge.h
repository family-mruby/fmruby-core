/*
 * wl_bridge.h - Linux windows as Family mruby windows (doc/linux_native/,
 * L9a). See wl_bridge.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "input_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The task that keeps the stand-ins and the compositor in step; started as
 * one of the machine's input sources when input.backend is wlroots. */
extern const fmrb_input_source_t fmrb_input_source_wl_bridge;

/* For the stand-in app with this pid (FmrbApp.wl_window): 1 and its title
 * while the Linux window is there, 0 once it has gone, -1 when pid is no
 * stand-in (yet). */
int native_wl_window_state(int pid, char *title, size_t cap);

/* The CRT look for the settings window (doc/web_crt, C5; FmrbApp.crt_state
 * and crt_set). native_crt_state writes "" when the compositor cannot draw
 * the look here (not the compositor, no GLES), else "crt" or "sharp" and
 * the five strengths: "crt,55,30,30,15,40". native_crt_ask asks the
 * compositor for a setting (it applies at once); false when it cannot draw
 * the look. */
void native_crt_state(char *out, size_t cap);
bool native_crt_ask(bool crt, const int strengths[5]);

/* The stand-in's close button (FmrbApp.wl_close): ask the Linux window to
 * close. False when pid is no stand-in or the window has gone. */
bool native_wl_window_close(int pid);

#ifdef __cplusplus
}
#endif
