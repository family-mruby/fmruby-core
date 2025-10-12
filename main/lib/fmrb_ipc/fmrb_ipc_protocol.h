#ifndef FMRB_IPC_PROTOCOL_H
#define FMRB_IPC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol version
#define FMRB_IPC_PROTOCOL_VERSION 1

// Message types
typedef enum {
    FMRB_IPC_MSG_GRAPHICS_DRAW = 0x10,
    FMRB_IPC_MSG_GRAPHICS_CLEAR = 0x11,
    FMRB_IPC_MSG_GRAPHICS_PRESENT = 0x12,
    FMRB_IPC_MSG_GRAPHICS_SET_PIXEL = 0x13,
    FMRB_IPC_MSG_GRAPHICS_DRAW_LINE = 0x14,
    FMRB_IPC_MSG_GRAPHICS_DRAW_RECT = 0x15,
    FMRB_IPC_MSG_GRAPHICS_DRAW_TEXT = 0x16,

    FMRB_IPC_MSG_AUDIO_PLAY = 0x20,
    FMRB_IPC_MSG_AUDIO_STOP = 0x21,
    FMRB_IPC_MSG_AUDIO_PAUSE = 0x22,
    FMRB_IPC_MSG_AUDIO_RESUME = 0x23,
    FMRB_IPC_MSG_AUDIO_SET_VOLUME = 0x24,
    FMRB_IPC_MSG_AUDIO_QUEUE_SAMPLES = 0x25,

    FMRB_IPC_MSG_ACK = 0xF0,
    FMRB_IPC_MSG_NACK = 0xF1,
    FMRB_IPC_MSG_PING = 0xF2,
    FMRB_IPC_MSG_PONG = 0xF3
} fmrb_ipc_msg_type_t;

// Message header
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

// Utility functions
uint32_t fmrb_ipc_calculate_checksum(const uint8_t *data, size_t len);
bool fmrb_ipc_verify_header(const fmrb_ipc_header_t *header);
void fmrb_ipc_init_header(fmrb_ipc_header_t *header, uint8_t msg_type, uint16_t sequence, uint32_t payload_len);

#ifdef __cplusplus
}
#endif

#endif // FMRB_IPC_PROTOCOL_H