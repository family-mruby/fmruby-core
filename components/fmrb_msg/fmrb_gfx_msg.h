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
    GFX_CMD_SET_FONT,
    GFX_CMD_BLEND_RECT,
    GFX_CMD_SET_OUTPUT_LEVEL,
    GFX_CMD_SET_CHROMA_LEVEL,
    // Sprite image target switch. Must travel the same queue as the pixel
    // commands it brackets, otherwise the trailing draws can be processed on
    // WROVER after target=0 already arrived through a faster path, causing
    // tail-of-icon clipping in launcher sprites.
    GFX_CMD_SET_SPRITE_IMAGE_TARGET,
    // Sprite lifecycle / instance state. All routed through host_task queue so
    // they stay in order with surrounding draw commands.
    GFX_CMD_DELETE_SPRITE_IMAGE,
    GFX_CMD_DELETE_SPRITE_INSTANCE,
    GFX_CMD_SPRITE_INSTANCE_MOVE,
    GFX_CMD_SPRITE_INSTANCE_SET_VISIBLE,
    GFX_CMD_SPRITE_INSTANCE_SET_FRAME,
    GFX_CMD_DELETE_ALL_SPRITES,
    GFX_CMD_DELETE_CANVAS,
    GFX_CMD_SET_COMPOSITE_REGIONS,  // Async; updates per-canvas sub-rect compositing list
    GFX_CMD_SET_CANVAS_VIEWPORT,    // Async; source-rect scroll register (P4 backend only)
    // Sync commands (require response from WROVER)
    GFX_CMD_CREATE_CANVAS,
    GFX_CMD_CREATE_SPRITE_IMAGE,
    GFX_CMD_CREATE_SPRITE_INSTANCE,
    GFX_CMD_LOAD_SPRITE_IMAGE_BMP,  // Sync (returns success/failure)
    GFX_CMD_DEFINE_PROG,         // Sync; returns prog_id
    GFX_CMD_EXEC_PROG,           // Async
    GFX_CMD_DELETE_PROG,         // Async
    GFX_CMD_GET_PIXEL,           // Sync; returns RGB332 byte
    // CREATE_MASK is NOT in this enum — it is sent directly via
    // fmrb_transport_send_sync (variable-length payload up to ~10 KB,
    // bypasses host_task batching). DELETE_MASK and DRAW_IMAGE_MASKED
    // ride the batching queue so they preserve order with surrounding
    // drawing commands.
    GFX_CMD_DELETE_MASK,         // Async
    GFX_CMD_DRAW_IMAGE_MASKED,   // Async
    // Stateless sub-rect stamp from a SpriteImage onto a canvas. No
    // SpriteInstance allocated. Source pixels equal to the SpriteImage's
    // transparent_color (when use_transparent is set) are skipped.
    GFX_CMD_DRAW_TILE            // Async
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
            uint8_t hybrid_mode;  // 0 = use current font, 1 = ASCII/JA hybrid render
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
            uint8_t family;  // 0=default, 1=ja
            uint8_t size;    // pixel height (JA: 12 supported now)
        } set_font;
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
            uint16_t image_id;  // 0 = reset target to canvas
        } set_sprite_image_target;
        struct {
            uint16_t image_id;
        } delete_sprite_image;
        // Sync load: path is NUL-terminated and stays within this fixed buffer.
        // Caps at FMRB_GFX_LOAD_BMP_PATH_MAX bytes including NUL.
        struct {
            uint16_t image_id;
            char path[120];
        } load_sprite_image_bmp;
        struct {
            uint16_t instance_id;
        } delete_sprite_instance;
        struct {
            uint16_t instance_id;
            int16_t x, y;
        } sprite_instance_move;
        struct {
            uint16_t instance_id;
            uint8_t visible;  // 0 = hidden, 1 = visible
        } sprite_instance_set_visible;
        struct {
            uint16_t instance_id;
            uint8_t frame_index;
        } sprite_instance_set_frame;
        // delete_all_sprites uses cmd->canvas_id only — no extra params.
        struct {
            uint8_t frame_count;
            uint16_t image_ids[8];
            int16_t x, y;
            int16_t z_order;
        } create_sprite_instance;
        // GfxBlock VM: define program. bytecode_buf/strtable_buf are fmrb_malloc'd
        // in the caller; the Host Task copies their contents into the payload and
        // frees them after send. Ownership transfers to Host Task.
        struct {
            uint8_t *bytecode_buf;
            uint16_t bytecode_len;
            uint8_t *strtable_buf;
            uint16_t strtable_len;
        } define_prog;
        // GfxBlock VM: execute program. Inlined fixed-size buffer so the
        // gfx_cmd_t can travel through the host_task queue without lifetime
        // worries. Packed as [uint8_t reg_id, int16_t value] * reg_count
        // = 3 bytes per entry. 16 regs max matches the prior stack budget.
        struct {
            uint8_t prog_id;
            uint8_t reg_count;
            uint8_t reg_updates[3 * 16];
        } exec_prog;
        struct {
            uint8_t prog_id;
        } delete_prog;
        // Sync readback: returns 2 bytes (color, status) via sync_ctx.
        struct {
            int16_t x, y;
        } get_pixel;
        // Composite region list update (async). 8 regions x 9 bytes = 72 B
        // inline; fits comfortably inside the gfx_cmd_t payload budget.
        struct {
            uint8_t count;  // 0 = clear, 1..FMRB_GFX_MAX_COMPOSITE_REGIONS = valid entries
            fmrb_gfx_composite_region_t regions[FMRB_GFX_MAX_COMPOSITE_REGIONS];
        } set_composite_regions;
        // Canvas viewport (source-rect scroll). view_w == 0 clears.
        struct {
            uint16_t src_x, src_y;
            uint16_t view_w, view_h;
        } set_canvas_viewport;
        // Mask lifetime / masked image blit (both async).
        struct {
            uint16_t mask_id;
        } delete_mask;
        struct {
            uint16_t image_id;
            uint16_t mask_id;
            int16_t x, y;
        } draw_image_masked;
        struct {
            uint16_t image_id;
            int16_t  src_x, src_y;
            uint16_t w, h;
            int16_t  dst_x, dst_y;
        } draw_tile;
    } params;

    // Sync context pointer (NULL = fire-and-forget, non-NULL = response expected)
    // Points to caller's stack. Caller blocks on sync->done until Host Task signals.
    struct gfx_cmd_sync_ctx *sync;

    // Set when the sender did NOT take a flow-control slot, so the host task
    // knows not to give one back for this command. Everything an app draws is
    // metered (fmrb_gfx_submit); this is for the few commands issued from a
    // task that must not block on a semaphore an app paces - see
    // fmrb_gfx_submit_unmetered.
    uint8_t unmetered;
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
