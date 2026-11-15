/**
 * @file fmrb_gfx_drawer.h
 * @brief The drawers of an app: what an FmrbGfx is below the app boundary
 *
 * Every FmrbGfx of an app, in whatever engine, is a drawer here
 * (doc/app_boundary W4b): the canvas it draws on, its drawing origin and the
 * size of the area that starts there, and whether it draws into a sprite image
 * now (whose own coordinates take no origin). The receiver number of the app
 * boundary's FmrbGfx functions (fmrb_gen/fmrb_boundary.h) is a drawer number,
 * and the flat function adds the origin itself, so no engine's wrapper adds
 * anything: the engine's object keeps only the number (mruby's mrb_gfx_data,
 * Lua's userdata, the Spinel and Python FmrbGfx).
 *
 * The drawers live in the app's context (fmrb_app_task_context_t, in PSRAM on
 * the ESP32 builds) and are numbered 0 .. FMRB_GFX_DRAWERS_MAX - 1 within the
 * app: an engine opens one when it makes an FmrbGfx and closes it when that
 * goes; the ones left are dropped with the app. A number another app opened
 * means nothing here (each app sees its own table).
 *
 * Fixed-width integers only: the MicroPython module includes this while the
 * qstr extractor preprocesses it with no firmware header in sight.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many FmrbGfx an app can have at once (@gfx, window_gfx, the
 *  wallpaper's, two extra canvases, and room for a few of its own). */
#define FMRB_GFX_DRAWERS_MAX 8

typedef struct {
    uint16_t canvas;    /**< the canvas handle it draws on */
    int16_t  ox, oy;    /**< the drawing origin on that canvas */
    uint16_t area_w;    /**< the size of the area from the origin */
    uint16_t area_h;
    uint8_t  used;
    uint8_t  to_image;  /**< drawing into a sprite image: no origin */
} fmrb_gfx_drawer_t;

/**
 * @brief Open a drawer of the calling app on a canvas
 *
 * Its origin is (0, 0) and its area the given size until
 * fmrb_gfx_drawer_set_origin says otherwise.
 * @return the drawer number, or a negative value when the calling task is no
 *         app or all FMRB_GFX_DRAWERS_MAX are open
 */
int32_t fmrb_gfx_drawer_open(int32_t canvas, int32_t area_w, int32_t area_h);

/** @brief Close a drawer of the calling app (a number that is not open is ignored) */
void fmrb_gfx_drawer_close(int32_t drawer);

/** @brief The area a drawer draws in: its top-left on the canvas and its size */
int32_t fmrb_gfx_drawer_set_origin(int32_t drawer, int32_t x, int32_t y, int32_t w, int32_t h);

/** @brief Point a drawer at another canvas (a restored app's canvas, doc/checkpoint F3) */
int32_t fmrb_gfx_drawer_set_canvas(int32_t drawer, int32_t canvas);

/** @brief Whether a drawer draws into a sprite image now (set with the target) */
int32_t fmrb_gfx_drawer_set_to_image(int32_t drawer, int32_t to_image);

/**
 * @brief A drawer of the calling app
 * @return NULL when the number is not an open drawer of the calling app
 */
const fmrb_gfx_drawer_t *fmrb_gfx_drawer_get(int32_t drawer);

#ifdef __cplusplus
}
#endif
