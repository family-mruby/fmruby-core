#ifndef FMRB_IPC_PROTOCOL_H
#define FMRB_IPC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol version
#define FMRB_IPC_PROTOCOL_VERSION 1

// Message types (based on IPC_spec.md)
typedef enum {
    FMRB_IPC_TYPE_CONTROL = 1,
    FMRB_IPC_TYPE_GRAPHICS = 2,
    FMRB_IPC_TYPE_AUDIO = 4,
    FMRB_IPC_TYPE_INPUT = 128,  // Linux only

    // Flags
    FMRB_IPC_FLAG_ACK_REQUIRED = 32,
    FMRB_IPC_FLAG_CHUNKED = 64
} fmrb_ipc_type_t;

// Graphics sub-commands (LovyanGFX API in snake_case)
typedef enum {
    // Window management
    FMRB_IPC_GFX_CREATE_WINDOW = 0x01,
    FMRB_IPC_GFX_SET_WINDOW_ORDER = 0x02,
    FMRB_IPC_GFX_SET_WINDOW_PREF = 0x03,
    FMRB_IPC_GFX_REFRESH_ALL_WINDOWS = 0x04,
    FMRB_IPC_GFX_UPDATE_WINDOW = 0x05,

    // Image management
    FMRB_IPC_GFX_CREATE_IMAGE_FROM_MEM = 0x06,
    FMRB_IPC_GFX_CREATE_IMAGE_FROM_FILE = 0x07,
    FMRB_IPC_GFX_DELETE_IMAGE = 0x08,

    // Basic drawing (LovyanGFX compatible)
    FMRB_IPC_GFX_DRAW_PIXEL = 0x10,
    FMRB_IPC_GFX_DRAW_LINE = 0x11,
    FMRB_IPC_GFX_DRAW_FAST_VLINE = 0x12,
    FMRB_IPC_GFX_DRAW_FAST_HLINE = 0x13,

    FMRB_IPC_GFX_DRAW_RECT = 0x14,
    FMRB_IPC_GFX_FILL_RECT = 0x15,
    FMRB_IPC_GFX_DRAW_ROUND_RECT = 0x16,
    FMRB_IPC_GFX_FILL_ROUND_RECT = 0x17,

    FMRB_IPC_GFX_DRAW_CIRCLE = 0x18,
    FMRB_IPC_GFX_FILL_CIRCLE = 0x19,
    FMRB_IPC_GFX_DRAW_ELLIPSE = 0x1A,
    FMRB_IPC_GFX_FILL_ELLIPSE = 0x1B,

    FMRB_IPC_GFX_DRAW_TRIANGLE = 0x1C,
    FMRB_IPC_GFX_FILL_TRIANGLE = 0x1D,

    FMRB_IPC_GFX_DRAW_ARC = 0x1E,
    FMRB_IPC_GFX_FILL_ARC = 0x1F,

    // Text drawing
    FMRB_IPC_GFX_DRAW_STRING = 0x20,
    FMRB_IPC_GFX_DRAW_CHAR = 0x21,
    FMRB_IPC_GFX_SET_TEXT_SIZE = 0x22,
    FMRB_IPC_GFX_SET_TEXT_COLOR = 0x23,

    // Clear and fill
    FMRB_IPC_GFX_CLEAR = 0x30,
    FMRB_IPC_GFX_FILL_SCREEN = 0x31,

    // Image/bitmap drawing
    FMRB_IPC_GFX_DRAW_IMAGE = 0x40,
    FMRB_IPC_GFX_DRAW_BITMAP = 0x41
} fmrb_ipc_graphics_cmd_t;

// Audio sub-commands
typedef enum {
    FMRB_IPC_MSG_AUDIO_PLAY = 0x20,
    FMRB_IPC_MSG_AUDIO_STOP = 0x21,
    FMRB_IPC_MSG_AUDIO_PAUSE = 0x22,
    FMRB_IPC_MSG_AUDIO_RESUME = 0x23,
    FMRB_IPC_MSG_AUDIO_SET_VOLUME = 0x24,
    FMRB_IPC_MSG_AUDIO_QUEUE_SAMPLES = 0x25
} fmrb_ipc_audio_cmd_t;

// Legacy message types (for backward compatibility)
#define FMRB_IPC_MSG_GRAPHICS_CLEAR      0x11
#define FMRB_IPC_MSG_GRAPHICS_PRESENT    0x12
#define FMRB_IPC_MSG_GRAPHICS_SET_PIXEL  0x13
#define FMRB_IPC_MSG_GRAPHICS_DRAW_LINE  0x14
#define FMRB_IPC_MSG_GRAPHICS_DRAW_RECT  0x15
#define FMRB_IPC_MSG_GRAPHICS_DRAW_TEXT  0x16
#define FMRB_IPC_MSG_ACK                 0xF0
#define FMRB_IPC_MSG_NACK                0xF1
#define FMRB_IPC_MSG_PING                0xF2
#define FMRB_IPC_MSG_PONG                0xF3

