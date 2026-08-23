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
    // Write the result of the last present to a file on the display
    // side's own filesystem. Handled synchronously there (encode and
    // write finish before the next command), so a command stream of
    // present + EXPORT_FRAME per slide saves exactly those pictures.
    FMRB_LINK_GFX_EXPORT_FRAME = 0x33,

    // Image/bitmap drawing
    FMRB_LINK_GFX_DRAW_IMAGE = 0x40,
    FMRB_LINK_GFX_DRAW_BITMAP = 0x41,

    // Canvas management (LovyanGFX sprite-based)
    FMRB_LINK_GFX_CREATE_CANVAS = 0x50,
    FMRB_LINK_GFX_DELETE_CANVAS = 0x51,
    FMRB_LINK_GFX_SET_TARGET = 0x52,
    FMRB_LINK_GFX_PUSH_CANVAS = 0x53,
    FMRB_LINK_GFX_SET_CANVAS_VISIBLE = 0x54,
    FMRB_LINK_GFX_GET_PIXEL = 0x55,
    FMRB_LINK_GFX_SET_COMPOSITE_REGIONS = 0x56,
    FMRB_LINK_GFX_SET_CANVAS_VIEWPORT = 0x57,
    FMRB_LINK_GFX_SET_SPRITE_CLIP = 0x58,

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
    // 1bpp mask + masked image blit (uses SpriteImage as the image source).
    // Mask upload is streamed: CREATE_MASK reserves a slot, then MASK_DATA
    // chunks fill the buffer at byte offsets (each chunk fits in a single
    // SPI frame, modeled on file_transfer's BEGIN/DATA flow).
    FMRB_LINK_GFX_CREATE_MASK = 0x84,         // sync; returns mask_id (reserves buffer)
    FMRB_LINK_GFX_DELETE_MASK = 0x85,         // async
    FMRB_LINK_GFX_DRAW_IMAGE_MASKED = 0x86,   // async
    FMRB_LINK_GFX_MASK_DATA = 0x87,           // async; appends chunk to a reserved mask
    FMRB_LINK_GFX_CREATE_SPRITE_INSTANCE = 0x88,
    FMRB_LINK_GFX_DELETE_SPRITE_INSTANCE = 0x89,
    FMRB_LINK_GFX_SPRITE_INSTANCE_MOVE = 0x8A,
    FMRB_LINK_GFX_SPRITE_INSTANCE_SET_VISIBLE = 0x8B,
    FMRB_LINK_GFX_SPRITE_INSTANCE_SET_FRAME = 0x8C,
    // Stamp a sub-region of a SpriteImage onto a canvas. No SpriteInstance is
    // allocated. Source pixels equal to the SpriteImage's transparent_color
    // (when use_transparent is set) are skipped. Designed for stateless BG
    // tile rendering.
    FMRB_LINK_GFX_DRAW_TILE = 0x8D,           // async
    FMRB_LINK_GFX_DELETE_ALL_SPRITES = 0x8F,

    // GfxBlock VM (draw-batch programs)
    FMRB_LINK_GFX_DEFINE_PROG = 0x90,
    FMRB_LINK_GFX_EXEC_PROG = 0x91,
    FMRB_LINK_GFX_DELETE_PROG = 0x92,

    // Motion-JPEG playback into a canvas. Modern/P4 only: it needs the SoC's
    // JPEG decoder and a filesystem the display side can read. The Retro
    // backend answers these with an error, it never grows an implementation.
    FMRB_LINK_GFX_VIDEO_OPEN = 0xA0,      // sync; returns video_opened
    FMRB_LINK_GFX_VIDEO_CONTROL = 0xA1,   // sync; returns video_status
    FMRB_LINK_GFX_VIDEO_STATUS = 0xA2     // sync; returns video_status
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

// Composite regions: per-canvas sub-rect compositing.
// When count > 0, the compositor copies only the listed regions (each with its
// own transparent/opaque mode) instead of pushing the whole active area.
// count = 0 clears regions and restores the full-area pushSprite fallback.
#define FMRB_LINK_MAX_COMPOSITE_REGIONS 8

