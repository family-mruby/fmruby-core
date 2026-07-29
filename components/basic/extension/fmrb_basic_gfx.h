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
/// DEF SPRITE (0-7) plus DEF MOVE (8-15) slots the renderer tracks.
#define BASIC_SPRITE_SLOTS 16

/// Text plane size in pixels: 224x192.
#define BASIC_SCREEN_W (BASIC_SCREEN_COLS * BASIC_SCREEN_CELL_W)
#define BASIC_SCREEN_H (BASIC_SCREEN_ROWS * BASIC_SCREEN_CELL_H)

typedef struct {
    fmrb_app_task_context_t* app_ctx;
    fmrb_canvas_handle_t canvas_id;

    /// Where the canvas is presented in the frame buffer.
    int16_t origin_x;
    int16_t origin_y;

    /// Offset of the 224x192 text plane inside the canvas. A fullscreen .bas
    /// app owns the whole frame and paints the surround black, so its canvas is
    /// bigger than the plane and everything drawn in plane coordinates shifts
    /// by this much. Zero for a windowed app, where the canvas is the plane.
    int16_t pad_x;
    int16_t pad_y;
    /// Canvas size, which is the plane plus the surround.
    uint16_t canvas_w;
    uint16_t canvas_h;

    /// Palette as the interpreter sets it: colour codes 0-60 (core_spec sec 7).
    /// Kept so FILTER can re-tint without another round trip to the core.
    uint8_t backdrop_code;
    uint8_t attr_code[4][3];
    uint8_t sprite_code[4][3];
    /// FILTER colour 0-7 (0 = no tint), applied to the BG plane only.
    uint8_t filter_color;
    /// The same palette after the code table and FILTER, as RGB332.
    /// attr_rgb[group][index] is colour index 1-3 of that attribute group;
    /// index 0 is the shared backdrop.
    uint8_t backdrop_rgb;
    uint8_t attr_rgb[4][3];
    /// Sprite palette, same shape. DEF SPRITE picks a group with its colour
    /// argument, and CGSET selects which bank both planes read.
    uint8_t sprite_rgb[4][3];

    /// A sprite moved, was shown or hidden since the last present. Compositing
    /// happens at present time, so this needs one just like a changed cell.
    bool sprites_moved;
    /// Cells changed since the last present, coalesced into one dirty rect.
    bool dirty;
    uint8_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;

    /// What is currently on the canvas. Cells that do not change are not
    /// redrawn, which is what keeps scrolling a mostly blank screen cheap.
    uint8_t drawn_code[BASIC_SCREEN_COLS * BASIC_SCREEN_ROWS];
    uint8_t drawn_attr[BASIC_SCREEN_COLS * BASIC_SCREEN_ROWS];

    /// Glyph cache: one 128x128 sheet per colour attribute, filled lazily.
    /// A cached cell costs a single draw_tile instead of a dozen rectangles.
    uint16_t sheet_id[4];
    uint8_t sheet_ready[4][32];  // one bit per character code

    /// Text plane character table (CGEN): false = table B (text), true = A.
    bool text_table_a;

    /// Sprite plane: DEF SPRITE slots 0-7 then DEF MOVE slots 8-15.
    bool sprite_plane_on;
    struct {
        uint16_t instance_id;
        /// One image per animation frame. A walking MOVE character alternates
        /// between two poses, so holding both and switching frames costs one
        /// command where repainting the artwork cost one per run of pixels.
        uint16_t image_id[2];
        uint8_t frame_count;
        uint8_t frame_index;
        uint8_t base_tile[2];
        /// Where the instance already is, so an unchanged position is not
        /// resent. Not valid until the first move after the instance is made.
        int16_t x;
        int16_t y;
        bool pos_valid;
        uint8_t attr;
        bool size16;
        bool table_a;
        /// Flips are baked into the artwork, so a change repaints it.
        bool flip_x;
        bool flip_y;
        bool visible;
        /// Set when the sprite palette changed: the artwork holds its colours,
        /// so the image has to be painted again even though the tiles did not
        /// change.
        bool repaint;
    } sprites[BASIC_SPRITE_SLOTS];
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

/// Fill the whole screen with one character (CLS): one rectangle, not 672 cells.
void basic_console_fill(void* user_data, uint8_t code, uint8_t attr);

/// Create, move, show or hide one sprite. Matches the core's sprite callback.
void basic_console_sprite_update(void* user_data, const basic_sprite_view* sprite);

/// SPRITE ON / OFF: show or hide the whole sprite plane.
void basic_console_sprite_plane(void* user_data, bool on);

/// CGEN: switch the text plane between character tables B and A.
void basic_console_set_charset(void* user_data, bool table_a);

/// Apply one palette group (colour codes 0-60, core_spec sec 7).
/**
 * @brief FILTER: tint the whole BG plane (v3_spec).
 * @param color 0 = no tint, 1-7 = the filter colours
 */
void basic_console_set_filter(void* user_data, uint8_t color);

/**
 * @brief Sprite plane palette: three colours for attribute group @p attr.
 *
 * Separate from the BG palette because CGSET selects the banks independently
 * and FILTER only tints the BG plane (core_spec sec 7, v3_spec).
 */
void basic_console_set_sprite_palette(void* user_data, uint8_t attr, uint8_t c1,
                                      uint8_t c2, uint8_t c3);

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
