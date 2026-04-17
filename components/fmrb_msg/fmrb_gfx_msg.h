#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fmrb_msg.h"
#include "fmrb_gfx.h"
#include "fmrb_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Graphics message definitions for FMRB_MSG_TYPE_APP_GFX
 *
 * This header defines the message payload structure for graphics commands
 * sent via the fmrb_msg system to the Host Task.
 */

// Graphics command types
typedef enum {
    GFX_CMD_CLEAR = 0,
    GFX_CMD_PIXEL,
    GFX_CMD_LINE,
    GFX_CMD_RECT,
    GFX_CMD_CIRCLE,
    GFX_CMD_TEXT,
    GFX_CMD_PRESENT,
    GFX_CMD_CREATE_IMAGE_FROM_FILE,
    GFX_CMD_DRAW_IMAGE,
    GFX_CMD_DELETE_IMAGE,
    GFX_CMD_ROUND_RECT,
    GFX_CMD_ELLIPSE,
    GFX_CMD_TRIANGLE,
    GFX_CMD_ARC,
    GFX_CMD_TEXT_SIZE,
    GFX_CMD_BLEND_RECT,
    GFX_CMD_SET_OUTPUT_LEVEL,
    GFX_CMD_SET_CHROMA_LEVEL,
    // Sync commands (require response from WROVER)
    GFX_CMD_CREATE_CANVAS,
    GFX_CMD_CREATE_SPRITE_IMAGE,
    GFX_CMD_CREATE_SPRITE_INSTANCE
} gfx_cmd_type_t;

// Graphics command structure
typedef struct {
    gfx_cmd_type_t cmd_type;
    fmrb_canvas_handle_t canvas_id;
    union {
        struct {
            fmrb_color_t color;
        } clear;
        struct {
            int16_t x;
            int16_t y;
            fmrb_color_t color;
        } pixel;
        struct {
            int16_t x1;
            int16_t y1;
            int16_t x2;
            int16_t y2;
            fmrb_color_t color;
        } line;
        struct {
            fmrb_rect_t rect;
            fmrb_color_t color;
            bool filled;
        } rect;
        struct {
            int16_t x;
            int16_t y;
            int16_t radius;
            fmrb_color_t color;
            bool filled;
        } circle;
        struct {
            int16_t x;
            int16_t y;
            char text[FMRB_GFX_MAX_TEXT_LEN];
            fmrb_color_t color;
            fmrb_color_t bg_color;
            bool bg_transparent;  // true = no background, false = use bg_color
            fmrb_font_size_t font_size;
        } text;
        struct {
            int16_t x;  // Screen X position
            int16_t y;  // Screen Y position
            fmrb_color_t transparent_color;  // Transparent color (0xFF = no transparency)
        } present;
        struct {
            uint16_t image_id;
            int16_t x;
            int16_t y;
            uint8_t flags;
            int16_t scale_x_fp8;  // Fixed-point scale (x256): 256 = 1.0
            int16_t scale_y_fp8;  // 0 = same as scale_x
        } draw_image;
        struct {
            uint16_t image_id;
        } delete_image;
        struct {
            int16_t x, y;
            int16_t w, h;
            int16_t radius;
            fmrb_color_t color;
            bool filled;
        } round_rect;
        struct {
            int16_t x, y;
            int16_t rx, ry;
            fmrb_color_t color;
            bool filled;
        } ellipse;
        struct {
            int16_t x0, y0;
            int16_t x1, y1;
            int16_t x2, y2;
            fmrb_color_t color;
            bool filled;
        } triangle;
        struct {
            int16_t x, y;
            int16_t r0, r1;
            int16_t angle0, angle1;
            fmrb_color_t color;
            bool filled;
        } arc;
        struct {
            uint8_t size;
        } text_size;
        struct {
            fmrb_rect_t rect;
            fmrb_color_t color;
            uint8_t mode;  // FMRB_BLEND_MODE_*
        } blend_rect;
        struct {
            uint8_t level;
        } set_output_level;
        struct {
            uint8_t level;
        } set_chroma_level;
        struct {
            int32_t width, height;
            int16_t z_order;
            uint8_t use_transparent;
            uint8_t transparent_color;
        } create_canvas;
        struct {
            uint16_t width, height;
            uint8_t transparent_color;
            uint8_t use_transparent;
        } create_sprite_image;
        struct {
            uint8_t frame_count;
            uint16_t image_ids[8];
            int16_t x, y;
            int16_t z_order;
        } create_sprite_instance;
    } params;

    // Sync context pointer (NULL = fire-and-forget, non-NULL = response expected)
    // Points to caller's stack. Caller blocks on sync->done until Host Task signals.
    struct gfx_cmd_sync_ctx *sync;
} gfx_cmd_t;

// Sync context for response-awaiting GFX commands (allocated on caller's stack)
typedef struct gfx_cmd_sync_ctx {
    fmrb_semaphore_t done;        // Signaled when response is ready
    uint8_t *response_buf;        // Response data buffer (caller-owned)
    uint16_t response_len;        // in: buffer size, out: actual data length
    int8_t result;                // 0=success, <0=error
} gfx_cmd_sync_ctx_t;

#ifdef __cplusplus
}
#endif