typedef struct __attribute__((packed)) {
    int16_t src_x;            // Source rect top-left within the canvas render buffer
    int16_t src_y;
    int16_t dst_x;            // Destination offset relative to canvas push_x/push_y
    int16_t dst_y;
    int16_t w;                // Region width / height (pixels)
    int16_t h;
    uint8_t use_transparent;  // 1 = per-pixel color-key compare, 0 = opaque memcpy
    uint8_t _pad;
} fmrb_link_graphics_composite_region_t;  // 14 bytes

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint8_t count;            // 0 = clear, otherwise number of valid regions
    uint8_t _pad;
    fmrb_link_graphics_composite_region_t regions[FMRB_LINK_MAX_COMPOSITE_REGIONS];
} fmrb_link_graphics_set_composite_regions_t;

// ---- Motion-JPEG playback (Modern/P4 only) ----------------------------------
// VIDEO_OPEN hands the display side a file of concatenated JPEG frames, the
// canvas to play it into, and where inside that canvas the picture goes. Each
// decoded frame is written into that rect and the canvas is committed, so
// whatever the app drew elsewhere on the canvas (its window frame, controls)
// survives. The app must not draw *inside* the rect while playback runs.
// One player exists at a time; opening again replaces it.

typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    int16_t  x, y;        // top-left of the picture, canvas coordinates
    uint16_t fps;         // frames per second (1..60)
    uint8_t  flags;       // bit 0: loop back to the first frame at the end
    uint16_t path_len;
    // Followed by path string (path_len bytes, no null terminator required)
} fmrb_link_graphics_video_open_t;

#define FMRB_LINK_GFX_VIDEO_FLAG_LOOP 0x01

typedef struct __attribute__((packed)) {
    uint8_t  result;      // 0 = opened, non-zero = fmrb_err_t-like failure
    uint16_t width, height;
} fmrb_link_graphics_video_opened_t;

typedef enum {
    FMRB_LINK_GFX_VIDEO_ACTION_PLAY = 0,
    FMRB_LINK_GFX_VIDEO_ACTION_PAUSE = 1,
    FMRB_LINK_GFX_VIDEO_ACTION_STOP = 2,   // stops and releases the file
    FMRB_LINK_GFX_VIDEO_ACTION_REWIND = 3  // back to the first frame
} fmrb_link_graphics_video_action_t;

typedef struct __attribute__((packed)) {
    uint8_t action;       // fmrb_link_graphics_video_action_t
} fmrb_link_graphics_video_control_t;

typedef enum {
    FMRB_LINK_GFX_VIDEO_STATE_IDLE = 0,
    FMRB_LINK_GFX_VIDEO_STATE_PLAYING = 1,
    FMRB_LINK_GFX_VIDEO_STATE_PAUSED = 2,
    FMRB_LINK_GFX_VIDEO_STATE_FINISHED = 3
} fmrb_link_graphics_video_state_t;

typedef struct __attribute__((packed)) {
    uint8_t  state;           // fmrb_link_graphics_video_state_t
    uint16_t canvas_id;
    uint32_t frames_shown;
    uint32_t frames_dropped;  // decoded late and skipped to keep the clock
} fmrb_link_graphics_video_status_t;

// SET_CANVAS_VIEWPORT: composite only the (src_x, src_y, view_w, view_h)
// sub-rect of the canvas at its push position instead of the full buffer.
// The canvas is addressed as a torus: the source rect wraps around the
// canvas edges, so a ring-buffer canvas barely larger than the viewport can
// scroll an arbitrarily large world while the app stamps newly exposed
// tiles on the hidden side. view_w == 0 clears the viewport (full-canvas
// composite, the default). view_w/view_h are clamped to the canvas size.
// Supported by the Modern (P4/PPA) backend only; apps must gate on
// FmrbConst::CHIP_MODEL.
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t src_x, src_y;
    uint16_t view_w, view_h;
} fmrb_link_graphics_set_canvas_viewport_t;

// SET_SPRITE_CLIP: confine this canvas's sprite compositing to a sub-rect.
// Sprites are composited on top of everything the canvas itself drew, so
// without a clip they paint over the window frame and title bar the app drew
// into the same canvas. The rect is in the same coordinate space as sprite
// instance positions (canvas-local; viewport-relative when a viewport is
// set), and is clamped to the canvas. w == 0 or h == 0 clears the clip
// (sprites bounded by the canvas only, the default).
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t x, y;
    uint16_t w, h;
} fmrb_link_graphics_set_sprite_clip_t;

