#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Link protocol version is defined in fmrb.h (FMRB_LINK_VERSION)

// Link frame size constants (single source of truth)
// Change FMRB_LINK_FRAME_SIZE to resize frames (e.g. 1024); all derived values follow.
#define FMRB_LINK_FRAME_SIZE          256
#define FMRB_LINK_FRAME_HEADER_SIZE   6   // magic(1) + seq(1) + ack_seq(1) + status(1) + data_len(2)
#define FMRB_LINK_FRAME_CRC_SIZE      2
#define FMRB_LINK_FRAME_MAX_DATA      (FMRB_LINK_FRAME_SIZE - FMRB_LINK_FRAME_HEADER_SIZE - FMRB_LINK_FRAME_CRC_SIZE)

// Message type byte layout:
//   bit 7:   CHUNKED flag
//   bit 6:   ACK_REQUIRED flag
//   bit 5:   (reserved)
//   bit 4-0: Type (sequential, 0-31)
#define FMRB_LINK_TYPE_MASK  0x1F
#define FMRB_LINK_FLAG_MASK  0xE0

typedef enum {
    // Types (bit 4-0, sequential)
    FMRB_LINK_TYPE_EMPTY = 0,
    FMRB_LINK_TYPE_CONTROL = 1,
    FMRB_LINK_TYPE_GRAPHICS = 2,
    FMRB_LINK_TYPE_AUDIO = 3,
    FMRB_LINK_TYPE_FILE_TRANSFER = 4,
    FMRB_LINK_TYPE_INPUT = 5,

    // Flags (bit 7-6)
    FMRB_LINK_FLAG_ACK_REQUIRED = 0x40,
    FMRB_LINK_FLAG_CHUNKED = 0x80
} fmrb_link_type_t;

//---------------------------
// Sub Command
//---------------------------
#define FMRB_LINK_CONTROL_VERSION      0x01
#define FMRB_LINK_CONTROL_INIT_DISPLAY 0x02
#define FMRB_LINK_CONTROL_SET_TIME     0x03
#define FMRB_LINK_CONTROL_GA_VERSION   0x04

// Max length of GA firmware version string including NUL
#define FMRB_GA_VERSION_MAX_LEN        16

// Control command structures
typedef struct __attribute__((packed)) {
    uint8_t version;  // Protocol version number
} fmrb_control_version_req_t;

typedef struct __attribute__((packed)) {
    uint8_t version;  // Protocol version number
} fmrb_control_version_resp_t;

typedef struct __attribute__((packed)) {
    char version[FMRB_GA_VERSION_MAX_LEN];  // NUL-terminated GA firmware version
} fmrb_control_ga_version_resp_t;

typedef struct __attribute__((packed)) {
    uint16_t width;
    uint16_t height;
    uint8_t color_depth;     // 8 for RGB332
    uint8_t margin_x;       // Horizontal margin (left+right) in pixels
    uint8_t margin_y;       // Vertical margin (top+bottom) in pixels
} fmrb_control_init_display_t;

typedef struct __attribute__((packed)) {
    int64_t tv_sec;          // Seconds since epoch (Unix time)
    int32_t tv_usec;         // Microseconds
    char    tz[32];          // POSIX TZ string (NUL-terminated; empty = leave TZ unchanged)
} fmrb_control_set_time_t;

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
    FMRB_LINK_GFX_SET_FONT = 0x24,

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
    FMRB_LINK_GFX_SET_CANVAS_VISIBLE = 0x54,

    // Cursor control (global resource, no canvas_id)
    FMRB_LINK_GFX_CURSOR_SET_POSITION = 0x60,
    FMRB_LINK_GFX_CURSOR_SET_VISIBLE = 0x61,

    // Pixel blending
    FMRB_LINK_GFX_BLEND_RECT = 0x68,

    // Display output control (CVBS/NTSC)
    FMRB_LINK_GFX_SET_OUTPUT_LEVEL = 0x70,
    FMRB_LINK_GFX_SET_CHROMA_LEVEL = 0x71,

    // Sprite management
    FMRB_LINK_GFX_CREATE_SPRITE_IMAGE = 0x80,
    FMRB_LINK_GFX_DELETE_SPRITE_IMAGE = 0x81,
    FMRB_LINK_GFX_SET_SPRITE_IMAGE_TARGET = 0x82,
    FMRB_LINK_GFX_LOAD_SPRITE_IMAGE_BMP = 0x83,
    FMRB_LINK_GFX_CREATE_SPRITE_INSTANCE = 0x88,
    FMRB_LINK_GFX_DELETE_SPRITE_INSTANCE = 0x89,
    FMRB_LINK_GFX_SPRITE_INSTANCE_MOVE = 0x8A,
    FMRB_LINK_GFX_SPRITE_INSTANCE_SET_VISIBLE = 0x8B,
    FMRB_LINK_GFX_SPRITE_INSTANCE_SET_FRAME = 0x8C,
    FMRB_LINK_GFX_DELETE_ALL_SPRITES = 0x8F,

    // GfxBlock VM (draw-batch programs)
    FMRB_LINK_GFX_DEFINE_PROG = 0x90,
    FMRB_LINK_GFX_EXEC_PROG = 0x91,
    FMRB_LINK_GFX_DELETE_PROG = 0x92
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