// Frame header (based on IPC_spec.md)
typedef struct __attribute__((packed)) {
    uint8_t type;    // Message type
    uint8_t seq;     // Sequence number
    uint16_t len;    // Payload bytes
} fmrb_ipc_frame_hdr_t;

// Chunk flags
typedef enum {
    FMRB_IPC_CHUNK_FL_START = 1 << 0,
    FMRB_IPC_CHUNK_FL_END = 1 << 1,
    FMRB_IPC_CHUNK_FL_ERR = 1 << 7
} fmrb_ipc_chunk_flags_t;

// Chunked header
typedef struct __attribute__((packed)) {
    uint8_t flags;       // Chunk flags
    uint8_t chunk_id;    // Chunk identifier
    uint16_t chunk_len;  // Chunk length
    uint32_t offset;     // Offset in total data
    uint32_t total_len;  // Total data length
} fmrb_ipc_chunk_info_t;

// Response header
typedef struct __attribute__((packed)) {
    uint8_t type;      // Message type
    uint8_t seq;       // Rolling counter
    uint16_t response; // 0: OK, others: Fail
} fmrb_ipc_frame_response_hdr_t;

// Chunk ACK
typedef struct __attribute__((packed)) {
    uint8_t chunk_id;     // Target lane
    uint8_t gen;          // Generation
    uint16_t credit;      // 0..window size (next concurrent request allowance)
    uint32_t next_offset; // Next offset to send
} fmrb_ipc_frame_chunk_ack_t;

// Legacy message header (kept for compatibility)
typedef struct __attribute__((packed)) {
    uint32_t magic;      // 0xFMRB (0x464D5242)
    uint8_t version;     // Protocol version
    uint8_t msg_type;    // Message type
    uint16_t sequence;   // Sequence number
    uint32_t payload_len; // Payload length
    uint32_t checksum;   // CRC32 checksum of payload
} fmrb_ipc_header_t;

// Graphics message structures
typedef struct __attribute__((packed)) {
    uint16_t x, y;
    uint16_t width, height;
    uint32_t color;
} fmrb_ipc_graphics_clear_t;

typedef struct __attribute__((packed)) {
    uint16_t x, y;
    uint32_t color;
} fmrb_ipc_graphics_pixel_t;

typedef struct __attribute__((packed)) {
    uint16_t x1, y1;
    uint16_t x2, y2;
    uint32_t color;
} fmrb_ipc_graphics_line_t;

typedef struct __attribute__((packed)) {
    uint16_t x, y;
    uint16_t width, height;
    uint32_t color;
    bool filled;
} fmrb_ipc_graphics_rect_t;

typedef struct __attribute__((packed)) {
    uint16_t x, y;
    uint32_t color;
    uint8_t font_size;
    uint16_t text_len;
    // Followed by text data
} fmrb_ipc_graphics_text_t;

// Audio message structures
typedef struct __attribute__((packed)) {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t data_len;
    // Followed by audio data
} fmrb_ipc_audio_play_t;

typedef struct __attribute__((packed)) {
    uint8_t volume; // 0-100
} fmrb_ipc_audio_volume_t;

// Response structures
typedef struct __attribute__((packed)) {
    uint16_t original_sequence;
    uint8_t status; // 0 = success, others = error codes
} fmrb_ipc_ack_t;

// Magic number
#define FMRB_IPC_MAGIC 0x464D5242 // "FMRB"

// Max payload size
#define FMRB_IPC_MAX_PAYLOAD_SIZE 4096

// Utility functions (legacy)
uint32_t fmrb_ipc_calculate_checksum(const uint8_t *data, size_t len);
bool fmrb_ipc_verify_header(const fmrb_ipc_header_t *header);
void fmrb_ipc_init_header(fmrb_ipc_header_t *header, uint8_t msg_type, uint16_t sequence, uint32_t payload_len);

// Frame utility functions (IPC_spec.md compliant)
/**
 * @brief Build a frame with header + payload + CRC32, then COBS encode
 * @param type Frame type
 * @param seq Sequence number
 * @param payload Payload data
 * @param payload_len Payload length
 * @param output Output buffer (must be large enough)
 * @param output_len Output buffer size
 * @return Encoded frame length (including 0x00 terminator), or -1 on error
 */
ssize_t fmrb_ipc_frame_encode(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t payload_len,
                               uint8_t *output, size_t output_len);

/**
 * @brief Decode a COBS frame and verify CRC32
 * @param input Encoded frame (without 0x00 terminator)
 * @param input_len Encoded frame length
 * @param hdr Output frame header
 * @param payload Output payload buffer
 * @param payload_max_len Maximum payload buffer size
 * @param payload_len Output actual payload length
 * @return 0 on success, -1 on error
 */
int fmrb_ipc_frame_decode(const uint8_t *input, size_t input_len,
                           fmrb_ipc_frame_hdr_t *hdr, uint8_t *payload,
                           size_t payload_max_len, uint16_t *payload_len);

#ifdef __cplusplus
}
#endif

#endif // FMRB_IPC_PROTOCOL_H