// GET_PIXEL: read a single RGB332 pixel from a canvas back buffer.
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;  // 0 = screen, others = back buffer of that canvas
    int16_t x, y;
} fmrb_link_graphics_get_pixel_t;

typedef struct __attribute__((packed)) {
    uint8_t color;   // RGB332 pixel value (0 when status != 0)
    uint8_t status;  // 0 = success, 1 = out of range, 0xFF = error
} fmrb_link_graphics_pixel_value_t;

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

// EXPORT_FRAME: same payload shape as LOAD_SPRITE_IMAGE_BMP, minus the image.
// The path is the display side's, not the core's: on Tab5 the two share one
// VFS, in the simulator they do not.
typedef struct __attribute__((packed)) {
    uint16_t path_len;
    // Followed by path string (path_len bytes)
} fmrb_link_graphics_export_frame_t;

// CREATE_MASK: reserve a 1bpp mask slot on the backend. Bit-packed,
// MSB-first per byte. Row stride = ceil(width / 8) bytes. The backend
// allocates a zero-filled buffer of that size; chunks are then sent via
// MASK_DATA. This decoupling keeps each SPI frame small (modeled on
// file_transfer BEGIN/DATA), avoiding the 256-byte MessageBuffer cap on
// reassembled chunked messages.
//
// canvas_id binds the mask's lifetime to the canvas: when the canvas is
// deleted (or the app exits), masks with the matching canvas_id are
// freed automatically. This matches how SpriteImage lifetimes work.
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t width, height;
} fmrb_link_graphics_create_mask_t;

typedef struct __attribute__((packed)) {
    uint16_t mask_id;  // 0 = error (pool full / alloc failed / bad payload)
} fmrb_link_graphics_mask_created_t;

// MASK_DATA: append a chunk into a previously reserved mask buffer.
// offset is in bytes from the start of the mask data. chunk_len bytes
// follow this header.
typedef struct __attribute__((packed)) {
    uint16_t mask_id;
    uint32_t offset;
    uint16_t chunk_len;
    // Followed by chunk_len bytes of mask data.
} fmrb_link_graphics_mask_data_t;

typedef struct __attribute__((packed)) {
    uint16_t mask_id;
} fmrb_link_graphics_delete_mask_t;

// DRAW_IMAGE_MASKED: blit a SpriteImage onto a canvas using a 1bpp mask.
// Mask dimensions drive the per-pixel loop; pixels are sampled from the
// source sprite at the same local (xx, yy) and written at (x+xx, y+yy)
// only when the mask bit is set.
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t image_id;   // SpriteImage ID (source)
    uint16_t mask_id;
    int16_t x, y;        // destination top-left on canvas (local coords)
} fmrb_link_graphics_draw_image_masked_t;

// DRAW_TILE: stamp the sub-region (src_x, src_y, w, h) of a SpriteImage onto
// a canvas at (dst_x, dst_y). Honors the source SpriteImage's transparent
// color. No SpriteInstance is allocated.
typedef struct __attribute__((packed)) {
    uint16_t canvas_id;
    uint16_t image_id;   // SpriteImage ID (source / tilesheet)
    int16_t  src_x, src_y;
    uint16_t w, h;
    int16_t  dst_x, dst_y;
} fmrb_link_graphics_draw_tile_t;

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
#define FMRB_LINK_FILE_TRANSFER_RMDIR   0x06

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

// RMDIR: recursively delete everything under path. The handler restricts the
// allowed root (e.g. "/flash/cache") so this cannot be aimed at the bytecode
// or config partitions. ACK payload carries the number of removed entries.
typedef struct __attribute__((packed)) {
    uint16_t path_len;
    // Followed by path string (path_len bytes)
} fmrb_link_file_transfer_rmdir_t;

typedef struct __attribute__((packed)) {
    uint32_t deleted_count;  // Files and directories removed
    uint8_t  status;         // 0=success, 1=path rejected, 2=fs error
} fmrb_link_file_transfer_rmdir_resp_t;

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
