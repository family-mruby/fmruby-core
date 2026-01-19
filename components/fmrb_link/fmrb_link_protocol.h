#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include "fmrb_link_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Protocol version
#define FMRB_LINK_PROTOCOL_VERSION 1

// Message types (based on IPC_spec.md)
typedef enum {
    FMRB_LINK_TYPE_CONTROL = 1,
    FMRB_LINK_TYPE_GRAPHICS = 2,
    FMRB_LINK_TYPE_AUDIO = 4,
    FMRB_LINK_TYPE_INPUT = 128,  // Linux only

    // Flags
    FMRB_LINK_FLAG_ACK_REQUIRED = 32,
    FMRB_LINK_FLAG_CHUNKED = 64
} fmrb_link_type_t;

//---------------------------
// Sub Command
//---------------------------
#define FMRB_LINK_CONTROL_VERSION      0x01
#define FMRB_LINK_CONTROL_INIT_DISPLAY 0x02

// Control command structures
typedef struct __attribute__((packed)) {
    uint8_t version;  // Protocol version number
} fmrb_control_version_req_t;

typedef struct __attribute__((packed)) {
    uint8_t version;  // Protocol version number
} fmrb_control_version_resp_t;

typedef struct __attribute__((packed)) {
    uint16_t width;
    uint16_t height;
    uint8_t color_depth;     // 8 for RGB332
} fmrb_control_init_display_t;

// Protocol response codes
#define FMRB_LINK_RESPONSE_MSG_ACK     0xF0
#define FMRB_LINK_RESPONSE_MSG_NACK    0xF1

// Graphics sub-commands (LovyanGFX API in snake_case)
typedef enum {
    // Window management
    FMRB_LINK_GFX_CREATE_WINDOW = 0x01,
    FMRB_LINK_GFX_SET_WINDOW_ORDER = 0x02,
    FMRB_LINK_GFX_SET_WINDOW_PREF = 0x03,
    FMRB_LINK_GFX_REFRESH_ALL_WINDOWS = 0x04,
    FMRB_LINK_GFX_UPDATE_WINDOW = 0x05,

    // Image management
    FMRB_LINK_GFX_CREATE_IMAGE_FROM_MEM = 0x06,
    FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE = 0x07,
    FMRB_LINK_GFX_DELETE_IMAGE = 0x08,

    // Basic drawing (LovyanGFX compatible)
    FMRB_LINK_GFX_DRAW_PIXEL = 0x10,
    FMRB_LINK_GFX_DRAW_LINE = 0x11,
    FMRB_LINK_GFX_DRAW_FAST_VLINE = 0x12,
    FMRB_LINK_GFX_DRAW_FAST_HLINE = 0x13,

    FMRB_LINK_GFX_DRAW_RECT = 0x14,
    FMRB_LINK_GFX_FILL_RECT = 0x15,
    FMRB_LINK_GFX_DRAW_ROUND_RECT = 0x16,
    FMRB_LINK_GFX_FILL_ROUND_RECT = 0x17,

    FMRB_LINK_GFX_DRAW_CIRCLE = 0x18,
    FMRB_LINK_GFX_FILL_CIRCLE = 0x19,
    FMRB_LINK_GFX_DRAW_ELLIPSE = 0x1A,
    FMRB_LINK_GFX_FILL_ELLIPSE = 0x1B,

    FMRB_LINK_GFX_DRAW_TRIANGLE = 0x1C,
    FMRB_LINK_GFX_FILL_TRIANGLE = 0x1D,

    FMRB_LINK_GFX_DRAW_ARC = 0x1E,
    FMRB_LINK_GFX_FILL_ARC = 0x1F,

    // Text drawing
    FMRB_LINK_GFX_DRAW_STRING = 0x20,
    FMRB_LINK_GFX_DRAW_CHAR = 0x21,
    FMRB_LINK_GFX_SET_TEXT_SIZE = 0x22,
    FMRB_LINK_GFX_SET_TEXT_COLOR = 0x23,

    // Clear and fill
    FMRB_LINK_GFX_CLEAR = 0x30,
    FMRB_LINK_GFX_FILL_SCREEN = 0x31,
    FMRB_LINK_GFX_PRESENT = 0x32,

    // Image/bitmap drawing
    FMRB_LINK_GFX_DRAW_IMAGE = 0x40,
    FMRB_LINK_GFX_DRAW_BITMAP = 0x41,

    // Canvas management (LovyanGFX sprite-based)
    FMRB_LINK_GFX_CREATE_CANVAS = 0x50,
    FMRB_LINK_GFX_DELETE_CANVAS = 0x51,
    FMRB_LINK_GFX_SET_TARGET = 0x52,
    FMRB_LINK_GFX_PUSH_CANVAS = 0x53,

    // Cursor control (global resource, no canvas_id)
    FMRB_LINK_GFX_CURSOR_SET_POSITION = 0x60,
    FMRB_LINK_GFX_CURSOR_SET_VISIBLE = 0x61
} fmrb_link_graphics_cmd_t;

