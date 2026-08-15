#include "fmrb_app_canvas.h"

#include "fmrb_log.h"

static const char *TAG = "app_canvas";

// Colour key used for the rounded corners of a window canvas (very dark blue
// in RGB332). Every binding used this same value.
#define APP_CANVAS_TRANSPARENT_KEY 0x01

static fmrb_err_t create_canvas(fmrb_app_task_context_t *ctx, uint16_t width,
                                uint16_t height, int16_t z_order,
                                bool use_transparent, uint8_t transparent_color,
                                fmrb_canvas_handle_t *out_canvas)
{
    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        FMRB_LOGE(TAG, "Graphics context not initialized");
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_gfx_err_t ret = fmrb_gfx_create_canvas(gfx_ctx, width, height, z_order,
                                                use_transparent,
                                                transparent_color, &canvas_id);
    if (ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "[%s] Failed to create canvas: %d", ctx->app_name, ret);
        return FMRB_ERR_FAILED;
    }
    *out_canvas = canvas_id;
    return FMRB_OK;
}

// Delete one canvas and report whether it went. Callers zero their field
// either way: a canvas that cannot be deleted is not going to be deletable
// later either, and keeping the id would only invite a second attempt.
static void delete_canvas(fmrb_app_task_context_t *ctx, const char *what,
                          fmrb_canvas_handle_t canvas_id)
{
    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        return;
    }
    fmrb_gfx_err_t ret = fmrb_gfx_delete_canvas(gfx_ctx, canvas_id);
    if (ret == FMRB_GFX_OK) {
        FMRB_LOGI(TAG, "[%s] Deleted %s canvas %u", ctx->app_name, what,
                  canvas_id);
    } else {
        FMRB_LOGW(TAG, "[%s] Failed to delete %s canvas %u: %d", ctx->app_name,
                  what, canvas_id, ret);
    }
}

