/**
 * @file fmrb_spx_gfx.h
 * @brief Spinel FFI shim for the FmrbGfx operations not on the app boundary:
 *        the drawer (FmrbGfx.new, _set_origin, destroy), and the adaptors of
 *        the boundary's functions that give back a record or bytes.
 *
 * Spinel-generated C cannot take an mrb_state and cannot receive C structs
 * through its FFI, so every drawing primitive is exposed as a plain C function
 * whose first argument is the FmrbGfx object's drawer number (fmrb_gfx_drawer.h:
 * the canvas, the drawing origin, whether it draws into a sprite image; the
 * Spinel FmrbGfx keeps the number as a plain Integer instance variable, as the
 * boundary's functions take it). See doc/spinel_aot/phase4.md T4-1 and the
 * kernel precedent in main/kernel/fmrb_spx.h.
 *
 * Return convention (mirrors fmrb_spx.h):
 * - Drawing commands return 0 on success, negative on error (a shim-local
 *   negative code). They never raise; the Ruby base class decides whether an
 *   error is fatal.
 * - Queries that yield a single scalar return that value when >= 0 and a
 *   negative code on error/absence.
 * - Structured results cross the boundary as fixed-layout little-endian byte
 *   buffers returned via :binstr (a real Spinel String read with getbyte); the
 *   byte length is published in sp_ffi_bin_len. An empty String means "nil".
 */
#ifndef FMRB_SPX_GFX_H
#define FMRB_SPX_GFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared negative error codes (kept numerically identical to fmrb_spx.h). */
#ifndef FMRB_SPX_ERR
#define FMRB_SPX_ERR (-1)          /**< generic failure */
#define FMRB_SPX_ERR_RANGE (-2)    /**< argument out of range */
#define FMRB_SPX_ERR_CAP (-3)      /**< caller buffer too small */
#endif

/* The FmrbGfx methods on the app boundary are not here: their flat functions
   are generated from sig/ (fmrb_gen/fmrb_boundary.h, doc/app_boundary),
   sprites and video included. This file keeps the drawer, which the engine's
   FmrbGfx opens, lays out and closes. */

/* --- the drawer (FmrbGfx.new, _set_origin, destroy) --- */
/** A drawer of the app on a canvas: its number, or negative when none is left. */
int fmrb_spx_gfx_drawer_open(int canvas_id, int width, int height);
int fmrb_spx_gfx_drawer_set_origin(int drawer, int x, int y, int w, int h);
int fmrb_spx_gfx_drawer_close(int drawer);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPX_GFX_H */
