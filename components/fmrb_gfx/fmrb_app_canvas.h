/**
 * @file fmrb_app_canvas.h
 * @brief Canvas ownership for an app task.
 *
 * An app owns up to three kinds of canvas: the window canvas, an optional
 * background layer (the desktop's wallpaper), and a handful of extra canvases
 * it can ask for at runtime. Every one of them has to be registered on the app
 * context so the kernel can reclaim it, and released exactly once no matter
 * how the app ends - normal exit, uncaught exception, or a forced kill.
 *
 * Each language binding used to open-code that, which is how the bindings
 * drifted apart (one forgot the background canvas, another left the context
 * field set after deleting, so the kernel deleted the same id twice). The
 * bindings now call these and never touch fmrb_gfx_create_canvas /
 * fmrb_gfx_delete_canvas or ctx->extra_canvas_ids themselves.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the canvases described by the app context.
 *
 * The window canvas takes its size from ctx->window_width/height and its
 * z-order from ctx->z_order, with colour-key transparency (key 0x01) only for
 * a non-fullscreen window that kept rounded corners. When
 * ctx->has_background_canvas is set, a second canvas of the same size is
 * created at z=0 for the wallpaper. Both are registered on the context.
 *
 * A headless app gets nothing and returns FMRB_OK with both outputs left at
 * FMRB_CANVAS_SCREEN, so callers do not need their own headless branch.
 *
 * A background canvas that fails to create is logged and skipped rather than
 * failing the call: the app is still usable without its wallpaper.
 *
 * @param ctx App context to create for and register on.
 * @param out_canvas Receives the window canvas id. May be NULL.
 * @param out_bg_canvas Receives the background canvas id, or
 *        FMRB_CANVAS_SCREEN when there is none. May be NULL.
 * @return FMRB_OK, or FMRB_ERR_INVALID_STATE when there is no graphics
 *         context, or FMRB_ERR_FAILED when the window canvas cannot be made.
 */
fmrb_err_t fmrb_app_canvas_init(fmrb_app_task_context_t *ctx,
                                fmrb_canvas_handle_t *out_canvas,
                                fmrb_canvas_handle_t *out_bg_canvas);

/**
 * @brief Create the window canvas with explicit geometry.
 *
 * For bindings whose script decides the size instead of the TOML window
 * (Lua's FmrbApp.create_canvas, the BASIC console). Registers the result as
 * ctx->canvas_id exactly as fmrb_app_canvas_init() would.
 *
 * @return FMRB_OK, or FMRB_ERR_INVALID_STATE / FMRB_ERR_FAILED as above.
 */
fmrb_err_t fmrb_app_canvas_create_main(fmrb_app_task_context_t *ctx,
                                       uint16_t width, uint16_t height,
                                       int16_t z_order, bool use_transparent,
                                       uint8_t transparent_color,
                                       fmrb_canvas_handle_t *out_canvas);

/**
 * @brief Create an extra canvas and take a slot for it on the context.
 *
 * @param z_offset Added to ctx->z_order, so a canvas stays with its window.
 * @return FMRB_OK, FMRB_ERR_NO_RESOURCE when all
 *         FMRB_APP_MAX_EXTRA_CANVAS slots are taken, or as above.
 */
fmrb_err_t fmrb_app_canvas_create_extra(fmrb_app_task_context_t *ctx,
                                        uint16_t width, uint16_t height,
                                        int16_t z_offset, bool use_transparent,
                                        uint8_t transparent_color,
                                        fmrb_canvas_handle_t *out_canvas);

/**
 * @brief Delete one extra canvas and free its slot.
 * @return FMRB_OK, or FMRB_ERR_NOT_FOUND when the id is not an extra canvas
 *         of this app (which is what makes it safe to call with any id).
 */
fmrb_err_t fmrb_app_canvas_delete_extra(fmrb_app_task_context_t *ctx,
                                        fmrb_canvas_handle_t canvas_id);

/**
 * @brief Release every canvas the app owns and clear the context fields.
 *
 * Idempotent: the fields are zeroed as they are deleted, so the binding's
 * cleanup, the kernel's normal reap and the forced-kill path can all call
 * this and only the first one does any work. That is the point - an id
 * deleted twice can hit a canvas the graphics side has already handed to
 * another app.
 */
void fmrb_app_canvas_release_all(fmrb_app_task_context_t *ctx);

#ifdef __cplusplus
}
#endif
