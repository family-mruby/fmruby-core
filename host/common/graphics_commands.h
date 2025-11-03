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

    // Canvas management (LovyanGFX sprite-based)
    FMRB_GFX_CMD_CREATE_CANVAS = 0x50,
    FMRB_GFX_CMD_DELETE_CANVAS = 0x51,
    FMRB_GFX_CMD_SET_TARGET = 0x52,
    FMRB_GFX_CMD_PUSH_CANVAS = 0x53,

    // Legacy
    FMRB_GFX_CMD_DRAW_TEXT = 0x06,
    FMRB_GFX_CMD_PRESENT = 0x08
} fmrb_gfx_cmd_type_t;

// Color format (RGB332: 8-bit color)
typedef uint8_t fmrb_color_t;

// Canvas handle type (0 = main screen, 1-65534 = canvas ID)
typedef uint16_t fmrb_canvas_handle_t;
#define FMRB_CANVAS_SCREEN 0
#define FMRB_CANVAS_INVALID 0xFFFF

// Color constants (RGB332 format)
#define FMRB_COLOR_BLACK   0x00  // R=0, G=0, B=0
#define FMRB_COLOR_WHITE   0xFF  // R=7, G=7, B=3
#define FMRB_COLOR_RED     0xE0  // R=7, G=0, B=0
#define FMRB_COLOR_GREEN   0x1C  // R=0, G=7, B=0
#define FMRB_COLOR_BLUE    0x03  // R=0, G=0, B=3
#define FMRB_COLOR_YELLOW  0xFC  // R=7, G=7, B=0
#define FMRB_COLOR_CYAN    0x1F  // R=0, G=7, B=3
#define FMRB_COLOR_MAGENTA 0xE3  // R=7, G=0, B=3

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

// Canvas management command structures
typedef struct {
    uint8_t cmd_type;
    uint16_t canvas_id;
    int32_t width, height;
} __attribute__((packed)) fmrb_gfx_create_canvas_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint16_t canvas_id;
} __attribute__((packed)) fmrb_gfx_delete_canvas_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint16_t target_id;  // 0=screen, other=canvas ID
} __attribute__((packed)) fmrb_gfx_set_target_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint16_t canvas_id;
    uint16_t dest_canvas_id;  // 0=screen, other=canvas ID
    int32_t x, y;
    uint8_t transparent_color;
    uint8_t use_transparency;  // 0=no, 1=yes
} __attribute__((packed)) fmrb_gfx_push_canvas_cmd_t;

// Screen configuration
#define FMRB_SCREEN_WIDTH  640
#define FMRB_SCREEN_HEIGHT 480
#define FMRB_SCREEN_BPP    16

#ifdef __cplusplus
}
#endif

#endif // FMRB_HOST_GRAPHICS_COMMANDS_H