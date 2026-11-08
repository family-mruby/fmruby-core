/*
 * crt_warp.h - the CRT look's curve (doc/web_crt/), in C for the native
 * compositor's pointer (native/wlr/wl_crt.c): the same formula as the
 * shader (crt.frag) and the browser build's crt.js, which
 * rake webcrt:test holds to the same values.
 *
 * Positions are 0..1 across the display or the picture, y down. Pure code
 * (libm only), so the host test builds it alone.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The strengths a fresh look gets, 0..100 each: the Standard set
 * (crt_presets.h, also crt.js's DEFAULTS). */
#include "crt_presets.h"
/* The time effects (flicker, noise) draw this many frames a second. */
#define CRT_TIME_FPS 30

/* Slider value (0..100) -> the curve's coefficient (0..0.25). */
double crt_curve_coef(int curve);

/* Display position -> picture position (the shader's direction). Outside
 * the picture when either result is < 0 or >= 1. */
void crt_warp_to_source(double u, double v, int curve, double *x, double *y);

/* Picture position -> display position: the inverse, by Newton steps. */
void crt_warp_to_display(double x, double y, int curve, double *u, double *v);

/* Inside the picture: 0 <= x < 1 and 0 <= y < 1. */
bool crt_inside(double x, double y);

/* How strongly a fine-grained effect may show at a magnification (display
 * pixels per picture pixel): smoothstep from lo to hi. */
double crt_fine_weight(double scale, double lo, double hi);

#ifdef __cplusplus
}
#endif
