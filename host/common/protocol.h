#ifndef FMRB_HOST_PROTOCOL_H
#define FMRB_HOST_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol version
#define FMRB_HOST_PROTOCOL_VERSION 1

// Message types
typedef enum {
    FMRB_HOST_MSG_GRAPHICS = 0x10,
    FMRB_HOST_MSG_AUDIO = 0x20,
    FMRB_HOST_MSG_INPUT = 0x30,
    FMRB_HOST_MSG_CONTROL = 0x40
} fmrb_host_msg_type_t;

// Message header
typedef struct {
    uint8_t version;        // Protocol version
    uint8_t msg_type;       // Message type
    uint16_t length;        // Payload length
    uint32_t sequence;      // Sequence number
} __attribute__((packed)) fmrb_host_msg_header_t;

// Socket configuration
#define FMRB_HOST_SOCKET_PATH "/tmp/fmrb_host.sock"
#define FMRB_HOST_MAX_CLIENTS 4
#define FMRB_HOST_BUFFER_SIZE 4096

#ifdef __cplusplus
}
#endif

#endif // FMRB_HOST_PROTOCOL_H