// Blend modes for BLEND_RECT
#define FMRB_BLEND_MODE_ADD  0  // Per-component saturating add
#define FMRB_BLEND_MODE_XOR  1  // Per-pixel XOR

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    uint16_t x, y;
    uint16_t width, height;
    uint8_t color;  // RGB332 color operand
    uint8_t mode;   // FMRB_BLEND_MODE_*
} fmrb_link_graphics_blend_rect_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // Target canvas ID (0=screen)
    int32_t x, y;
    uint8_t color;  // RGB332 format (foreground)
    uint8_t bg_color;  // RGB332 format (background)
    uint8_t bg_transparent;  // 1 = transparent (no background), 0 = use bg_color
    uint8_t hybrid_mode;  // 0 = use current font; 1 = ASCII -> Font0, multi-byte UTF-8 -> misaki_8
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

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    int16_t x, y;
    int16_t r0, r1;         // inner and outer radius
    int16_t angle0, angle1; // start and end angle in degrees (0-360)
    uint8_t color;
} fmrb_link_graphics_arc_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint8_t size;  // text size multiplier (1-4)
} fmrb_link_graphics_text_size_t;

// Font family identifiers for fmrb_link_graphics_set_font_t
#define FMRB_LINK_GFX_FONT_FAMILY_DEFAULT 0  // LovyanGFX Font0 (6x8 ASCII)
#define FMRB_LINK_GFX_FONT_FAMILY_JA      1  // efontJA_* (UTF-8, Japanese)

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint8_t family;  // FMRB_LINK_GFX_FONT_FAMILY_*
    uint8_t size;    // Pixel height (used for JA: 12 currently supported)
} fmrb_link_graphics_set_font_t;

// Canvas management structures
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    int32_t width, height;
    int16_t z_order;
    uint8_t use_transparent;    // 1=enable color-key transparency during composition
    uint8_t transparent_color;  // RGB332 color treated as transparent
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

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint8_t visible;  // 0=hidden, 1=visible
} fmrb_link_graphics_set_canvas_visible_t;

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

// Image management structures
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t path_len;
    // Followed by path string (path_len bytes, no null terminator required)
} fmrb_link_graphics_create_image_from_file_t;

typedef struct __attribute__((packed)) {
    uint16_t image_id;    // Allocated image ID (0 = error)
    uint16_t width;       // Decoded image width
    uint16_t height;      // Decoded image height
} fmrb_link_graphics_image_created_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t image_id;
    int16_t x, y;
    uint8_t flags;        // Bit 0: fade_in (reserved for future use)
    int16_t scale_x_fp8;  // Fixed-point scale (x256): 256 = 1.0, 512 = 2.0, 128 = 0.5
    int16_t scale_y_fp8;  // 0 = same as scale_x
} fmrb_link_graphics_draw_image_t;

typedef struct __attribute__((packed)) {
    uint16_t image_id;
} fmrb_link_graphics_delete_image_t;

// Sprite management structures
#define FMRB_SPRITE_MAX_FRAMES 8

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;       // Parent window
    uint16_t width, height;
    uint8_t transparent_color;
    uint8_t use_transparent;  // 0=no, 1=yes
} fmrb_link_graphics_create_sprite_image_t;

