/**
 * @file fmrb_basic_gfx.h
 * @brief BASIC text screen renderer (28x24 characters of 8x8 pixels)
 *
 * The interpreter core owns the 28x24 shadow buffer; this module mirrors it
 * onto an fmrb_gfx canvas one cell at a time and presents the result once per
 * update batch. The fmruby specific graphics statements (CIRCLE, PRESENT)
 * draw onto the same canvas.
 */

#pragma once

#include "fmrb_basic.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Character cell size in pixels (core_spec sec 13).
#define BASIC_SCREEN_CELL_W 8
#define BASIC_SCREEN_CELL_H 8
/// Text plane size in characters.
#define BASIC_SCREEN_COLS 28
#define BASIC_SCREEN_ROWS 24
/// Text plane size in pixels: 224x192.
#define BASIC_SCREEN_W (BASIC_SCREEN_COLS * BASIC_SCREEN_CELL_W)
#define BASIC_SCREEN_H (BASIC_SCREEN_ROWS * BASIC_SCREEN_CELL_H)

typedef struct {
    fmrb_app_task_context_t* app_ctx;
    fmrb_canvas_handle_t canvas_id;

    /// Where the text plane is presented (centred on a 320x240 frame buffer).
    int16_t origin_x;
    int16_t origin_y;

    /// Palette: backdrop plus three colours per attribute group, as RGB332.
    uint8_t backdrop_rgb;
    uint8_t attr_rgb[4];

    /// Cells changed since the last present, coalesced into one dirty rect.
    bool dirty;
    uint8_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;
} basic_console_ctx_t;

/**
 * @brief Create the text screen canvas and clear it
 *
 * @param console Renderer context (caller-allocated, zeroed)
 * @param ctx Application task context
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t basic_console_init(basic_console_ctx_t* console,
                              fmrb_app_task_context_t* ctx);

/// Draw one character cell. Matches the core's screen cell callback.
void basic_console_draw_cell(void* user_data, uint8_t x, uint8_t y, uint8_t code,
                             uint8_t attr);

/// Present the canvas if anything changed since the last call.
void basic_console_present(void* user_data);

/// Apply one palette group (colour codes 0-60, core_spec sec 7).
void basic_console_set_palette(void* user_data, uint8_t attr, uint8_t backdrop,
                               uint8_t c1, uint8_t c2, uint8_t c3);

/**
 * @brief Destroy the renderer and release its canvas
 *
 * @param console Renderer context
 */
void basic_console_destroy(basic_console_ctx_t* console);

/**
 * @brief Register the fmruby graphics statements (CIRCLE, PRESENT)
 *
 * @param state BASIC state
 * @param console Renderer context
 */
void basic_console_register_gfx_ops(basic_state_t* state,
                                    basic_console_ctx_t* console);

#ifdef __cplusplus
}
#endif
