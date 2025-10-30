#include "fmrb_gfx.h"
#include "fmrb_hal.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_transport.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "fmrb_gfx";

typedef struct {
    fmrb_gfx_config_t config;
    fmrb_link_transport_handle_t transport;
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

fmrb_gfx_err_t fmrb_gfx_init(const fmrb_gfx_config_t *config, fmrb_gfx_context_t *context) {
    if (!config || !context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = fmrb_sys_malloc(sizeof(fmrb_gfx_context_impl_t));
    if (!ctx) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

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
        fmrb_sys_free(ctx);
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
        fmrb_link_transport_deinit(ctx->transport);
    }

    ctx->initialized = false;
    fmrb_sys_free(ctx);

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

    // Send only color for fill_screen/clear command
    // Payload: just the color value (RGB565)
    fmrb_color_t color_val = color;
    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_SCREEN, &color_val, sizeof(color_val));
}

fmrb_gfx_err_t fmrb_gfx_clear_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_clear_t clear_cmd = {
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_SCREEN, &clear_cmd, sizeof(clear_cmd));
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

    fmrb_link_graphics_pixel_t pixel_cmd = {
        .x = x,
        .y = y,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_PIXEL, &pixel_cmd, sizeof(pixel_cmd));
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

    fmrb_link_graphics_line_t line_cmd = {
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .color = color
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_LINE, &line_cmd, sizeof(line_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_rect_t rect_cmd = {
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color,
        .filled = false
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color) {
    if (!context || !rect) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    fmrb_link_graphics_rect_t rect_cmd = {
        .x = rect->x,
        .y = rect->y,
        .width = rect->width,
        .height = rect->height,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_RECT, &rect_cmd, sizeof(rect_cmd));
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
    size_t total_size = sizeof(fmrb_link_graphics_text_t) + text_len;
    uint8_t *cmd_buffer = fmrb_sys_malloc(total_size);
    if (!cmd_buffer) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    fmrb_link_graphics_text_t *text_cmd = (fmrb_link_graphics_text_t*)cmd_buffer;
    text_cmd->x = x;
    text_cmd->y = y;
    text_cmd->color = color;
    text_cmd->font_size = font_size;
    text_cmd->text_len = text_len;

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

fmrb_gfx_err_t fmrb_gfx_present(fmrb_gfx_context_t context) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Use legacy present command (0x08) since it's simple and works
    return send_graphics_command(ctx, 0x08, NULL, 0);
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

// LovyanGFX compatible API implementations

fmrb_gfx_err_t fmrb_gfx_draw_pixel(fmrb_gfx_context_t context, int32_t x, int32_t y, fmrb_color_t color) {
    return fmrb_gfx_set_pixel(context, (int16_t)x, (int16_t)y, color);
}

fmrb_gfx_err_t fmrb_gfx_draw_fast_vline(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t h, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Draw vertical line as a thin rectangle
    fmrb_link_graphics_rect_t rect_cmd = {
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .width = 1,
        .height = (uint16_t)h,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_fast_hline(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t w, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Draw horizontal line as a thin rectangle
    fmrb_link_graphics_rect_t rect_cmd = {
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .width = (uint16_t)w,
        .height = 1,
        .color = color,
        .filled = true
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_RECT, &rect_cmd, sizeof(rect_cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_round_rect(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    // Payload: x, y, w, h, r, color, filled
    typedef struct __attribute__((packed)) {
        int32_t x, y, w, h, r;
        uint32_t color;
        uint8_t filled;
    } round_rect_cmd_t;

    round_rect_cmd_t cmd = {
        .x = x, .y = y, .w = w, .h = h, .r = r,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_ROUND_RECT, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_round_rect(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, w, h, r;
        uint32_t color;
        uint8_t filled;
    } round_rect_cmd_t;

    round_rect_cmd_t cmd = {
        .x = x, .y = y, .w = w, .h = h, .r = r,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_ROUND_RECT, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_circle(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, r;
        uint32_t color;
        uint8_t filled;
    } circle_cmd_t;

    circle_cmd_t cmd = {
        .x = x, .y = y, .r = r,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_CIRCLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_circle(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, r;
        uint32_t color;
        uint8_t filled;
    } circle_cmd_t;

    circle_cmd_t cmd = {
        .x = x, .y = y, .r = r,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_CIRCLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_ellipse(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t rx, int32_t ry, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, rx, ry;
        uint32_t color;
        uint8_t filled;
    } ellipse_cmd_t;

    ellipse_cmd_t cmd = {
        .x = x, .y = y, .rx = rx, .ry = ry,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_ELLIPSE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_ellipse(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t rx, int32_t ry, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, rx, ry;
        uint32_t color;
        uint8_t filled;
    } ellipse_cmd_t;

    ellipse_cmd_t cmd = {
        .x = x, .y = y, .rx = rx, .ry = ry,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_ELLIPSE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_triangle(fmrb_gfx_context_t context, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x0, y0, x1, y1, x2, y2;
        uint32_t color;
        uint8_t filled;
    } triangle_cmd_t;

    triangle_cmd_t cmd = {
        .x0 = x0, .y0 = y0,
        .x1 = x1, .y1 = y1,
        .x2 = x2, .y2 = y2,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_TRIANGLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_triangle(fmrb_gfx_context_t context, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x0, y0, x1, y1, x2, y2;
        uint32_t color;
        uint8_t filled;
    } triangle_cmd_t;

    triangle_cmd_t cmd = {
        .x0 = x0, .y0 = y0,
        .x1 = x1, .y1 = y1,
        .x2 = x2, .y2 = y2,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_TRIANGLE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_arc(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, r0, r1;
        float angle0, angle1;
        uint32_t color;
        uint8_t filled;
    } arc_cmd_t;

    arc_cmd_t cmd = {
        .x = x, .y = y, .r0 = r0, .r1 = r1,
        .angle0 = angle0, .angle1 = angle1,
        .color = color,
        .filled = 0
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_ARC, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_arc(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1, fmrb_color_t color) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        int32_t x, y, r0, r1;
        float angle0, angle1;
        uint32_t color;
        uint8_t filled;
    } arc_cmd_t;

    arc_cmd_t cmd = {
        .x = x, .y = y, .r0 = r0, .r1 = r1,
        .angle0 = angle0, .angle1 = angle1,
        .color = color,
        .filled = 1
    };

    return send_graphics_command(ctx, FMRB_LINK_GFX_FILL_ARC, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_draw_string(fmrb_gfx_context_t context, const char *str, int32_t x, int32_t y, fmrb_color_t color) {
    if (!context || !str) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    size_t str_len = strlen(str);
    if (str_len > 255) {
        str_len = 255;
    }

    // Allocate buffer for command + string
    size_t total_size = sizeof(int32_t) * 2 + sizeof(uint32_t) + sizeof(uint16_t) + str_len;
    uint8_t *cmd_buffer = fmrb_sys_malloc(total_size);
    if (!cmd_buffer) {
        return FMRB_GFX_ERR_NO_MEMORY;
    }

    size_t offset = 0;
    memcpy(cmd_buffer + offset, &x, sizeof(int32_t)); offset += sizeof(int32_t);
    memcpy(cmd_buffer + offset, &y, sizeof(int32_t)); offset += sizeof(int32_t);
    memcpy(cmd_buffer + offset, &color, sizeof(uint32_t)); offset += sizeof(uint32_t);
    uint16_t len16 = (uint16_t)str_len;
    memcpy(cmd_buffer + offset, &len16, sizeof(uint16_t)); offset += sizeof(uint16_t);
    memcpy(cmd_buffer + offset, str, str_len);

    fmrb_gfx_err_t ret = send_graphics_command(ctx, FMRB_LINK_GFX_DRAW_STRING, cmd_buffer, total_size);
    fmrb_sys_free(cmd_buffer);

    return ret;
}

fmrb_gfx_err_t fmrb_gfx_draw_char(fmrb_gfx_context_t context, char c, int32_t x, int32_t y, fmrb_color_t color) {
    char str[2] = {c, '\0'};
    return fmrb_gfx_draw_string(context, str, x, y, color);
}

fmrb_gfx_err_t fmrb_gfx_set_text_size(fmrb_gfx_context_t context, float size) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        float size;
    } text_size_cmd_t;

    text_size_cmd_t cmd = { .size = size };

    return send_graphics_command(ctx, FMRB_LINK_GFX_SET_TEXT_SIZE, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_set_text_color(fmrb_gfx_context_t context, fmrb_color_t fg, fmrb_color_t bg) {
    if (!context) {
        return FMRB_GFX_ERR_INVALID_PARAM;
    }

    fmrb_gfx_context_impl_t *ctx = (fmrb_gfx_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_GFX_ERR_NOT_INITIALIZED;
    }

    typedef struct __attribute__((packed)) {
        uint32_t fg;
        uint32_t bg;
    } text_color_cmd_t;

    text_color_cmd_t cmd = { .fg = fg, .bg = bg };

    return send_graphics_command(ctx, FMRB_LINK_GFX_SET_TEXT_COLOR, &cmd, sizeof(cmd));
}

fmrb_gfx_err_t fmrb_gfx_fill_screen(fmrb_gfx_context_t context, fmrb_color_t color) {
    return fmrb_gfx_clear(context, color);
}