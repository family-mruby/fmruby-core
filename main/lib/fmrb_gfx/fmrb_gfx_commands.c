#include "fmrb_gfx_commands.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fmrb_gfx_commands";

// Command types
typedef enum {
    FMRB_GFX_CMD_CLEAR,
    FMRB_GFX_CMD_PIXEL,
    FMRB_GFX_CMD_LINE,
    FMRB_GFX_CMD_RECT,
    FMRB_GFX_CMD_CIRCLE,
    FMRB_GFX_CMD_TEXT
} fmrb_gfx_command_type_t;

// Command structures
typedef struct {
    fmrb_canvas_handle_t canvas_id;
    fmrb_color_t color;
} clear_command_t;

typedef struct {
    fmrb_canvas_handle_t canvas_id;
    int16_t x, y;
    fmrb_color_t color;
} pixel_command_t;

typedef struct {
    fmrb_canvas_handle_t canvas_id;
    int16_t x1, y1, x2, y2;
    fmrb_color_t color;
} line_command_t;

typedef struct {
    fmrb_canvas_handle_t canvas_id;
    fmrb_rect_t rect;
    fmrb_color_t color;
    bool filled;
} rect_command_t;

typedef struct {
    fmrb_canvas_handle_t canvas_id;
    int16_t x, y;
    int16_t radius;
    fmrb_color_t color;
    bool filled;
} circle_command_t;

typedef struct {
    fmrb_canvas_handle_t canvas_id;
    int16_t x, y;
    fmrb_color_t color;
    fmrb_font_size_t font_size;
    char text[256]; // Maximum text length
} text_command_t;

// Generic command
typedef struct {
    fmrb_gfx_command_type_t type;
    union {
        clear_command_t clear;
        pixel_command_t pixel;
        line_command_t line;
        rect_command_t rect;
        circle_command_t circle;
        text_command_t text;
    } data;
} fmrb_gfx_command_t;

// Command buffer structure
struct fmrb_gfx_command_buffer {
    fmrb_gfx_command_t *commands;
    size_t max_commands;
    size_t count;
};

fmrb_gfx_command_buffer_t* fmrb_gfx_command_buffer_create(size_t max_commands) {
    if (max_commands == 0) {
        return NULL;
    }

    fmrb_gfx_command_buffer_t *buffer = fmrb_sys_malloc(sizeof(fmrb_gfx_command_buffer_t));
    if (!buffer) {
        return NULL;
    }

    buffer->commands = fmrb_sys_malloc(sizeof(fmrb_gfx_command_t) * max_commands);
    if (!buffer->commands) {
        fmrb_sys_free(buffer);
        return NULL;
    }

    buffer->max_commands = max_commands;
    buffer->count = 0;

    return buffer;
}

