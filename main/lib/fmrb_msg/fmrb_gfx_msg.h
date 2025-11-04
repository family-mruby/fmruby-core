#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fmrb_msg.h"
#include "fmrb_gfx.h"

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
    GFX_CMD_TEXT,
    GFX_CMD_PRESENT
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
            char text[32];
            fmrb_color_t color;
            fmrb_font_size_t font_size;
        } text;
        // present command has no additional params (uses canvas_id only)
    } params;
} gfx_cmd_t;

#ifdef __cplusplus
}
#endif
