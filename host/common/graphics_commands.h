#ifndef FMRB_HOST_GRAPHICS_COMMANDS_H
#define FMRB_HOST_GRAPHICS_COMMANDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Graphics command types
typedef enum {
    FMRB_GFX_CMD_CLEAR = 0x01,
    FMRB_GFX_CMD_DRAW_PIXEL = 0x02,
    FMRB_GFX_CMD_DRAW_LINE = 0x03,
    FMRB_GFX_CMD_DRAW_RECT = 0x04,
    FMRB_GFX_CMD_FILL_RECT = 0x05,
    FMRB_GFX_CMD_DRAW_TEXT = 0x06,
    FMRB_GFX_CMD_DRAW_BITMAP = 0x07,
    FMRB_GFX_CMD_PRESENT = 0x08
} fmrb_gfx_cmd_type_t;

// Color format (RGB565)
typedef uint16_t fmrb_color_t;

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

// Screen configuration
#define FMRB_SCREEN_WIDTH  640
#define FMRB_SCREEN_HEIGHT 480
#define FMRB_SCREEN_BPP    16

#ifdef __cplusplus
}
#endif

#endif // FMRB_HOST_GRAPHICS_COMMANDS_H