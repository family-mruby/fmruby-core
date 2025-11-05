#ifndef FMRB_HOST_PROTOCOL_H
#define FMRB_HOST_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol version
#define FMRB_HOST_PROTOCOL_VERSION 1

// Magic number for message validation
#define FMRB_MAGIC 0x464D5242  // "FMRB"

// Message types
typedef enum {
    FMRB_MSG_GRAPHICS = 1,
    FMRB_MSG_AUDIO = 2,
    FMRB_HOST_MSG_GRAPHICS = 0x10,
    FMRB_HOST_MSG_AUDIO = 0x20,
    FMRB_HOST_MSG_INPUT = 0x30,
    FMRB_HOST_MSG_CONTROL = 0x40
} fmrb_host_msg_type_t;

// Message header (compatible with main/common/protocol.h)
typedef struct {
    uint32_t magic;      // FMRB_MAGIC (0x464D5242)
    uint32_t type;       // Message type
    uint32_t size;       // Message total size
} __attribute__((packed)) fmrb_message_header_t;

// Host-specific message header
typedef struct {
    uint8_t version;        // Protocol version
    uint8_t msg_type;       // Message type
    uint16_t length;        // Payload length
    uint32_t sequence;      // Sequence number
} __attribute__((packed)) fmrb_host_msg_header_t;

// Control command subtypes
#define FMRB_CONTROL_CMD_INIT_DISPLAY 0x01

// Display initialization command
typedef struct {
    uint8_t cmd_type;        // FMRB_CONTROL_CMD_INIT_DISPLAY
    uint16_t width;
    uint16_t height;
    uint8_t color_depth;     // 8 for RGB332
} __attribute__((packed)) fmrb_control_init_display_t;

// Socket configuration
#define FMRB_HOST_SOCKET_PATH "/tmp/fmrb_host.sock"
#define FMRB_HOST_MAX_CLIENTS 4
#define FMRB_HOST_BUFFER_SIZE 4096

#ifdef __cplusplus
}
#endif

#endif // FMRB_HOST_PROTOCOL_H