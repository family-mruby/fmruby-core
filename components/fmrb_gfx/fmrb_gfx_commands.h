#pragma once

#include "fmrb_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Command buffer management
typedef struct fmrb_gfx_command_buffer fmrb_gfx_command_buffer_t;

/**
 * @brief Create new command buffer
 * @param max_commands Maximum number of commands
 * @return Command buffer handle or NULL on error
 */
fmrb_gfx_command_buffer_t* fmrb_gfx_command_buffer_create(size_t max_commands);

/**
 * @brief Destroy command buffer
 * @param buffer Command buffer handle
 */
void fmrb_gfx_command_buffer_destroy(fmrb_gfx_command_buffer_t* buffer);

/**
 * @brief Clear all commands from buffer
 * @param buffer Command buffer handle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_clear(fmrb_gfx_command_buffer_t* buffer);

/**
 * @brief Add clear command to buffer
 * @param buffer Command buffer handle
 * @param canvas_id Target canvas ID
 * @param color Clear color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_add_clear(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, fmrb_color_t color);

/**
 * @brief Add pixel command to buffer
 * @param buffer Command buffer handle
 * @param canvas_id Target canvas ID
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_add_pixel(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, fmrb_color_t color);

/**
 * @brief Add line command to buffer
 * @param buffer Command buffer handle
 * @param canvas_id Target canvas ID
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color Line color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_add_line(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x1, int16_t y1, int16_t x2, int16_t y2, fmrb_color_t color);

/**
 * @brief Add rectangle command to buffer
 * @param buffer Command buffer handle
 * @param canvas_id Target canvas ID
 * @param rect Rectangle
 * @param color Rectangle color
 * @param filled Whether to fill the rectangle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_add_rect(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, const fmrb_rect_t* rect, fmrb_color_t color, bool filled);

/**
 * @brief Add circle command to buffer
 * @param buffer Command buffer handle
 * @param canvas_id Target canvas ID
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param radius Circle radius
 * @param color Circle color
 * @param filled Whether to fill the circle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_add_circle(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, int16_t radius, fmrb_color_t color, bool filled);

/**
 * @brief Add text command to buffer
 * @param buffer Command buffer handle
 * @param canvas_id Target canvas ID
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text string
 * @param color Text foreground color
 * @param bg_color Text background color
 * @param bg_transparent If true, background is transparent (bg_color is ignored)
 * @param font_size Font size
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_add_text(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, const char* text, fmrb_color_t color, fmrb_color_t bg_color, bool bg_transparent, fmrb_font_size_t font_size);

/**
 * @brief Execute all commands in buffer
 * @param buffer Command buffer handle
 * @param context Graphics context
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_command_buffer_execute(fmrb_gfx_command_buffer_t* buffer, fmrb_gfx_context_t context);

/**
 * @brief Get number of commands in buffer
 * @param buffer Command buffer handle
 * @return Number of commands
 */
size_t fmrb_gfx_command_buffer_count(const fmrb_gfx_command_buffer_t* buffer);

#ifdef __cplusplus
}
#endif
