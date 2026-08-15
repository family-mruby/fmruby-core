#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Color type (RGB332: 8-bit color, 3-bit R, 3-bit G, 2-bit B)
typedef uint8_t fmrb_color_t;

// Point structure
typedef struct {
    int16_t x, y;
} fmrb_point_t;

// Rectangle structure
typedef struct {
    int16_t x, y;
    uint16_t width, height;
} fmrb_rect_t;

// Font size enumeration
typedef enum {
    FMRB_FONT_SIZE_SMALL = 8,
    FMRB_FONT_SIZE_MEDIUM = 12,
    FMRB_FONT_SIZE_LARGE = 16,
    FMRB_FONT_SIZE_XLARGE = 20
} fmrb_font_size_t;

// Text buffer size for draw_text commands.
//
// This cannot simply be raised. A gfx command travels to the host task inside
// an fmrb_msg_t, whose payload (FMRB_MAX_MSG_PAYLOAD_SIZE, 176 bytes) was sized
// around exactly this buffer -- growing the text grows every message slot in
// every queue, and internal RAM is the scarce resource. fmrb_gfx_cmd.c asserts
// the relationship so a change here fails the build rather than the display.
//
// A caller that needs a longer line splits it: one Japanese character is three
// bytes, so 128 bytes is 42 characters, and the editor draws a row in several
// commands (see draw_edit_row). Truncation, when it does happen, lands on a
// UTF-8 boundary rather than mid-character.
#define FMRB_GFX_MAX_TEXT_LEN 128

// Graphics error codes
typedef enum {
    FMRB_GFX_OK = 0,
    FMRB_GFX_ERR_INVALID_PARAM = -1,
    FMRB_GFX_ERR_NO_MEMORY = -2,
    FMRB_GFX_ERR_NOT_INITIALIZED = -3,
    FMRB_GFX_ERR_FAILED = -4
} fmrb_gfx_err_t;

// Canvas handle type (0 = main screen, 1-65534 = canvas ID)
typedef uint16_t fmrb_canvas_handle_t;
#define FMRB_CANVAS_SCREEN 0      // Main screen
#define FMRB_CANVAS_RENDER 0xFFF0 
#define FMRB_CANVAS_INVALID 0xFFFF

// Graphics configuration
typedef struct {
    uint16_t screen_width;
    uint16_t screen_height;
    uint8_t bits_per_pixel;
    bool double_buffered;
} fmrb_gfx_config_t;

// Forward declaration for transport handle
// (fully defined in fmrb_transport.h, included by implementation files)
#ifndef FMRB_TRANSPORT_HANDLE_DEFINED
#define FMRB_TRANSPORT_HANDLE_DEFINED
typedef void* fmrb_transport_handle_t;
#endif

// Graphics context implementation structure
typedef struct {
    fmrb_gfx_config_t config;
    fmrb_transport_handle_t transport;
    fmrb_rect_t clip_rect;
    bool clip_enabled;
    bool initialized;
    fmrb_canvas_handle_t current_target;  // 0=screen, other=canvas
    uint16_t next_canvas_id;              // Canvas ID generator
} fmrb_gfx_context_impl_t;

// Graphics context handle
typedef fmrb_gfx_context_impl_t* fmrb_gfx_context_t;

// Color constants (RGB332 format)
#define FMRB_COLOR_BLACK    0x00  // R=0, G=0, B=0
#define FMRB_COLOR_WHITE    0xFF  // R=7, G=7, B=3
#define FMRB_COLOR_RED      0xE0  // R=7, G=0, B=0
#define FMRB_COLOR_GREEN    0x1C  // R=0, G=7, B=0
#define FMRB_COLOR_BLUE     0x03  // R=0, G=0, B=3
#define FMRB_COLOR_YELLOW   0xFC  // R=7, G=7, B=0
#define FMRB_COLOR_CYAN     0x1F  // R=0, G=7, B=3
#define FMRB_COLOR_MAGENTA  0xE3  // R=7, G=0, B=3
#define FMRB_COLOR_GRAY     0x6D  // R=3, G=3, B=1

// Utility macros for RGB332 color manipulation
// Convert RGB (0-255 each) to RGB332 (8-bit)
#define FMRB_COLOR_RGB332(r, g, b) \
    ((uint8_t)((((r) >> 5) << 5) | (((g) >> 5) << 2) | ((b) >> 6)))

