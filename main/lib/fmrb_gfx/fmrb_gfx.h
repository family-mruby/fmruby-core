#ifndef FMRB_GFX_H
#define FMRB_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Color type (RGBA8888)
typedef uint32_t fmrb_color_t;

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

// Color constants
#define FMRB_COLOR_BLACK    0xFF000000
#define FMRB_COLOR_WHITE    0xFFFFFFFF
#define FMRB_COLOR_RED      0xFFFF0000
#define FMRB_COLOR_GREEN    0xFF00FF00
#define FMRB_COLOR_BLUE     0xFF0000FF
#define FMRB_COLOR_YELLOW   0xFFFFFF00
#define FMRB_COLOR_CYAN     0xFF00FFFF
#define FMRB_COLOR_MAGENTA  0xFFFF00FF
#define FMRB_COLOR_GRAY     0xFF808080

// Utility macros for color manipulation
#define FMRB_COLOR_RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define FMRB_COLOR_RGB(r, g, b) FMRB_COLOR_RGBA(r, g, b, 255)
#define FMRB_COLOR_GET_A(c) (((c) >> 24) & 0xFF)
#define FMRB_COLOR_GET_R(c) (((c) >> 16) & 0xFF)
#define FMRB_COLOR_GET_G(c) (((c) >> 8) & 0xFF)
#define FMRB_COLOR_GET_B(c) ((c) & 0xFF)

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

#ifdef __cplusplus
}
#endif

#endif // FMRB_GFX_H