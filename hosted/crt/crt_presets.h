/*
 * crt_presets.h - the CRT look's sets of strengths (doc/web_crt C6b): the
 * one table that the browser build (wasm/web/crt.js, its Display section's
 * buttons) and the native PC settings (native/apps/linux_settings.app.rb,
 * the Display page's buttons) both use. rake webcrt:gen writes it into
 * those two files and rake webcrt:test fails while either is out of date,
 * so edit the rows here and run rake webcrt:gen.
 *
 * Each row is 0..100 for scanline, mask, glow, curve, vignette, ntsc,
 * flicker, noise (crt.js's KEYS, crt_set's order). A set leaves the look's
 * on/off alone.
 *
 * Standard is also the strengths a fresh look gets (chosen by the user on
 * the GPD, doc/web_crt C6); Strong is the one a demo shows best.
 */
#pragma once

#define CRT_PRESET_STANDARD 55, 30, 30, 15, 40, 0, 0, 0
#define CRT_PRESET_STRONG 75, 50, 45, 25, 55, 50, 50, 50
