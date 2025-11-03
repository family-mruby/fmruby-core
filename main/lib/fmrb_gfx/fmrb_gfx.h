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

// Graphics error codes
typedef enum {
    FMRB_GFX_OK = 0,
    FMRB_GFX_ERR_INVALID_PARAM = -1,
    FMRB_GFX_ERR_NO_MEMORY = -2,
    FMRB_GFX_ERR_NOT_INITIALIZED = -3,
    FMRB_GFX_ERR_FAILED = -4
} fmrb_gfx_err_t;

// Graphics context handle
typedef void* fmrb_gfx_context_t;

// Graphics configuration
typedef struct {
    uint16_t screen_width;
    uint16_t screen_height;
    uint8_t bits_per_pixel;
    bool double_buffered;
} fmrb_gfx_config_t;

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
 * @param config Graphics configuration
 * @param context Pointer to store graphics context
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_init(const fmrb_gfx_config_t *config, fmrb_gfx_context_t *context);

/**
 * @brief Deinitialize graphics subsystem
 * @param context Graphics context
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_deinit(fmrb_gfx_context_t context);

/**
 * @brief Clear screen with specified color
 * @param context Graphics context
 * @param color Clear color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_clear(fmrb_gfx_context_t context, fmrb_color_t color);

/**
 * @brief Clear specified region with color
 * @param context Graphics context
 * @param rect Region to clear
 * @param color Clear color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_clear_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color);

/**
 * @brief Set pixel at specified coordinates
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_set_pixel(fmrb_gfx_context_t context, int16_t x, int16_t y, fmrb_color_t color);

/**
 * @brief Get pixel color at specified coordinates
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pointer to store pixel color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_get_pixel(fmrb_gfx_context_t context, int16_t x, int16_t y, fmrb_color_t *color);

/**
 * @brief Draw line between two points
 * @param context Graphics context
 * @param x1 Start X coordinate
 * @param y1 Start Y coordinate
 * @param x2 End X coordinate
 * @param y2 End Y coordinate
 * @param color Line color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_line(fmrb_gfx_context_t context, int16_t x1, int16_t y1, int16_t x2, int16_t y2, fmrb_color_t color);

/**
 * @brief Draw rectangle outline
 * @param context Graphics context
 * @param rect Rectangle to draw
 * @param color Rectangle color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color);

/**
 * @brief Draw filled rectangle
 * @param context Graphics context
 * @param rect Rectangle to fill
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect, fmrb_color_t color);

/**
 * @brief Draw text at specified position
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text string (null-terminated)
 * @param color Text color
 * @param font_size Font size
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_text(fmrb_gfx_context_t context, int16_t x, int16_t y, const char *text, fmrb_color_t color, fmrb_font_size_t font_size);

/**
 * @brief Get text dimensions
 * @param text Text string (null-terminated)
 * @param font_size Font size
 * @param width Pointer to store text width
 * @param height Pointer to store text height
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_get_text_size(const char *text, fmrb_font_size_t font_size, uint16_t *width, uint16_t *height);

/**
 * @brief Present/swap buffers (for double buffering)
 * @param context Graphics context
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_present(fmrb_gfx_context_t context);

/**
 * @brief Set clipping region
 * @param context Graphics context
 * @param rect Clipping rectangle (NULL to disable clipping)
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_set_clip_rect(fmrb_gfx_context_t context, const fmrb_rect_t *rect);

// LovyanGFX compatible API (snake_case)

/**
 * @brief Draw a pixel (LovyanGFX compatible)
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_pixel(fmrb_gfx_context_t context, int32_t x, int32_t y, fmrb_color_t color);

/**
 * @brief Draw fast vertical line (LovyanGFX compatible)
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param h Height
 * @param color Line color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_fast_vline(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t h, fmrb_color_t color);

/**
 * @brief Draw fast horizontal line (LovyanGFX compatible)
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param color Line color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_fast_hline(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t w, fmrb_color_t color);

/**
 * @brief Draw rounded rectangle outline (LovyanGFX compatible)
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param r Corner radius
 * @param color Rectangle color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_round_rect(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, fmrb_color_t color);

/**
 * @brief Draw filled rounded rectangle (LovyanGFX compatible)
 * @param context Graphics context
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param r Corner radius
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_round_rect(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, fmrb_color_t color);

/**
 * @brief Draw circle outline (LovyanGFX compatible)
 * @param context Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param r Radius
 * @param color Circle color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_circle(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r, fmrb_color_t color);

/**
 * @brief Draw filled circle (LovyanGFX compatible)
 * @param context Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param r Radius
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_circle(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r, fmrb_color_t color);

/**
 * @brief Draw ellipse outline (LovyanGFX compatible)
 * @param context Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param rx Radius X
 * @param ry Radius Y
 * @param color Ellipse color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_ellipse(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t rx, int32_t ry, fmrb_color_t color);

/**
 * @brief Draw filled ellipse (LovyanGFX compatible)
 * @param context Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param rx Radius X
 * @param ry Radius Y
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_ellipse(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t rx, int32_t ry, fmrb_color_t color);

/**
 * @brief Draw triangle outline (LovyanGFX compatible)
 * @param context Graphics context
 * @param x0 First vertex X
 * @param y0 First vertex Y
 * @param x1 Second vertex X
 * @param y1 Second vertex Y
 * @param x2 Third vertex X
 * @param y2 Third vertex Y
 * @param color Triangle color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_triangle(fmrb_gfx_context_t context, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, fmrb_color_t color);

/**
 * @brief Draw filled triangle (LovyanGFX compatible)
 * @param context Graphics context
 * @param x0 First vertex X
 * @param y0 First vertex Y
 * @param x1 Second vertex X
 * @param y1 Second vertex Y
 * @param x2 Third vertex X
 * @param y2 Third vertex Y
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_triangle(fmrb_gfx_context_t context, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, fmrb_color_t color);

/**
 * @brief Draw arc (LovyanGFX compatible)
 * @param context Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param r0 Inner radius
 * @param r1 Outer radius
 * @param angle0 Start angle (degrees)
 * @param angle1 End angle (degrees)
 * @param color Arc color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_arc(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1, fmrb_color_t color);

/**
 * @brief Draw filled arc (LovyanGFX compatible)
 * @param context Graphics context
 * @param x Center X coordinate
 * @param y Center Y coordinate
 * @param r0 Inner radius
 * @param r1 Outer radius
 * @param angle0 Start angle (degrees)
 * @param angle1 End angle (degrees)
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_arc(fmrb_gfx_context_t context, int32_t x, int32_t y, int32_t r0, int32_t r1, float angle0, float angle1, fmrb_color_t color);

/**
 * @brief Draw string (LovyanGFX compatible)
 * @param context Graphics context
 * @param str String to draw
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Text color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_string(fmrb_gfx_context_t context, const char *str, int32_t x, int32_t y, fmrb_color_t color);

/**
 * @brief Draw character (LovyanGFX compatible)
 * @param context Graphics context
 * @param c Character to draw
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Text color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_draw_char(fmrb_gfx_context_t context, char c, int32_t x, int32_t y, fmrb_color_t color);

/**
 * @brief Set text size (LovyanGFX compatible)
 * @param context Graphics context
 * @param size Text size multiplier
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_set_text_size(fmrb_gfx_context_t context, float size);

/**
 * @brief Set text color (LovyanGFX compatible)
 * @param context Graphics context
 * @param fg Foreground color
 * @param bg Background color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_set_text_color(fmrb_gfx_context_t context, fmrb_color_t fg, fmrb_color_t bg);

/**
 * @brief Fill screen with color (LovyanGFX compatible)
 * @param context Graphics context
 * @param color Fill color
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_fill_screen(fmrb_gfx_context_t context, fmrb_color_t color);

#ifdef __cplusplus
}
#endif