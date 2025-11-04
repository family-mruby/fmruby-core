#include "fmrb_gfx.h"
#include "fmrb_hal.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_transport.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fmrb_gfx";

// Global graphics context (shared across all FmrbGfx instances)
static fmrb_gfx_context_impl_t *g_gfx_context = NULL;
static fmrb_gfx_context_impl_t g_gfx_context_body;

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

    fmrb_link_transport_err_t ret = fmrb_link_transport_send(ctx->transport, cmd_type, (const uint8_t*)cmd_data, cmd_size);

    switch (ret) {
        case FMRB_LINK_TRANSPORT_OK:
            return FMRB_GFX_OK;
        case FMRB_LINK_TRANSPORT_ERR_INVALID_PARAM:
            return FMRB_GFX_ERR_INVALID_PARAM;
        case FMRB_LINK_TRANSPORT_ERR_NO_MEMORY:
            return FMRB_GFX_ERR_NO_MEMORY;
        default:
            return FMRB_GFX_ERR_FAILED;
    }
}

fmrb_gfx_err_t fmrb_gfx_init(const fmrb_gfx_config_t *config) {
    if (!config) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    // If global context already exists, prevent double initialization
    if (g_gfx_context != NULL) {
        ESP_LOGW(TAG, "Graphics context already initialized, reusing existing context");
        return FMRB_GFX_OK;
    }

    //fmrb_gfx_context_impl_t *ctx = fmrb_sys_malloc(sizeof(fmrb_gfx_context_impl_t));
    fmrb_gfx_context_impl_t *ctx = &g_gfx_context_body;
    // if (!ctx) {
    //     return FMRB_GFX_ERR_NO_MEMORY;
    // }

    memset(ctx, 0, sizeof(fmrb_gfx_context_impl_t));
    ctx->config = *config;

    // Initialize IPC transport for graphics
    fmrb_link_transport_config_t transport_config = {
        .timeout_ms = 1000,
        .enable_retransmit = true,
        .max_retries = 3,
        .window_size = 8
    };

    fmrb_link_transport_err_t ret = fmrb_link_transport_init(&transport_config, &ctx->transport);
    if (ret != FMRB_LINK_TRANSPORT_OK) {
        //fmrb_sys_free(ctx);
        return FMRB_GFX_ERR_FAILED;
    }

    ctx->initialized = true;
    ctx->current_target = FMRB_CANVAS_SCREEN;  // Default to main screen
    ctx->next_canvas_id = 1;  // Start canvas IDs from 1

    // Store as global context
    g_gfx_context = ctx;

    ESP_LOGI(TAG, "Graphics initialized: %dx%d, %d bpp", config->screen_width, config->screen_height, config->bits_per_pixel);
    ESP_LOGI(TAG, "init: g_gfx_context=%p, initialized=%d", g_gfx_context, ctx->initialized);

    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_deinit(void) {
    // if (!context) {
    //     return FMRB_GFX_ERR_INVALID_PARAM;
    // }

    // fmrb_gfx_context_impl_t *ctx = context;

    // Only deinitialize if this is the global context
    if (g_gfx_context != NULL) {
        if (g_gfx_context->transport) {
            fmrb_link_transport_deinit(g_gfx_context->transport);
        }

        g_gfx_context->initialized = false;
        // fmrb_sys_free(ctx);
        g_gfx_context = NULL;

        ESP_LOGI(TAG, "Graphics deinitialized");
    } else {
        ESP_LOGW(TAG, "Attempted to deinit NULL global context, ignoring");
    }

    return FMRB_GFX_OK;
}

fmrb_gfx_context_t fmrb_gfx_get_global_context(void) {
    ESP_LOGI(TAG, "get_global_context: g_gfx_context=%p, initialized=%d",
             g_gfx_context, g_gfx_context ? g_gfx_context->initialized : -1);
    if (g_gfx_context) {
        ESP_LOGI(TAG, "get_global_context: returning ctx=%p with fields: config.screen_width=%d, transport=%p",
                 g_gfx_context, g_gfx_context->config.screen_width, g_gfx_context->transport);
    }
    return g_gfx_context;
}

fmrb_gfx_err_t fmrb_gfx_clear(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, fmrb_color_t color) {
    ESP_LOGI(TAG, "clear: called with context=%p, canvas_id=%d, color=0x%02x", context, canvas_id, color);
    ESP_LOGI(TAG, "clear: g_gfx_context=%p (for reference)", g_gfx_context);

    if (!context) {
        ESP_LOGE(TAG, "clear: context is NULL");
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    ESP_LOGI(TAG, "clear: ctx=%p, canvas_id=%d, initialized=%d, transport=%p, screen_width=%d",
             ctx, canvas_id, ctx->initialized, ctx->transport, ctx->config.screen_width);

    if (!ctx->initialized) {
        ESP_LOGE(TAG, "clear: context not initialized (ctx=%p, initialized=%d)", ctx, ctx->initialized);
        ESP_LOGE(TAG, "clear: context == g_gfx_context? %s", (context == g_gfx_context) ? "YES" : "NO");
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Send cmd_type + canvas_id + color for fill_screen/clear command
    struct __attribute__((packed)) {
        uint8_t cmd_type;
        uint16_t canvas_id;
        uint8_t color;
    } clear_cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_SCREEN,
        .canvas_id = canvas_id,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_SCREEN, &clear_cmd, sizeof(clear_cmd));
}

fmrb_gfx_err_t fmrb_gfx_clear_rect(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_clear_t clear_cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_SCREEN,
        .canvas_id = canvas_id,
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_SCREEN, &clear_cmd, sizeof(clear_cmd));
}

fmrb_gfx_err_t fmrb_gfx_set_pixel(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    if (is_clipped(ctx, x, y)) {
        return FMRB_GFX_OK; // Silently ignore clipped pixels
    }

    fmrb_link_graphics_pixel_t pixel_cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_PIXEL,
        .canvas_id = canvas_id,
        .x = x,
        .y = y,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_PIXEL, &pixel_cmd, sizeof(pixel_cmd));
}