// Convert RGB332 back to RGB (0-255 each, approximate)
#define FMRB_COLOR_GET_R(c) ((((c) >> 5) & 0x07) * 36)  // 0-252
#define FMRB_COLOR_GET_G(c) ((((c) >> 2) & 0x07) * 36)  // 0-252
#define FMRB_COLOR_GET_B(c) (((c) & 0x03) * 85)         // 0-255

// Legacy compatibility (RGB to RGB332)
#define FMRB_COLOR_RGB(r, g, b) FMRB_COLOR_RGB332(r, g, b)

/**
 * @brief Initialize graphics subsystem
 *
 * Creates the global graphics context. This should be called once at startup.
 * Subsequent calls will return success without reinitializing.
 * Use fmrb_gfx_get_global_context() to obtain the initialized context.
 *
 * @param config Graphics configuration
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_init(const fmrb_gfx_config_t *config);

/**
 * @brief Deinitialize graphics subsystem
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_deinit(void);

/**
 * @brief Get global graphics context
 * @return Global graphics context (NULL if not initialized)
 *
 * Returns the shared global graphics context that is used by all FmrbGfx instances.
 * This context is created by the first call to fmrb_gfx_init() and reused thereafter.
 */
fmrb_gfx_context_t fmrb_gfx_get_global_context(void);

/**
 * @brief Present/swap buffers (for double buffering)
 * @param context Graphics context
 * @param canvas_id Target canvas ID
 * @return Graphics error code
 */
//fmrb_gfx_err_t fmrb_gfx_present(fmrb_gfx_context_t context, fmrb_canvas_handle_t canvas_id);

// LovyanGFX compatible API (snake_case)

// Canvas management API (for Window drawing buffers)

/**
 * @brief Create canvas (offscreen drawing buffer)
 * @param context Graphics context
 * @param width Canvas width
 * @param height Canvas height
 * @param z_order Z-order of the canvas
 * @param use_transparent Enable color-key transparency during composition
 * @param transparent_color RGB332 color treated as transparent (ignored if use_transparent=false)
 * @param canvas_handle Pointer to store canvas handle
 * @return Graphics error code
 *
 * Use case: Create drawing buffer for application window
 */
fmrb_gfx_err_t fmrb_gfx_create_canvas(
    fmrb_gfx_context_t context,
    int32_t width, int32_t height,
    int16_t z_order,
    bool use_transparent,
    uint8_t transparent_color,
    fmrb_canvas_handle_t *canvas_handle);

/**
 * @brief Delete canvas
 * @param context Graphics context
 * @param canvas_handle Canvas handle to delete
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_delete_canvas(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle);

// Composite region descriptor (public API mirror of the link-protocol struct).
// Each region describes one sub-rect copy from a canvas render buffer onto the
// screen buffer at compositing time. When transparent is true the per-pixel
// color-key compare uses the canvas's transparent_color; otherwise the rect is
// memcpy'd.
typedef struct {
    int16_t src_x, src_y;     // Source top-left inside the canvas render buffer
    int16_t dst_x, dst_y;     // Destination offset relative to the canvas position
    int16_t w, h;             // Region dimensions in pixels
    uint8_t use_transparent;  // 1 = color-key compare, 0 = opaque memcpy
} fmrb_gfx_composite_region_t;

#define FMRB_GFX_MAX_COMPOSITE_REGIONS 8

/**
 * @brief Replace the composite region list for a canvas (asynchronous).
 *
 * When count > 0, the WROVER-side compositor copies only the listed regions
 * instead of pushing the whole active area. count = 0 clears regions and
 * restores the default full-area pushSprite path. Typically used to keep
 * rounded-corner windows fast: interior region is opaque (memcpy fast path)
 * and only the corner regions stay transparent (per-pixel compare).
 *
 * @param context Graphics context
 * @param canvas_handle Canvas to update
 * @param regions Array of region descriptors (may be NULL when count == 0)
 * @param count Number of regions (0..FMRB_GFX_MAX_COMPOSITE_REGIONS)
 * @return FMRB_GFX_OK on success
 */
fmrb_gfx_err_t fmrb_gfx_set_composite_regions(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    const fmrb_gfx_composite_region_t *regions,
    uint8_t count);

