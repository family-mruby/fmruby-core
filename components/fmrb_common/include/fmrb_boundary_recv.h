/**
 * @file fmrb_boundary_recv.h
 * @brief Receiver numbers of the app boundary's FmrbGfx functions
 *
 * A flat boundary function of an FmrbGfx method (fmrb_gen/fmrb_boundary.h,
 * generated from sig/) takes the receiver as a number. A number of 0 or more
 * is a canvas id, and the coordinates are the canvas's own (what Spinel,
 * MicroPython and wasm apps pass; their wrappers add the origin themselves).
 *
 * mruby and Lua keep the drawing origin in their C object. Their receiver
 * hook binds that target for the one call and passes FMRB_GFX_RECV_BOUND;
 * the flat function then adds the bound origin. This is an interim form:
 * doc/app_boundary W4b moves every engine's origin below the boundary.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The receiver number meaning "the target bound by fmrb_gfx_recv_bind". */
#define FMRB_GFX_RECV_BOUND (-65536)

/**
 * @brief Receiver number for a canvas drawn with an origin
 *
 * Returns the canvas id itself when the origin is (0, 0); otherwise binds
 * canvas and origin to the calling app task and returns FMRB_GFX_RECV_BOUND.
 * Returns a negative value other than FMRB_GFX_RECV_BOUND when the calling
 * task is no app task.
 */
int32_t fmrb_gfx_recv_bind(int32_t canvas_id, int32_t ox, int32_t oy);

/**
 * @brief Canvas id and origin of a receiver number
 * @return 0 on success, negative when the number names no target
 */
int fmrb_gfx_recv_resolve(int32_t gfx, uint16_t *canvas, int16_t *ox, int16_t *oy);

#ifdef __cplusplus
}
#endif
