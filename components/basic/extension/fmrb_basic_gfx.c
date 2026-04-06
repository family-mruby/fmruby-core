/**
 * @file fmrb_basic_gfx.c
 * @brief BASIC console window implementation
 *
 * Provides GUI window output for BASIC PRINT statements,
 * with automatic scrolling when text exceeds the visible area.
 */

#include "fmrb_basic_gfx.h"
#include "fmrb_msg.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include <string.h>

static const char *TAG = "basic_gfx";

// Send graphics command to Host Task (same pattern as fmrb_lua_gfx.c)
static fmrb_err_t send_gfx_command(const gfx_cmd_t *cmd) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "Failed to get current task context");
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_GFX,
        .src_pid = ctx->app_id,
        .size = sizeof(gfx_cmd_t)
    };
    memcpy(msg.data, cmd, sizeof(gfx_cmd_t));

    fmrb_err_t ret = FMRB_ERR_TIMEOUT;
    for (int retry = 0; retry < 3; retry++) {
        ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
        if (ret == FMRB_OK) {
            break;
        }
        FMRB_LOGW(TAG, "Failed to send graphics command, retry %d/3", retry + 1);
        fmrb_task_delay_ms(100);
    }
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Graphics command dropped after 3 retries");
    }
    return ret;
}

// Draw window frame (matching FmrbApp draw_window_frame in fmrb-app.rb)
static void draw_window_frame(basic_console_ctx_t* console) {
    uint16_t w = console->window_width;
    uint16_t h = console->window_height;
    fmrb_canvas_handle_t cid = console->canvas_id;
    const char* name = console->app_ctx->app_name;

    // Title bar background
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = cid,
        .params.rect = {
            .rect = {.x = 0, .y = 0, .width = w, .height = BASIC_CONSOLE_TITLEBAR_H},
            .color = 0xC5,
            .filled = true
        }
    };
    send_gfx_command(&cmd);

    // Menu button
    cmd = (gfx_cmd_t){
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = cid,
        .params.rect = {
            .rect = {.x = 2, .y = 2, .width = 8, .height = 8},
            .color = 0x60,
            .filled = true
        }
    };
    send_gfx_command(&cmd);

    // App name in title bar
    cmd = (gfx_cmd_t){
        .cmd_type = GFX_CMD_TEXT,
        .canvas_id = cid,
        .params.text = {
            .x = 12, .y = 2,
            .color = 0xFF,  // WHITE
            .bg_color = 0,
            .bg_transparent = true,
            .font_size = FMRB_FONT_SIZE_SMALL
        }
    };
    strncpy(cmd.params.text.text, name, sizeof(cmd.params.text.text) - 1);
    cmd.params.text.text[sizeof(cmd.params.text.text) - 1] = '\0';
    send_gfx_command(&cmd);

    // Close button (red background)
    int16_t close_x = (int16_t)(w - 10);
    int16_t close_y = 2;
    cmd = (gfx_cmd_t){
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = cid,
        .params.rect = {
            .rect = {.x = close_x, .y = close_y, .width = 8, .height = 8},
            .color = 0xE0,  // RED
            .filled = true
        }
    };
    send_gfx_command(&cmd);

    // Close button X mark (two diagonal lines)
    cmd = (gfx_cmd_t){
        .cmd_type = GFX_CMD_LINE,
        .canvas_id = cid,
        .params.line = {
            .x1 = (int16_t)(close_x + 2), .y1 = (int16_t)(close_y + 2),
            .x2 = (int16_t)(close_x + 5), .y2 = (int16_t)(close_y + 5),
            .color = 0xFF  // WHITE
        }
    };
    send_gfx_command(&cmd);

    cmd = (gfx_cmd_t){
        .cmd_type = GFX_CMD_LINE,
        .canvas_id = cid,
        .params.line = {
            .x1 = (int16_t)(close_x + 5), .y1 = (int16_t)(close_y + 2),
            .x2 = (int16_t)(close_x + 2), .y2 = (int16_t)(close_y + 5),
            .color = 0xFF  // WHITE
        }
    };
    send_gfx_command(&cmd);

    // Window border
    cmd = (gfx_cmd_t){
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = cid,
        .params.rect = {
            .rect = {.x = 0, .y = 0, .width = w, .height = h},
            .color = 0x60,
            .filled = false
        }
    };
    send_gfx_command(&cmd);
}

