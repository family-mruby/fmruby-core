#include "fmrb_gfx.h"
#include "fmrb_hal.h"
#include "fmrb_ipc_protocol.h"
#include "fmrb_ipc_transport.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fmrb_gfx";

typedef struct {
    fmrb_gfx_config_t config;
    fmrb_ipc_transport_handle_t transport;
    fmrb_rect_t clip_rect;
    bool clip_enabled;
    bool initialized;
} fmrb_gfx_context_impl_t;

// Helper function to check if point is within clip rectangle
static bool is_clipped(fmrb_gfx_context_impl_t *ctx, int16_t x, int16_t y) {
    if (!ctx->clip_enabled) {
        return false;
    }

    return (x < ctx->clip_rect.x ||
            y < ctx->clip_rect.y ||
            x >= ctx->clip_rect.x + ctx->clip_rect.width ||
            y >= ctx->clip_rect.y + ctx->clip_rect.height);
}

// Helper function to send graphics command
static fmrb_gfx_err_t send_graphics_command(fmrb_gfx_context_impl_t *ctx, uint8_t cmd_type, const void *cmd_data, size_t cmd_size) {
    if (!ctx || !ctx->transport) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_transport_err_t ret = fmrb_ipc_transport_send(ctx->transport, cmd_type, (const uint8_t*)cmd_data, cmd_size);

    switch (ret) {
        case FMRB_IPC_TRANSPORT_OK:
            return FMRB_GFX_OK;
        case FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM:
            return FMRB_GFX_ERR_INVALID_PARAM;
        case FMRB_IPC_TRANSPORT_ERR_NO_MEMORY:
            return FMRB_GFX_ERR_NO_MEMORY;
        default:
            return FMRB_GFX_ERR_FAILED;
    }
}

fmrb_gfx_err_t fmrb_gfx_init(const fmrb_gfx_config_t *config, fmrb_gfx_context_t *context) {
    if (!config || !context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = malloc(sizeof(fmrb_gfx_context_impl_t));
    if (!ctx) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    memset(ctx, 0, sizeof(fmrb_gfx_context_impl_t));
    ctx->config = *config;

    // Initialize IPC transport for graphics
    fmrb_ipc_transport_config_t transport_config = {
        .timeout_ms = 1000,
        .enable_retransmit = true,
        .max_retries = 3,
        .window_size = 8
    };

    fmrb_ipc_transport_err_t ret = fmrb_ipc_transport_init(&transport_config, &ctx->transport);
    if (ret != FMRB_IPC_TRANSPORT_OK) {
        free(ctx);
        return FMRB_GFX_ERR_FAILED;
    }

    ctx->initialized = true;
    *context = ctx;

    ESP_LOGI(TAG, "Graphics initialized: %dx%d, %d bpp",
             config->screen_width, config->screen_height, config->bits_per_pixel);

    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_deinit(fmrb_gfx_context_t context) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;

    if (ctx->transport) {
        fmrb_ipc_transport_deinit(ctx->transport);
    }

    ctx->initialized = false;
    free(ctx);

    ESP_LOGI(TAG, "Graphics deinitialized");
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_clear(fmrb_gfx_context_t context, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_graphics_clear_t clear_cmd = {
        .x = 0,
        .y = 0,
        .width = ctx->config.screen_width,
        .height = ctx->config.screen_height,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_CLEAR, &clear_cmd, sizeof(clear_cmd));
}

fmrb_gfx_err_t fmrb_gfx_clear_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_graphics_clear_t clear_cmd = {
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_CLEAR, &clear_cmd, sizeof(clear_cmd));
}

fmrb_gfx_err_t fmrb_gfx_set_pixel(fmrb_gfx_context_t context, int16_t x, int16_t y, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    if (is_clipped(ctx, x, y)) {
        return FMRB_GFX_OK; // Silently ignore clipped pixels
    }

    fmrb_ipc_graphics_pixel_t pixel_cmd = {
        .x = x,
        .y = y,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_SET_PIXEL, &pixel_cmd, sizeof(pixel_cmd));
}

fmrb_gfx_err_t fmrb_gfx_get_pixel(fmrb_gfx_context_t context, int16_t x, int16_t y, fmrb_color_t *color) {
    // For now, return black color (read operation would require synchronous IPC)
    if (!context || !color) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    *color = FMRB_COLOR_BLACK;
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_draw_line(fmrb_gfx_context_t context, int16_t x1, int16_t y1, int16_t x2, int16_t y2, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_graphics_line_t line_cmd = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_DRAW_LINE, &line_cmd, sizeof(line_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_graphics_rect_t rect_cmd = {
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color,
        .filled = false
    };

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_DRAW_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_graphics_rect_t rect_cmd = {
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_DRAW_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_text(fmrb_gfx_context_t context, int16_t x, int16_t y, const char *text, fmrb_color_t color, fmrb_font_size_t font_size) {
    if (!context || !text) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    size_t text_len = strlen(text);
    if (text_len > 255) {
        text_len = 255; // Limit text length
    }

    // Allocate buffer for command + text
    size_t total_size = sizeof(fmrb_ipc_graphics_text_t) + text_len;
    uint8_t *cmd_buffer = malloc(total_size);
    if (!cmd_buffer) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    fmrb_ipc_graphics_text_t *text_cmd = (fmrb_ipc_graphics_text_t*)cmd_buffer;
    text_cmd->x = x;
    text_cmd->y = y;
    text_cmd->color = color;
    text_cmd->font_size = font_size;
    text_cmd->text_len = text_len;

    // Copy text data
    memcpy(cmd_buffer + sizeof(fmrb_ipc_graphics_text_t), text, text_len);

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_DRAW_TEXT, cmd_buffer, total_size);
    free(cmd_buffer);

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_get_text_size(const char *text, fmrb_font_size_t font_size, uint16_t *width, uint16_t *height) {
    if (!text || !width || !height) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    size_t text_len = strlen(text);

    // Simple font metrics estimation
    switch (font_size) {
        case FMRB_FONT_SIZE_SMALL:
            *width = text_len * 6;
            *height = 8;
            break;
        case FMRB_FONT_SIZE_MEDIUM:
            *width = text_len * 8;
            *height = 12;
            break;
        case FMRB_FONT_SIZE_LARGE:
            *width = text_len * 10;
            *height = 16;
            break;
        case FMRB_FONT_SIZE_XLARGE:
            *width = text_len * 12;
            *height = 20;
            break;
        default:
            return FMRB_GFX_ERR_INVALID_PARAM;
    }

    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_present(fmrb_gfx_context_t context) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    return send_graphics_command(ctx, FMRB_IPC_MSG_GRAPHICS_PRESENT, NULL, 0);
}

fmrb_gfx_err_t fmrb_gfx_set_clip_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    if (rect) {
        ctx->clip_rect = *rect;
        ctx->clip_enabled = true;
    } else {
        ctx->clip_enabled = false;
    }

    return FMRB_GFX_OK;
}