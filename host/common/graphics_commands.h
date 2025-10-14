#ifndef FMRB_HOST_GRAPHICS_COMMANDS_H
#define FMRB_HOST_GRAPHICS_COMMANDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Graphics command types (based on IPC_spec.md)
typedef enum {
    // Basic drawing
    FMRB_GFX_CMD_DRAW_PIXEL = 0x10,
    FMRB_GFX_CMD_DRAW_LINE = 0x11,
    FMRB_GFX_CMD_DRAW_FAST_VLINE = 0x12,
    FMRB_GFX_CMD_DRAW_FAST_HLINE = 0x13,

    FMRB_GFX_CMD_DRAW_RECT = 0x14,
    FMRB_GFX_CMD_FILL_RECT = 0x15,
    FMRB_GFX_CMD_DRAW_ROUND_RECT = 0x16,
    FMRB_GFX_CMD_FILL_ROUND_RECT = 0x17,

    FMRB_GFX_CMD_DRAW_CIRCLE = 0x18,
    FMRB_GFX_CMD_FILL_CIRCLE = 0x19,
    FMRB_GFX_CMD_DRAW_ELLIPSE = 0x1A,
    FMRB_GFX_CMD_FILL_ELLIPSE = 0x1B,

    FMRB_GFX_CMD_DRAW_TRIANGLE = 0x1C,
    FMRB_GFX_CMD_FILL_TRIANGLE = 0x1D,

    FMRB_GFX_CMD_DRAW_ARC = 0x1E,
    FMRB_GFX_CMD_FILL_ARC = 0x1F,

    // Text drawing
    FMRB_GFX_CMD_DRAW_STRING = 0x20,
    FMRB_GFX_CMD_DRAW_CHAR = 0x21,
    FMRB_GFX_CMD_SET_TEXT_SIZE = 0x22,
    FMRB_GFX_CMD_SET_TEXT_COLOR = 0x23,

    // Clear and fill
    FMRB_GFX_CMD_CLEAR = 0x30,
    FMRB_GFX_CMD_FILL_SCREEN = 0x31,

    // Image/bitmap drawing
    FMRB_GFX_CMD_DRAW_IMAGE = 0x40,
    FMRB_GFX_CMD_DRAW_BITMAP = 0x41,

    // Legacy
    FMRB_GFX_CMD_DRAW_TEXT = 0x06,
    FMRB_GFX_CMD_PRESENT = 0x08
} fmrb_gfx_cmd_type_t;

// Color format (RGB565)
typedef uint32_t fmrb_color_t;

// Color constants
#define FMRB_COLOR_BLACK   0x0000
#define FMRB_COLOR_WHITE   0xFFFF
#define FMRB_COLOR_RED     0xF800
#define FMRB_COLOR_GREEN   0x07E0
#define FMRB_COLOR_BLUE    0x001F
#define FMRB_COLOR_YELLOW  0xFFE0
#define FMRB_COLOR_CYAN    0x07FF
#define FMRB_COLOR_MAGENTA 0xF81F

// Graphics command structures
typedef struct {
    uint8_t cmd_type;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_clear_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_pixel_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x1, y1, x2, y2;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_line_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y, width, height;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_rect_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y;
    fmrb_color_t color;
    uint8_t font_size;
    uint16_t text_len;
    // text data follows
} __attribute__((packed)) fmrb_gfx_text_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y, width, height;
    uint16_t bitmap_size;
    // bitmap data follows
} __attribute__((packed)) fmrb_gfx_bitmap_cmd_t;

typedef struct {
    uint8_t cmd_type;
} __attribute__((packed)) fmrb_gfx_present_cmd_t;

// Additional command structures for LovyanGFX API
typedef struct {
    uint8_t cmd_type;
    int16_t x, y;
    int16_t h;  // height
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_vline_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y;
    int16_t w;  // width
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_hline_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y, width, height;
    int16_t radius;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_round_rect_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y;
    int16_t radius;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_circle_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x, y;
    int16_t rx, ry;  // radius x, y
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_ellipse_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int16_t x0, y0;
    int16_t x1, y1;
    int16_t x2, y2;
    fmrb_color_t color;
} __attribute__((packed)) fmrb_gfx_triangle_cmd_t;

typedef struct {
    uint8_t cmd_type;
    int32_t x, y;
    uint32_t color;
    uint16_t text_len;
    // text data follows
} __attribute__((packed)) fmrb_gfx_string_cmd_t;

// Screen configuration
#define FMRB_SCREEN_WIDTH  640
#define FMRB_SCREEN_HEIGHT 480
#define FMRB_SCREEN_BPP    16

#ifdef __cplusplus
}
#endif

#endif // FMRB_HOST_GRAPHICS_COMMANDS_H