void fmrb_gfx_command_buffer_destroy(fmrb_gfx_command_buffer_t* buffer) {
    if (!buffer) {
        return;
    }

    if (buffer->commands) {
        fmrb_sys_free(buffer->commands);
    }

    fmrb_sys_free(buffer);
    ESP_LOGI(TAG, "Command buffer destroyed");
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_clear(fmrb_gfx_command_buffer_t* buffer) {
    if (!buffer) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    buffer->count = 0;
    return FMRB_GFX_OK;
}

static fmrb_gfx_err_t add_command(fmrb_gfx_command_buffer_t* buffer, const fmrb_gfx_command_t* command) {
    if (!buffer || !command) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    if (buffer->count >= buffer->max_commands) {
        ESP_LOGW(TAG, "Command buffer full, dropping command");
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    buffer->commands[buffer->count++] = *command;
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_add_clear(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, fmrb_color_t color) {
    fmrb_gfx_command_t cmd = {
        .type = FMRB_GFX_CMD_CLEAR,
        .data.clear = { .canvas_id = canvas_id, .color = color }
    };

    return add_command(buffer, &cmd);
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_add_pixel(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, fmrb_color_t color) {
    fmrb_gfx_command_t cmd = {
        .type = FMRB_GFX_CMD_PIXEL,
        .data.pixel = { .canvas_id = canvas_id, .x = x, .y = y, .color = color }
    };

    return add_command(buffer, &cmd);
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_add_line(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x1, int16_t y1, int16_t x2, int16_t y2, fmrb_color_t color) {
    fmrb_gfx_command_t cmd = {
        .type = FMRB_GFX_CMD_LINE,
        .data.line = { .canvas_id = canvas_id, .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2, .color = color }
    };

    return add_command(buffer, &cmd);
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_add_rect(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, const fmrb_rect_t* rect, fmrb_color_t color, bool filled) {
    if (!rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_command_t cmd = {
        .type = FMRB_GFX_CMD_RECT,
        .data.rect = { .canvas_id = canvas_id, .rect = *rect, .color = color, .filled = filled }
    };

    return add_command(buffer, &cmd);
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_add_circle(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, int16_t radius, fmrb_color_t color, bool filled) {
    fmrb_gfx_command_t cmd = {
        .type = FMRB_GFX_CMD_CIRCLE,
        .data.circle = { .canvas_id = canvas_id, .x = x, .y = y, .radius = radius, .color = color, .filled = filled }
    };

    return add_command(buffer, &cmd);
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_add_text(fmrb_gfx_command_buffer_t* buffer, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, const char* text, fmrb_color_t color, fmrb_font_size_t font_size) {
    if (!text) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_command_t cmd = {
        .type = FMRB_GFX_CMD_TEXT,
        .data.text = { .canvas_id = canvas_id, .x = x, .y = y, .color = color, .font_size = font_size }
    };

    // Copy text with length limit
    strncpy(cmd.data.text.text, text, sizeof(cmd.data.text.text) - 1);
    cmd.data.text.text[sizeof(cmd.data.text.text) - 1] = '\0';

    return add_command(buffer, &cmd);
}

fmrb_gfx_err_t fmrb_gfx_command_buffer_execute(fmrb_gfx_command_buffer_t* buffer, fmrb_gfx_context_t context) {
    if (!buffer || !context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    ESP_LOGD(TAG, "Executing %zu commands", buffer->count);

    for (size_t i = 0; i < buffer->count; i++) {
        fmrb_gfx_command_t *cmd = &buffer->commands[i];
        fmrb_gfx_err_t ret = FMRB_GFX_OK;

        switch (cmd->type) {
            case FMRB_GFX_CMD_CLEAR:
                ESP_LOGD(TAG, "Executing CLEAR command [%zu]: canvas_id=%d, color=0x%02X",
                         i, cmd->data.clear.canvas_id, cmd->data.clear.color);
                ret = fmrb_gfx_clear(context, cmd->data.clear.canvas_id, cmd->data.clear.color);
                break;

            case FMRB_GFX_CMD_PIXEL:
                ESP_LOGD(TAG, "Executing PIXEL command [%zu]: canvas_id=%d, x=%d, y=%d, color=0x%02X",
                         i, cmd->data.pixel.canvas_id, cmd->data.pixel.x, cmd->data.pixel.y, cmd->data.pixel.color);
                ret = fmrb_gfx_set_pixel(context, cmd->data.pixel.canvas_id, cmd->data.pixel.x, cmd->data.pixel.y, cmd->data.pixel.color);
                break;

            case FMRB_GFX_CMD_LINE:
                ESP_LOGD(TAG, "Executing LINE command [%zu]: canvas_id=%d, x1=%d, y1=%d, x2=%d, y2=%d, color=0x%02X",
                         i, cmd->data.line.canvas_id, cmd->data.line.x1, cmd->data.line.y1,
                         cmd->data.line.x2, cmd->data.line.y2, cmd->data.line.color);
                ret = fmrb_gfx_draw_line(context, cmd->data.line.canvas_id, cmd->data.line.x1, cmd->data.line.y1,
                                       cmd->data.line.x2, cmd->data.line.y2, cmd->data.line.color);
                break;

            case FMRB_GFX_CMD_RECT:
                ESP_LOGD(TAG, "Executing RECT command [%zu]: canvas_id=%d, x=%d, y=%d, w=%d, h=%d, color=0x%02X, filled=%d",
                         i, cmd->data.rect.canvas_id, cmd->data.rect.rect.x, cmd->data.rect.rect.y,
                         cmd->data.rect.rect.width, cmd->data.rect.rect.height, cmd->data.rect.color, cmd->data.rect.filled);
                if (cmd->data.rect.filled) {
                    ret = fmrb_gfx_fill_rect(context, cmd->data.rect.canvas_id, &cmd->data.rect.rect, cmd->data.rect.color);
                } else {
                    ret = fmrb_gfx_draw_rect(context, cmd->data.rect.canvas_id, &cmd->data.rect.rect, cmd->data.rect.color);
                }
                break;

            case FMRB_GFX_CMD_CIRCLE:
                ESP_LOGD(TAG, "Executing CIRCLE command [%zu]: canvas_id=%d, x=%d, y=%d, r=%d, color=0x%02X, filled=%d",
                         i, cmd->data.circle.canvas_id, cmd->data.circle.x, cmd->data.circle.y,
                         cmd->data.circle.radius, cmd->data.circle.color, cmd->data.circle.filled);
                if (cmd->data.circle.filled) {
                    ret = fmrb_gfx_fill_circle(context, cmd->data.circle.canvas_id, cmd->data.circle.x, cmd->data.circle.y,
                                             cmd->data.circle.radius, cmd->data.circle.color);
                } else {
                    ret = fmrb_gfx_draw_circle(context, cmd->data.circle.canvas_id, cmd->data.circle.x, cmd->data.circle.y,
                                             cmd->data.circle.radius, cmd->data.circle.color);
                }
                break;

            case FMRB_GFX_CMD_TEXT:
                ESP_LOGD(TAG, "Executing TEXT command [%zu]: canvas_id=%d, x=%d, y=%d, text='%s', color=0x%02X",
                         i, cmd->data.text.canvas_id, cmd->data.text.x, cmd->data.text.y,
                         cmd->data.text.text, cmd->data.text.color);
                ret = fmrb_gfx_draw_text(context, cmd->data.text.canvas_id, cmd->data.text.x, cmd->data.text.y,
                                       cmd->data.text.text, cmd->data.text.color, cmd->data.text.font_size);
                break;

            default:
                ESP_LOGW(TAG, "Unknown command type: %d", cmd->type);
                ret = FMRB_GFX_ERR_INVALID_PARAM;
                break;
        }

        if (ret != FMRB_GFX_OK) {
            ESP_LOGE(TAG, "Command %zu execution failed: %d", i, ret);
            return ret;
        } else {
            ESP_LOGD(TAG, "Command %zu executed successfully", i);
        }
    }

    return FMRB_GFX_OK;
}

size_t fmrb_gfx_command_buffer_count(const fmrb_gfx_command_buffer_t* buffer) {
    return buffer ? buffer->count : 0;
}