fmrb_gfx_err_t fmrb_gfx_get_pixel(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, fmrb_color_t *color) {
    // For now, return black color (read operation would require synchronous IPC)
    if (!context || !color) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    *color = FMRB_COLOR_BLACK;
    return FMRB_GFX_OK;
}

fmrb_gfx_err_t fmrb_gfx_draw_line(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int16_t x1, int16_t y1, int16_t x2, int16_t y2, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_line_t line_cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_LINE,
        .canvas_id = canvas_id,
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_LINE, &line_cmd, sizeof(line_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_rect(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_rect_t rect_cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_RECT,
        .canvas_id = canvas_id,
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color,
        .filled = false
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_rect(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_rect_t rect_cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_RECT,
        .canvas_id = canvas_id,
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_text(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int16_t x, int16_t y, const char *text, fmrb_color_t color, fmrb_font_size_t font_size) {
    if (!context || !text) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    size_t text_len = strlen(text);
    if (text_len > 255) {
        text_len = 255; // Limit text length
    }

    // Allocate buffer for command + text
    size_t total_size = sizeof(fmrb_link_graphics_text_t) + text_len;
    uint8_t *cmd_buffer = fmrb_sys_malloc(total_size);
    if (!cmd_buffer) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    fmrb_link_graphics_text_t *text_cmd = (fmrb_link_graphics_text_t*)cmd_buffer;
    text_cmd->cmd_type = FMRB_LINK_GFX_DRAW_STRING;
    text_cmd->x = x;
    text_cmd->y = y;
    text_cmd->color = color;
    text_cmd->text_len = text_len;
    // Note: font_size is currently ignored by SDL2 host, may be added in the future

    // Copy text data
    memcpy(cmd_buffer + sizeof(fmrb_link_graphics_text_t), text, text_len);

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_STRING, cmd_buffer, total_size);
    fmrb_sys_free(cmd_buffer);

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

fmrb_gfx_err_t fmrb_gfx_present(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Use legacy present command (0x08) since it's simple and works
    return send_graphics_command(ctx, 0x08, NULL, 0);
}

fmrb_gfx_err_t fmrb_gfx_set_clip_rect(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, const fmrb_rect_t *rect) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
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

// LovyanGFX compatible API implementations

fmrb_gfx_err_t fmrb_gfx_draw_pixel(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, fmrb_color_t color) {
    return fmrb_gfx_set_pixel(context, canvas_id, (int16_t)x, (int16_t)y, color);
}

fmrb_gfx_err_t fmrb_gfx_draw_fast_vline(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t h, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Draw vertical line as a thin rectangle
    fmrb_link_graphics_rect_t rect_cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_RECT,
        .canvas_id = canvas_id,
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .width = 1,
        .height = (uint16_t)h,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_fast_hline(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t w, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Draw horizontal line as a thin rectangle
    fmrb_link_graphics_rect_t rect_cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_RECT,
        .canvas_id = canvas_id,
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .width = (uint16_t)w,
        .height = 1,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_round_rect(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Payload: cmd_type, x, y, w, h, r, color, filled
    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, w, h, r;
        uint8_t color;
        uint8_t filled;
    } round_rect_cmd_t;

    round_rect_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_ROUND_RECT,
        .x = x, .y = y, .w = w, .h = h, .r = r,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_ROUND_RECT, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_round_rect(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, w, h, r;
        uint8_t color;
        uint8_t filled;
    } round_rect_cmd_t;

    round_rect_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_ROUND_RECT,
        .x = x, .y = y, .w = w, .h = h, .r = r,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_ROUND_RECT, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_circle(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, r;
        uint8_t color;
        uint8_t filled;
    } circle_cmd_t;

    circle_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_CIRCLE,
        .x = x, .y = y, .r = r,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_CIRCLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_circle(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, r;
        uint8_t color;
        uint8_t filled;
    } circle_cmd_t;

    circle_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_CIRCLE,
        .x = x, .y = y, .r = r,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_CIRCLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_ellipse(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t rx, int32_t ry, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, rx, ry;
        uint8_t color;
        uint8_t filled;
    } ellipse_cmd_t;

    ellipse_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_ELLIPSE,
        .x = x, .y = y, .rx = rx, .ry = ry,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_ELLIPSE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_ellipse(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t rx, int32_t ry, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, rx, ry;
        uint8_t color;
        uint8_t filled;
    } ellipse_cmd_t;

    ellipse_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_ELLIPSE,
        .x = x, .y = y, .rx = rx, .ry = ry,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_ELLIPSE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_triangle(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x0, y0, x1, y1, x2, y2;
        uint8_t color;
        uint8_t filled;
    } triangle_cmd_t;

    triangle_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_TRIANGLE,
        .x0 = x0, .y0 = y0,
        .x1 = x1, .y1 = y1,
        .x2 = x2, .y2 = y2,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_TRIANGLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_triangle(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x0, y0, x1, y1, x2, y2;
        uint8_t color;
        uint8_t filled;
    } triangle_cmd_t;

    triangle_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_TRIANGLE,
        .x0 = x0, .y0 = y0,
        .x1 = x1, .y1 = y1,
        .x2 = x2, .y2 = y2,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_TRIANGLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_arc(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, r0, r1;
        float angle0, angle1;
        uint8_t color;
        uint8_t filled;
    } arc_cmd_t;

    arc_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_DRAW_ARC,
        .x = x, .y = y, .r0 = r0, .r1 = r1,
        .angle0 = angle0, .angle1 = angle1,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_ARC, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_arc(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        int32_t x, y, r0, r1;
        float angle0, angle1;
        uint8_t color;
        uint8_t filled;
    } arc_cmd_t;

    arc_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_FILL_ARC,
        .x = x, .y = y, .r0 = r0, .r1 = r1,
        .angle0 = angle0, .angle1 = angle1,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_ARC, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_string(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, const char *str, int32_t x, int32_t y, fmrb_color_t color) {
    if (!context || !str) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    size_t str_len = strlen(str);
    if (str_len > 255) {
        str_len = 255;
    }

    // Allocate buffer for command + string
    size_t total_size = sizeof(uint8_t) + sizeof(int32_t) * 2 + sizeof(uint8_t) + sizeof(uint16_t) + str_len;
    uint8_t *cmd_buffer = fmrb_sys_malloc(total_size);
    if (!cmd_buffer) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    size_t offset = 0;
    uint8_t cmd_type = FMRB_LINK_GFX_DRAW_STRING;
    memcpy(cmd_buffer + offset, &cmd_type, sizeof(uint8_t)); offset += sizeof(uint8_t);
    memcpy(cmd_buffer + offset, &x, sizeof(int32_t)); offset += sizeof(int32_t);
    memcpy(cmd_buffer + offset, &y, sizeof(int32_t)); offset += sizeof(int32_t);
    memcpy(cmd_buffer + offset, &color, sizeof(uint8_t)); offset += sizeof(uint8_t);
    uint16_t len16 = (uint16_t)str_len;
    memcpy(cmd_buffer + offset, &len16, sizeof(uint16_t)); offset += sizeof(uint16_t);
    memcpy(cmd_buffer + offset, str, str_len);

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_STRING, cmd_buffer, total_size);
    fmrb_sys_free(cmd_buffer);

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_draw_char(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, char c, int32_t x, int32_t y, fmrb_color_t color) {
    char str[2] = {c, '\0'};
    return fmrb_gfx_draw_string(context, canvas_id, str, x, y, color);
}

fmrb_gfx_err_t fmrb_gfx_set_text_size(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, float size) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        float size;
    } text_size_cmd_t;

    text_size_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_SET_TEXT_SIZE,
        .size = size
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_SET_TEXT_SIZE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_set_text_color(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, fmrb_color_t fg, fmrb_color_t bg) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint8_t cmd_type;
        uint8_t fg;
        uint8_t bg;
    } text_color_cmd_t;

    text_color_cmd_t cmd = {
        .cmd_type = FMRB_LINK_GFX_SET_TEXT_COLOR,
        .fg = fg,
        .bg = bg
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_SET_TEXT_COLOR, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_screen(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id, fmrb_color_t color) {
    return fmrb_gfx_clear(context, canvas_id, color);
}

// Canvas management API implementations

fmrb_gfx_err_t fmrb_gfx_create_canvas(
    fmrb_gfx_context_t context,
    int32_t width, int32_t height,
    fmrb_canvas_handle_t *canvas_handle)
{
    if (!context || !canvas_handle || width <= 0 || height <= 0) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Assign new canvas ID
    uint16_t canvas_id = ctx->next_canvas_id++;
    if (canvas_id == FMRB_CANVAS_INVALID) {
        // Wrapped around, skip invalid value
        canvas_id = ctx->next_canvas_id++;
    }

    // Send create canvas command to host
    fmrb_link_graphics_create_canvas_t cmd = {
        .cmd_type = FMRB_LINK_GFX_CREATE_CANVAS,
        .canvas_id = canvas_id,
        .width = width,
        .height = height
    };

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_LINK_GFX_CREATE_CANVAS, &cmd, sizeof(cmd));
    if (ret == FMRB_GFX_OK) {
        *canvas_handle = canvas_id;
        ESP_LOGI(TAG, "Canvas created: ID=%u, %dx%d", canvas_id, width, height);
    }

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_delete_canvas(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle)
{
    if (!context || canvas_handle == FMRB_CANVAS_SCREEN || canvas_handle == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // If deleting current target, switch back to screen
    if (ctx->current_target == canvas_handle) {
        ctx->current_target = FMRB_CANVAS_SCREEN;
    }

    // Send delete canvas command to host
    fmrb_link_graphics_delete_canvas_t cmd = {
        .cmd_type = FMRB_LINK_GFX_DELETE_CANVAS,
        .canvas_id = canvas_handle
    };

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_LINK_GFX_DELETE_CANVAS, &cmd, sizeof(cmd));
    if (ret == FMRB_GFX_OK) {
        ESP_LOGI(TAG, "Canvas deleted: ID=%u", canvas_handle);
    }

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_set_target(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t target)
{
    if (!context || target == FMRB_CANVAS_INVALID) {
        ESP_LOGE(TAG, "set_target: invalid params (context=%p, target=%u)", context, target);
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    ESP_LOGD(TAG, "set_target: ctx=%p, initialized=%d, target=%u", ctx, ctx->initialized, target);
    if (!ctx->initialized) {
        ESP_LOGE(TAG, "set_target: context not initialized (ctx=%p, initialized=%d)", ctx, ctx->initialized);
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Update local state
    ctx->current_target = target;

    // Send set target command to host
    fmrb_link_graphics_set_target_t cmd = {
        .cmd_type = FMRB_LINK_GFX_SET_TARGET,
        .target_id = target
    };

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_LINK_GFX_SET_TARGET, &cmd, sizeof(cmd));
    if (ret == FMRB_GFX_OK) {
        ESP_LOGD(TAG, "Drawing target set: ID=%u %s", target,
                 target == FMRB_CANVAS_SCREEN ? "(screen)" : "(canvas)");
    }

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_push_canvas(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    fmrb_canvas_handle_t dest_canvas,
    int32_t x, int32_t y,
    fmrb_color_t transparent_color)
{
    if (!context || canvas_handle == FMRB_CANVAS_SCREEN || canvas_handle == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    if (dest_canvas == FMRB_CANVAS_INVALID) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Send push canvas command to host
    // Use 0xFF to indicate no transparency
    fmrb_link_graphics_push_canvas_t cmd = {
        .cmd_type = FMRB_LINK_GFX_PUSH_CANVAS,
        .canvas_id = canvas_handle,
        .dest_canvas_id = dest_canvas,
        .x = x,
        .y = y,
        .transparent_color = transparent_color,
        .use_transparency = (transparent_color == 0xFF) ? 0 : 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_PUSH_CANVAS, &cmd, sizeof(cmd));
}