// Clear user area to white
static void clear_user_area(basic_console_ctx_t* console) {
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = console->canvas_id,
        .params.rect = {
            .rect = {
                .x = console->user_area_x0,
                .y = console->user_area_y0,
                .width = (uint16_t)console->user_area_width,
                .height = (uint16_t)console->user_area_height
            },
            .color = 0xFF,  // WHITE
            .filled = true
        }
    };
    send_gfx_command(&cmd);
}

// Present canvas to screen
static void present(basic_console_ctx_t* console) {
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PRESENT,
        .canvas_id = console->canvas_id,
        .params.present = {
            .x = (int16_t)console->app_ctx->window_pos_x,
            .y = (int16_t)console->app_ctx->window_pos_y,
            .transparent_color = 0xFF  // No transparency
        }
    };
    send_gfx_command(&cmd);
}

// Redraw all visible text lines
static void redraw_text(basic_console_ctx_t* console) {
    clear_user_area(console);

    for (int i = 0; i < console->line_count; i++) {
        int16_t x = (int16_t)(console->user_area_x0 + 2);
        int16_t y = (int16_t)(console->user_area_y0 + 2 + i * BASIC_CONSOLE_CHAR_HEIGHT);

        gfx_cmd_t cmd = {
            .cmd_type = GFX_CMD_TEXT,
            .canvas_id = console->canvas_id,
            .params.text = {
                .x = x, .y = y,
                .color = 0x00,  // BLACK
                .bg_color = 0,
                .bg_transparent = true,
                .font_size = FMRB_FONT_SIZE_SMALL
            }
        };
        strncpy(cmd.params.text.text, console->lines[i],
                sizeof(cmd.params.text.text) - 1);
        cmd.params.text.text[sizeof(cmd.params.text.text) - 1] = '\0';
        send_gfx_command(&cmd);
    }

    present(console);
}

// Add a single line to the console buffer with scroll
static void add_line(basic_console_ctx_t* console, const char* text, int len) {
    if (len > console->max_chars_per_line) {
        len = console->max_chars_per_line;
    }

    if (console->line_count >= console->max_visible_lines) {
        // Scroll: shift lines up by 1
        memmove(console->lines[0], console->lines[1],
                (size_t)(console->max_visible_lines - 1) * BASIC_CONSOLE_MAX_LINE_LEN);
        console->line_count = console->max_visible_lines - 1;
    }

    memcpy(console->lines[console->line_count], text, (size_t)len);
    console->lines[console->line_count][len] = '\0';
    console->line_count++;
}