/**
 * @brief Set the composite source viewport of a canvas (asynchronous).
 *
 * The compositor shows only the (src_x, src_y, view_w, view_h) sub-rect of
 * the canvas at its push position, which turns a large canvas into a
 * hardware-scrolled surface: updating the viewport is a register write, no
 * redraw. view_w == 0 clears the viewport (full-canvas composite, default).
 * Implemented by the Modern (P4/PPA) backend only; apps must gate usage on
 * the platform.
 *
 * @param context Graphics context
 * @param canvas_handle Canvas to update
 * @param src_x Left edge of the visible sub-rect inside the canvas
 * @param src_y Top edge of the visible sub-rect inside the canvas
 * @param view_w Visible width (0 = clear viewport)
 * @param view_h Visible height
 * @return FMRB_GFX_OK on success
 */
fmrb_gfx_err_t fmrb_gfx_set_canvas_viewport(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    uint16_t src_x, uint16_t src_y,
    uint16_t view_w, uint16_t view_h);

/**
 * @brief Confine sprite compositing of a canvas to a sub-rect (asynchronous).
 *
 * Sprites are composited on top of everything the canvas itself drew, so
 * without a clip they paint over the window frame and title bar the app drew
 * into the same canvas. The rect uses the same coordinate space as sprite
 * instance positions (canvas-local; viewport-relative when a viewport is set)
 * and is clamped to the canvas. w == 0 or h == 0 clears the clip, leaving
 * sprites bounded by the canvas only (the default).
 *
 * @param context Graphics context
 * @param canvas_handle Canvas to update
 * @param x Left edge of the sprite clip rect
 * @param y Top edge of the sprite clip rect
 * @param w Clip width (0 = clear clip)
 * @param h Clip height (0 = clear clip)
 * @return FMRB_GFX_OK on success
 */
fmrb_gfx_err_t fmrb_gfx_set_sprite_clip(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_handle,
    uint16_t x, uint16_t y,
    uint16_t w, uint16_t h);

// Cursor control API (global resource)

// Sprite API

/**
 * @brief Create a sprite image buffer on graphics-audio side
 * @param context Graphics context
 * @param canvas_id Parent canvas
 * @param width Image width
 * @param height Image height
 * @param transparent_color Color key for transparency
 * @param use_transparent Enable transparency
 * @return image_id (>0) on success, 0 on failure
 */
uint16_t fmrb_gfx_create_sprite_image(
    fmrb_gfx_context_t context,
    uint16_t canvas_id,
    uint16_t width, uint16_t height,
    uint8_t transparent_color, bool use_transparent);

/**
 * @brief Create a sprite instance (placement)
 * @param canvas_id Parent canvas
 * @param image_ids Array of image IDs for animation frames
 * @param frame_count Number of frames
 * @param x Window-local X position
 * @param y Window-local Y position
 * @param z_order Z-order within window
 * @return instance_id (>0) on success, 0 on failure
 */
uint16_t fmrb_gfx_create_sprite_instance(
    fmrb_gfx_context_t context,
    uint16_t canvas_id,
    const uint16_t *image_ids, uint8_t frame_count,
    int16_t x, int16_t y, int16_t z_order);

// ---------- GfxBlock VM (draw-batch programs) ----------

/**
 * @brief Register a drawing program on the WROVER side (synchronous).
 * @param context Graphics context
 * @param canvas_id Target canvas
 * @param bytecode Compiled bytecode buffer (copied into payload)
 * @param bytecode_len Length in bytes
 * @param strtable String table buffer (copied into payload)
 * @param strtable_len Length in bytes
 * @param out_prog_id Receives the prog_id allocated by WROVER (0xFF = pool full)
 * @return FMRB_GFX_OK on success
 */
fmrb_gfx_err_t fmrb_gfx_define_prog(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_id,
    const uint8_t *bytecode, uint16_t bytecode_len,
    const uint8_t *strtable, uint16_t strtable_len,
    uint8_t *out_prog_id);

/**
 * @brief Execute a registered program, applying register updates (asynchronous).
 * @param context Graphics context
 * @param canvas_id Target canvas
 * @param prog_id Program id returned by define_prog
 * @param reg_updates Packed [uint8_t reg_id, int16_t value] * reg_count
 * @param reg_count Number of register updates
 * @return FMRB_GFX_OK on success
 */
fmrb_gfx_err_t fmrb_gfx_exec_prog(
    fmrb_gfx_context_t context,
    fmrb_canvas_handle_t canvas_id,
    uint8_t prog_id,
    const uint8_t *reg_updates,
    uint8_t reg_count);

/**
 * @brief Delete a registered program, freeing the WROVER slot (asynchronous).
 */
fmrb_gfx_err_t fmrb_gfx_delete_prog(
    fmrb_gfx_context_t context,
    uint8_t prog_id);

#ifdef __cplusplus
}
#endif