// Audio sub-commands
typedef enum {
    FMRB_LINK_MSG_AUDIO_PLAY = 0x20,
    FMRB_LINK_MSG_AUDIO_STOP = 0x21,
    FMRB_LINK_MSG_AUDIO_PAUSE = 0x22,
    FMRB_LINK_MSG_AUDIO_RESUME = 0x23,
    FMRB_LINK_MSG_AUDIO_SET_VOLUME = 0x24,
    FMRB_LINK_MSG_AUDIO_QUEUE_SAMPLES = 0x25
} fmrb_link_audio_cmd_t;

// Frame header (based on IPC_spec.md)
typedef struct __attribute__((packed)) {
    uint8_t type;    // Message type
    uint8_t seq;     // Sequence number
    uint16_t len;    // Payload bytes
} fmrb_link_frame_hdr_t;

// Chunk flags
typedef enum {
    FMRB_LINK_CHUNK_FL_START = 1 << 0,
    FMRB_LINK_CHUNK_FL_END = 1 << 1,
    FMRB_LINK_CHUNK_FL_ERR = 1 << 7
} fmrb_link_chunk_flags_t;

// Chunked header
typedef struct __attribute__((packed)) {
    uint8_t flags;       // Chunk flags
    uint8_t chunk_id;    // Chunk identifier
    uint16_t chunk_len;  // Chunk length
    uint32_t offset;     // Offset in total data
    uint32_t total_len;  // Total data length
} fmrb_link_chunk_info_t;

// Response header
typedef struct __attribute__((packed)) {
    uint8_t type;      // Message type
    uint8_t seq;       // Rolling counter
    uint16_t response; // 0: OK, others: Fail
} fmrb_link_frame_response_hdr_t;

// Chunk ACK
typedef struct __attribute__((packed)) {
    uint8_t chunk_id;     // Target lane
    uint8_t gen;          // Generation
    uint16_t credit;      // 0..window size (next concurrent request allowance)
    uint32_t next_offset; // Next offset to send
} fmrb_link_frame_chunk_ack_t;


// Graphics message structures (RGB332 color format)
// Note: cmd_type is sent separately via send_graphics_command()
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    uint16_t x, y;
    uint16_t width, height;
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_clear_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    uint16_t x, y;
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_pixel_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    uint16_t x1, y1;
    uint16_t x2, y2;
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_line_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    uint16_t x, y;
    uint16_t width, height;
    uint8_t color;  // RGB332 format
    bool filled;
} fmrb_link_graphics_rect_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    int32_t x, y;
    uint8_t color;  // RGB332 format (foreground)
    uint8_t bg_color;  // RGB332 format (background)
    uint8_t bg_transparent;  // 1 = transparent (no background), 0 = use bg_color
    uint16_t text_len;
    // Followed by text data
} fmrb_link_graphics_text_t;

// Additional shape structures (LovyanGFX compatible)
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    int16_t x, y;
    int16_t width, height;
    int16_t radius;
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_round_rect_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    int16_t x, y;
    int16_t radius;
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_circle_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    int16_t x, y;
    int16_t rx, ry;  // radius x, y
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_ellipse_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    int16_t x0, y0;
    int16_t x1, y1;
    int16_t x2, y2;
    uint8_t color;  // RGB332 format
} fmrb_link_graphics_triangle_t;

// Canvas management structures
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    int32_t width, height;
    int16_t z_order;
} fmrb_link_graphics_create_canvas_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
} fmrb_link_graphics_delete_canvas_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    int16_t z_order;
} fmrb_link_graphics_set_window_order_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    int32_t x, y;         // Position
    int32_t width, height; // Size
} fmrb_link_graphics_update_window_t;

typedef struct __attribute__((packed)) {
    uint16_t target_id;  // 0=screen, other=canvas ID
} fmrb_link_graphics_set_target_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t dest_canvas_id;  // 0=screen, other=canvas ID
    int32_t x, y;
    uint8_t transparent_color;
    uint8_t use_transparency;  // 0=no, 1=yes
} fmrb_link_graphics_push_canvas_t;

// Cursor control structures (no canvas_id - cursor is global)
typedef struct __attribute__((packed)) {
    int32_t x, y;
} fmrb_link_graphics_cursor_position_t;

typedef struct __attribute__((packed)) {
    bool visible;
} fmrb_link_graphics_cursor_visible_t;

// Present command structure
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Canvas to present (0=screen/back_buffer, other=canvas ID)
} fmrb_link_graphics_present_t;

// Audio message structures
typedef struct __attribute__((packed)) {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t data_len;
    // Followed by audio data
} fmrb_link_audio_play_t;

typedef struct __attribute__((packed)) {
    uint8_t volume; // 0-100
} fmrb_link_audio_volume_t;

// Response structures
typedef struct __attribute__((packed)) {
    uint16_t original_sequence;
    uint8_t status; // 0 = success, others = error codes
} fmrb_link_ack_t;

// Max payload size
#define FMRB_LINK_MAX_PAYLOAD_SIZE 4096

#ifdef __cplusplus
}
#endif