fmrb_err_t basic_console_init(basic_console_ctx_t* console,
                              fmrb_app_task_context_t* ctx) {
    if (!console || !ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (ctx->headless) {
        FMRB_LOGI(TAG, "Headless app, console output stays in log");
        return FMRB_ERR_INVALID_STATE;
    }

    console->app_ctx = ctx;
    console->window_width = ctx->window_width;
    console->window_height = ctx->window_height;

    // User area geometry (matching FmrbApp layout)
    console->user_area_x0 = 1;
    console->user_area_y0 = (int16_t)(BASIC_CONSOLE_TITLEBAR_H + 1);
    console->user_area_width = (int16_t)(console->window_width - 2);
    console->user_area_height = (int16_t)(console->window_height - BASIC_CONSOLE_TITLEBAR_H - 2);

    console->max_visible_lines = console->user_area_height / BASIC_CONSOLE_CHAR_HEIGHT;
    if (console->max_visible_lines > BASIC_CONSOLE_MAX_LINES) {
        console->max_visible_lines = BASIC_CONSOLE_MAX_LINES;
    }
    console->max_chars_per_line = (console->user_area_width - 4) / BASIC_CONSOLE_CHAR_WIDTH;
    if (console->max_chars_per_line > BASIC_CONSOLE_MAX_LINE_LEN - 1) {
        console->max_chars_per_line = BASIC_CONSOLE_MAX_LINE_LEN - 1;
    }

    console->line_count = 0;

    // Create canvas
    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        FMRB_LOGE(TAG, "Graphics context not initialized");
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_create_canvas(
        gfx_ctx,
        ctx->window_width,
        ctx->window_height,
        ctx->z_order,
        &console->canvas_id
    );
    if (gfx_ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "Failed to create canvas: %d", gfx_ret);
        return FMRB_ERR_NO_MEMORY;
    }

    ctx->canvas_id = console->canvas_id;

    FMRB_LOGI(TAG, "Console canvas %u created (%dx%d), max_lines=%d, max_chars=%d",
              console->canvas_id, ctx->window_width, ctx->window_height,
              console->max_visible_lines, console->max_chars_per_line);

    // Draw window frame and clear user area
    draw_window_frame(console);
    clear_user_area(console);
    present(console);

    return FMRB_OK;
}

void basic_console_output_cb(void* user_data, const char* text) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || !text) {
        return;
    }

    // Also log to console for debugging
    FMRB_LOGI(TAG, "PRINT: %s", text);

    // Process text, splitting by newlines
    const char* p = text;
    while (*p != '\0') {
        // Find end of current line
        const char* line_end = p;
        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }

        int len = (int)(line_end - p);
        if (len > 0) {
            add_line(console, p, len);
        } else if (*line_end == '\n') {
            // Empty line (just a newline)
            add_line(console, "", 0);
        }

        if (*line_end == '\n') {
            line_end++;
        }
        p = line_end;
    }

    redraw_text(console);
}

// GFX ops callback: CLS - clear user area and text buffer
static void gfx_ops_cls(void* user_data) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) return;

    // Clear text buffer
    console->line_count = 0;

    // Clear user area on canvas
    clear_user_area(console);
}

// GFX ops callback: CIRCLE - draw circle on canvas (coords relative to user area)
static void gfx_ops_circle(void* user_data, int16_t x, int16_t y,
                           int16_t r, uint8_t color, bool filled) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) return;

    // Offset to user area
    int16_t abs_x = (int16_t)(console->user_area_x0 + x);
    int16_t abs_y = (int16_t)(console->user_area_y0 + y);

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CIRCLE,
        .canvas_id = console->canvas_id,
        .params.circle = {
            .x = abs_x,
            .y = abs_y,
            .radius = r,
            .color = (fmrb_color_t)color,
            .filled = filled
        }
    };
    send_gfx_command(&cmd);
}

// GFX ops callback: PRESENT - flush canvas to screen
static void gfx_ops_present(void* user_data) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) return;

    present(console);
}

void basic_console_register_gfx_ops(basic_state_t* state,
                                    basic_console_ctx_t* console) {
    if (!state || !console) return;

    state->gfx_ops.cls = gfx_ops_cls;
    state->gfx_ops.circle = gfx_ops_circle;
    state->gfx_ops.present = gfx_ops_present;
    state->gfx_ops.user_data = console;

    FMRB_LOGI(TAG, "Graphics ops registered to BASIC state");
}

void basic_console_destroy(basic_console_ctx_t* console) {
    if (!console) {
        return;
    }

    if (console->canvas_id != FMRB_CANVAS_SCREEN) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (gfx_ctx) {
            fmrb_gfx_delete_canvas(gfx_ctx, console->canvas_id);
        }
    }

    FMRB_LOGI(TAG, "Console destroyed");
}

fmrb_err_t basic_register_gfx_extension(basic_state_t* state) {
    if (!state) {
        return FMRB_ERR_INVALID_PARAM;
    }

    FMRB_LOGI(TAG, "Graphics extension placeholder registered");
    return FMRB_OK;
}
