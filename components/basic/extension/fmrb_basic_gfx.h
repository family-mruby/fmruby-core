/**
 * @file fmrb_basic_gfx.h
 * @brief BASIC console window for text output display
 *
 * Provides a GUI window that displays PRINT statement output
 * with automatic scrolling when text exceeds the visible area.
 */

#pragma once

#include "basic_internal.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BASIC_CONSOLE_MAX_LINES    64
#define BASIC_CONSOLE_MAX_LINE_LEN 128

#define BASIC_CONSOLE_CHAR_WIDTH   6
#define BASIC_CONSOLE_CHAR_HEIGHT  8

// Title bar height (matching FmrbApp window frame)
#define BASIC_CONSOLE_TITLEBAR_H   11

typedef struct {
    fmrb_app_task_context_t* app_ctx;
    fmrb_canvas_handle_t canvas_id;

    // Window geometry
    uint16_t window_width;
    uint16_t window_height;

    // User area (inside window frame)
    int16_t user_area_x0;
    int16_t user_area_y0;
    int16_t user_area_width;
    int16_t user_area_height;

    // Text line buffer
    char lines[BASIC_CONSOLE_MAX_LINES][BASIC_CONSOLE_MAX_LINE_LEN];
    int line_count;
    int max_visible_lines;
    int max_chars_per_line;
} basic_console_ctx_t;

/**
 * @brief Initialize BASIC console window
 *
 * Creates a canvas, draws window frame, and prepares for text output.
 *
 * @param console Console context (caller-allocated, zeroed)
 * @param ctx Application task context
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t basic_console_init(basic_console_ctx_t* console,
                              fmrb_app_task_context_t* ctx);

/**
 * @brief Output callback for basic_output()
 *
 * Matches basic_output_cb_t signature. Adds text to the console
 * line buffer and redraws the window with scrolling as needed.
 *
 * @param user_data Pointer to basic_console_ctx_t
 * @param text Text to display
 */
void basic_console_output_cb(void* user_data, const char* text);

/**
 * @brief Destroy console and release resources
 *
 * @param console Console context
 */
void basic_console_destroy(basic_console_ctx_t* console);

/**
 * @brief Register graphics ops callbacks to BASIC state
 *
 * Connects CLS, CIRCLE, PRESENT commands to the console window.
 *
 * @param state BASIC state
 * @param console Console context
 */
void basic_console_register_gfx_ops(basic_state_t* state,
                                    basic_console_ctx_t* console);

/**
 * @brief Register graphics extension commands (placeholder)
 *
 * @param state BASIC state
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t basic_register_gfx_extension(basic_state_t* state);

#ifdef __cplusplus
}
#endif
