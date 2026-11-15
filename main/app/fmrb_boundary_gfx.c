/*
 * The app boundary's FmrbGfx flat functions (doc/app_boundary): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from the .rbs files in sig/.
 * Every engine's glue (mruby, Lua, MicroPython, Spinel, WAMR) is generated
 * and calls these; nothing here knows an engine.
 */
#include <stdint.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_boundary_recv.h"
#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"

int32_t fmrb_gfx_recv_bind(int32_t canvas_id, int32_t ox, int32_t oy)
{
    if (ox == 0 && oy == 0) {
        return canvas_id;
    }
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return -1;
    }
    ctx->gfx_bound_canvas = canvas_id;
    ctx->gfx_bound_ox = (int16_t)ox;
    ctx->gfx_bound_oy = (int16_t)oy;
    return FMRB_GFX_RECV_BOUND;
}

int fmrb_gfx_recv_resolve(int32_t gfx, uint16_t *canvas, int16_t *ox, int16_t *oy)
{
    if (gfx == FMRB_GFX_RECV_BOUND) {
        const fmrb_app_task_context_t *ctx = fmrb_current();
        if (!ctx) {
            return -1;
        }
        *canvas = (uint16_t)ctx->gfx_bound_canvas;
        *ox = ctx->gfx_bound_ox;
        *oy = ctx->gfx_bound_oy;
        return 0;
    }
    // Anything else is a canvas id as the canvas handle type takes it (a
    // headless MicroPython app passes -1, as it did before the boundary).
    *canvas = (uint16_t)gfx;
    *ox = 0;
    *oy = 0;
    return 0;
}

int32_t fmrb_gfx_fill_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color)
{
    uint16_t canvas;
    int16_t ox, oy;
    if (fmrb_gfx_recv_resolve(gfx, &canvas, &ox, &oy) < 0) {
        return -1;
    }
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, (fmrb_canvas_handle_t)canvas, (int16_t)(x + ox), (int16_t)(y + oy),
                      (uint16_t)w, (uint16_t)h, (fmrb_color_t)color, true);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}