fmrb_err_t fmrb_app_canvas_create_main(fmrb_app_task_context_t *ctx,
                                       uint16_t width, uint16_t height,
                                       int16_t z_order, bool use_transparent,
                                       uint8_t transparent_color,
                                       fmrb_canvas_handle_t *out_canvas)
{
    if (!ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_err_t ret = create_canvas(ctx, width, height, z_order, use_transparent,
                                   transparent_color, &canvas_id);
    if (ret != FMRB_OK) {
        return ret;
    }

    ctx->canvas_id = canvas_id;
    if (out_canvas) {
        *out_canvas = canvas_id;
    }
    FMRB_LOGI(TAG, "[%s] Created canvas %u (%ux%u)", ctx->app_name, canvas_id,
              width, height);
    // Every runtime -- mruby, Spinel, MicroPython, Lua, BASIC -- reaches here
    // only once its script is loaded and compiled, so this is where the app
    // counts as started. Headless and background apps never arrive, which is
    // exactly why they get no "starting" indicator.
    fmrb_app_notify_started(ctx);
    return FMRB_OK;
}

fmrb_err_t fmrb_app_canvas_init(fmrb_app_task_context_t *ctx,
                                fmrb_canvas_handle_t *out_canvas,
                                fmrb_canvas_handle_t *out_bg_canvas)
{
    if (!ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (out_canvas) {
        *out_canvas = FMRB_CANVAS_SCREEN;
    }
    if (out_bg_canvas) {
        *out_bg_canvas = FMRB_CANVAS_SCREEN;
    }

    if (ctx->headless) {
        FMRB_LOGI(TAG, "[%s] Headless: no canvas allocated", ctx->app_name);
        return FMRB_OK;
    }

    // Colour-key transparency only for a non-fullscreen window that kept its
    // rounded corners; opting out skips the per-pixel compare in the
    // compositor.
    fmrb_err_t ret = fmrb_app_canvas_create_main(
        ctx, ctx->window_width, ctx->window_height, ctx->z_order,
        !ctx->fullscreen && ctx->rounded_corners, APP_CANVAS_TRANSPARENT_KEY,
        out_canvas);
    if (ret != FMRB_OK) {
        return ret;
    }

    if (!ctx->has_background_canvas) {
        return FMRB_OK;
    }

    // Wallpaper layer, below everything the app draws. Losing it is not worth
    // failing the app for.
    fmrb_canvas_handle_t bg_id = FMRB_CANVAS_SCREEN;
    if (create_canvas(ctx, ctx->window_width, ctx->window_height, 0, false, 0,
                      &bg_id) != FMRB_OK) {
        return FMRB_OK;
    }
    ctx->bg_canvas_id = bg_id;
    if (out_bg_canvas) {
        *out_bg_canvas = bg_id;
    }
    FMRB_LOGI(TAG, "[%s] Created background canvas %u", ctx->app_name, bg_id);
    return FMRB_OK;
}

fmrb_err_t fmrb_app_canvas_create_extra(fmrb_app_task_context_t *ctx,
                                        uint16_t width, uint16_t height,
                                        int16_t z_offset, bool use_transparent,
                                        uint8_t transparent_color,
                                        fmrb_canvas_handle_t *out_canvas)
{
    if (!ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }

    int slot = -1;
    for (int i = 0; i < FMRB_APP_MAX_EXTRA_CANVAS; i++) {
        if (ctx->extra_canvas_ids[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        FMRB_LOGE(TAG, "[%s] Extra canvas limit reached (%d)", ctx->app_name,
                  FMRB_APP_MAX_EXTRA_CANVAS);
        return FMRB_ERR_NO_RESOURCE;
    }

    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_err_t ret = create_canvas(ctx, width, height,
                                   (int16_t)(ctx->z_order + z_offset),
                                   use_transparent, transparent_color,
                                   &canvas_id);
    if (ret != FMRB_OK) {
        return ret;
    }

    ctx->extra_canvas_ids[slot] = canvas_id;
    if (out_canvas) {
        *out_canvas = canvas_id;
    }
    FMRB_LOGI(TAG, "[%s] Created extra canvas %u (%ux%u)", ctx->app_name,
              canvas_id, width, height);
    return FMRB_OK;
}

fmrb_err_t fmrb_app_canvas_delete_extra(fmrb_app_task_context_t *ctx,
                                        fmrb_canvas_handle_t canvas_id)
{
    if (!ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }

    for (int i = 0; i < FMRB_APP_MAX_EXTRA_CANVAS; i++) {
        if (ctx->extra_canvas_ids[i] != canvas_id) {
            continue;
        }
        ctx->extra_canvas_ids[i] = 0;
        delete_canvas(ctx, "extra", canvas_id);
        return FMRB_OK;
    }
    return FMRB_ERR_NOT_FOUND;
}

void fmrb_app_canvas_release_all(fmrb_app_task_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    // Each field is cleared before its canvas is deleted, so a second call -
    // or a call racing the first - finds nothing left to do.
    if (ctx->canvas_id != FMRB_CANVAS_SCREEN) {
        fmrb_canvas_handle_t canvas_id = ctx->canvas_id;
        ctx->canvas_id = FMRB_CANVAS_SCREEN;
        delete_canvas(ctx, "window", canvas_id);
    }

    if (ctx->bg_canvas_id != FMRB_CANVAS_SCREEN) {
        fmrb_canvas_handle_t bg_id = ctx->bg_canvas_id;
        ctx->bg_canvas_id = FMRB_CANVAS_SCREEN;
        delete_canvas(ctx, "background", bg_id);
    }

    for (int i = 0; i < FMRB_APP_MAX_EXTRA_CANVAS; i++) {
        if (ctx->extra_canvas_ids[i] == 0) {
            continue;
        }
        fmrb_canvas_handle_t extra_id = ctx->extra_canvas_ids[i];
        ctx->extra_canvas_ids[i] = 0;
        delete_canvas(ctx, "extra", extra_id);
    }
}