typedef struct __attribute__((packed)) {
    uint16_t image_id;        // Allocated sprite image ID (0 = error)
} fmrb_link_graphics_sprite_image_created_t;

typedef struct __attribute__((packed)) {
    uint16_t image_id;
} fmrb_link_graphics_delete_sprite_image_t;

typedef struct __attribute__((packed)) {
    uint16_t image_id;        // 0 = reset to canvas target
} fmrb_link_graphics_set_sprite_image_target_t;

typedef struct __attribute__((packed)) {
    uint16_t image_id;        // Target sprite image (must already be created)
    uint16_t path_len;
    // Followed by path string (path_len bytes)
} fmrb_link_graphics_load_sprite_image_bmp_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;       // Parent window
    uint8_t frame_count;      // Number of image_ids (1..FMRB_SPRITE_MAX_FRAMES)
    uint16_t image_ids[FMRB_SPRITE_MAX_FRAMES];
    int16_t x, y;             // Window-local position
    int16_t z_order;
} fmrb_link_graphics_create_sprite_instance_t;

typedef struct __attribute__((packed)) {
    uint16_t instance_id;     // Allocated instance ID (0 = error)
} fmrb_link_graphics_sprite_instance_created_t;

typedef struct __attribute__((packed)) {
    uint16_t instance_id;
} fmrb_link_graphics_delete_sprite_instance_t;

typedef struct __attribute__((packed)) {
    uint16_t instance_id;
    int16_t x, y;
} fmrb_link_graphics_sprite_instance_move_t;

typedef struct __attribute__((packed)) {
    uint16_t instance_id;
    uint8_t visible;          // 0=hidden, 1=visible
} fmrb_link_graphics_sprite_instance_set_visible_t;

typedef struct __attribute__((packed)) {
    uint16_t instance_id;
    uint8_t frame_index;
} fmrb_link_graphics_sprite_instance_set_frame_t;

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;       // Delete all sprites belonging to this window
} fmrb_link_graphics_delete_all_sprites_t;

// GfxBlock VM message structures
// DEFINE_PROG is synchronous; ACK payload contains one uint8_t prog_id
// (FMRB_GFX_VM_INVALID_PROG_ID = pool full or invalid request).
#define FMRB_GFX_VM_INVALID_PROG_ID 0xFF

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t bytecode_len;
    uint16_t strtable_len;
    // Followed by: bytecode[bytecode_len] + strtable[strtable_len]
} fmrb_link_graphics_define_prog_t;

// EXEC_PROG is asynchronous.
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint8_t prog_id;
    uint8_t reg_count;
    // Followed by: [uint8_t reg_id, int16_t value] * reg_count
} fmrb_link_graphics_exec_prog_t;

// DELETE_PROG is asynchronous. canvas_id is tracked on the WROVER side.
typedef struct __attribute__((packed)) {
    uint8_t prog_id;
} fmrb_link_graphics_delete_prog_t;

// File transfer sub-commands
#define FMRB_LINK_FILE_TRANSFER_BEGIN   0x01
#define FMRB_LINK_FILE_TRANSFER_DATA    0x02
#define FMRB_LINK_FILE_TRANSFER_END     0x03
#define FMRB_LINK_FILE_TRANSFER_STATUS  0x04
#define FMRB_LINK_FILE_TRANSFER_DELETE  0x05

// File transfer message structures
typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint16_t path_len;
    // Followed by path string (path_len bytes)
} fmrb_link_file_transfer_begin_t;

typedef struct __attribute__((packed)) {
    uint32_t offset;
    uint16_t chunk_len;
    // Followed by chunk data (chunk_len bytes)
} fmrb_link_file_transfer_data_t;

typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t checksum;       // CRC32
} fmrb_link_file_transfer_end_t;

typedef struct __attribute__((packed)) {
    uint16_t path_len;
    // Followed by path string (path_len bytes)
} fmrb_link_file_transfer_status_t;

typedef struct __attribute__((packed)) {
    uint8_t exists;          // 0=not found, 1=exists
    uint32_t file_size;      // File size (0 if not exists)
    uint32_t checksum;       // CRC32 of existing file (0 if not exists)
} fmrb_link_file_transfer_status_resp_t;

typedef struct __attribute__((packed)) {
    uint16_t path_len;
    // Followed by path string (path_len bytes)
} fmrb_link_file_transfer_delete_t;

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
