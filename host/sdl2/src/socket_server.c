#include "socket_server.h"
#include "graphics_handler.h"
#include "audio_handler.h"
#include "../common/protocol.h"
#include "../common/fmrb_ipc_cobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>

// Frame header (IPC_spec.md compliant)
typedef struct __attribute__((packed)) {
    uint8_t type;    // Message type
    uint8_t seq;     // Sequence number
    uint16_t len;    // Payload bytes
} fmrb_ipc_frame_hdr_t;

static int server_fd = -1;
static int client_fd = -1;
static int server_running = 0;

#define SOCKET_PATH "/tmp/fmrb_socket"
#define BUFFER_SIZE 4096

static int create_socket_server(void) {
    struct sockaddr_un addr;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    // Remove existing socket file
    unlink(SOCKET_PATH);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    if (listen(server_fd, 1) == -1) {
        fprintf(stderr, "Failed to listen on socket: %s\n", strerror(errno));
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    // Set non-blocking mode
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    printf("Socket server listening on %s\n", SOCKET_PATH);
    return 0;
}

static int accept_connection(void) {
    if (client_fd != -1) {
        return 0; // Already connected
    }

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "Failed to accept connection: %s\n", strerror(errno));
        }
        return -1;
    }

    // Set non-blocking mode for client socket
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    printf("Client connected\n");
    return 0;
}

static int process_cobs_frame(const uint8_t *encoded_data, size_t encoded_len) {
    // Allocate buffer for decoded data
    uint8_t *decoded_buffer = (uint8_t*)malloc(encoded_len);
    if (!decoded_buffer) {
        fprintf(stderr, "Failed to allocate decode buffer\n");
        return -1;
    }

    // COBS decode
    ssize_t decoded_len = fmrb_ipc_cobs_decode(encoded_data, encoded_len, decoded_buffer);
    if (decoded_len < (ssize_t)(sizeof(fmrb_ipc_frame_hdr_t) + sizeof(uint32_t))) {
        fprintf(stderr, "COBS decode failed or frame too small\n");
        free(decoded_buffer);
        return -1;
    }

    // Extract header
    fmrb_ipc_frame_hdr_t hdr;
    memcpy(&hdr, decoded_buffer, sizeof(fmrb_ipc_frame_hdr_t));

    // Verify size
    size_t expected_size = sizeof(fmrb_ipc_frame_hdr_t) + hdr.len + sizeof(uint32_t);
    if ((size_t)decoded_len != expected_size) {
        fprintf(stderr, "Frame size mismatch: expected=%zu, actual=%zd\n", expected_size, decoded_len);
        free(decoded_buffer);
        return -1;
    }

    // Extract and verify CRC32
    uint32_t received_crc;
    memcpy(&received_crc, decoded_buffer + sizeof(fmrb_ipc_frame_hdr_t) + hdr.len, sizeof(uint32_t));

    uint32_t calculated_crc = fmrb_ipc_crc32_update(0, decoded_buffer, sizeof(fmrb_ipc_frame_hdr_t) + hdr.len);
    if (received_crc != calculated_crc) {
        fprintf(stderr, "CRC32 mismatch: expected=0x%08x, actual=0x%08x\n", calculated_crc, received_crc);
        free(decoded_buffer);
        return -1;
    }

    // Extract payload
    const uint8_t *payload = decoded_buffer + sizeof(fmrb_ipc_frame_hdr_t);

    // Process based on type
    int result = 0;
    switch (hdr.type & 0x7F) { // Mask out flags
        case 2: // FMRB_IPC_TYPE_GRAPHICS
            result = graphics_handler_process_command(payload, hdr.len);
            break;

        case 4: // FMRB_IPC_TYPE_AUDIO
            result = audio_handler_process_command(payload, hdr.len);
            break;

        default:
            fprintf(stderr, "Unknown frame type: %u\n", hdr.type);
            result = -1;
            break;
    }

    free(decoded_buffer);
    return result;
}

static int process_message(const uint8_t *data, size_t size) {
    if (size < sizeof(fmrb_message_header_t)) {
        fprintf(stderr, "Message too small\n");
        return -1;
    }

    const fmrb_message_header_t *header = (const fmrb_message_header_t*)data;

    if (header->magic != FMRB_MAGIC) {
        fprintf(stderr, "Invalid magic number: 0x%08x\n", header->magic);
        return -1;
    }

    if (header->size != size) {
        fprintf(stderr, "Size mismatch: header=%u, actual=%zu\n", header->size, size);
        return -1;
    }

    const uint8_t *payload = data + sizeof(fmrb_message_header_t);
    size_t payload_size = size - sizeof(fmrb_message_header_t);

    switch (header->type) {
        case FMRB_MSG_GRAPHICS:
            return graphics_handler_process_command(payload, payload_size);

        case FMRB_MSG_AUDIO:
            return audio_handler_process_command(payload, payload_size);

        default:
            fprintf(stderr, "Unknown message type: %u\n", header->type);
            return -1;
    }
}

static int read_message(void) {
    static uint8_t buffer[BUFFER_SIZE];
    static size_t buffer_pos = 0;

    // Read data into buffer
    ssize_t bytes_read = read(client_fd, buffer + buffer_pos, BUFFER_SIZE - buffer_pos);
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            printf("Client disconnected\n");
            close(client_fd);
            client_fd = -1;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "Read error: %s\n", strerror(errno));
            close(client_fd);
            client_fd = -1;
        }
        return -1;
    }

    buffer_pos += bytes_read;

    // Process complete COBS frames (terminated by 0x00)
    int messages_processed = 0;
    size_t scan_pos = 0;

    while (scan_pos < buffer_pos) {
        // Look for frame terminator (0x00)
        size_t frame_end = scan_pos;
        while (frame_end < buffer_pos && buffer[frame_end] != 0x00) {
            frame_end++;
        }

        if (frame_end >= buffer_pos) {
            // No complete frame yet
            break;
        }

        // Found a complete frame: [scan_pos .. frame_end-1] + 0x00 at frame_end
        size_t frame_len = frame_end - scan_pos;

        if (frame_len > 0) {
            // Process COBS frame (without the 0x00 terminator)
            if (process_cobs_frame(buffer + scan_pos, frame_len) == 0) {
                messages_processed++;
            }
        }

        // Move to next frame (skip the 0x00 terminator)
        scan_pos = frame_end + 1;
    }

    // Remove processed data from buffer
    if (scan_pos > 0) {
        size_t remaining = buffer_pos - scan_pos;
        if (remaining > 0) {
            memmove(buffer, buffer + scan_pos, remaining);
        }
        buffer_pos = remaining;
    }

    // Check for buffer overflow
    if (buffer_pos >= BUFFER_SIZE - 1) {
        fprintf(stderr, "Buffer overflow, resetting\n");
        buffer_pos = 0;
    }

    return messages_processed;
}

int socket_server_start(void) {
    if (server_running) {
        return 0;
    }

    if (create_socket_server() != 0) {
        return -1;
    }

    server_running = 1;
    return 0;
}

void socket_server_stop(void) {
    if (client_fd != -1) {
        close(client_fd);
        client_fd = -1;
    }

    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
        unlink(SOCKET_PATH);
    }

    server_running = 0;
    printf("Socket server stopped\n");
}

int socket_server_process(void) {
    if (!server_running) {
        return 0;
    }

    // Try to accept new connections
    accept_connection();

    // Process messages from connected client
    if (client_fd != -1) {
        return read_message();
    }

    return 0;
}

int socket_server_is_running(void) {
    return server_running;
}