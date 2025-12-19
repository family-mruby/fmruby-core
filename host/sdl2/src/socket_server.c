#include "socket_server.h"
#include "graphics_handler.h"
#include "audio_handler.h"
#include "../common/fmrb_link_cobs.h"
#include "fmrb_link_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <msgpack.h>

// Socket server log levels
typedef enum {
    SOCK_LOG_NONE = 0,     // No logging
    SOCK_LOG_ERROR = 1,    // Error messages only
    SOCK_LOG_INFO = 2,     // Info + Error
    SOCK_LOG_DEBUG = 3,    // Debug + Info + Error (verbose)
} sock_log_level_t;

// Current log level (default: errors only)
static sock_log_level_t g_sock_log_level = SOCK_LOG_ERROR;

// Log macros
#define SOCK_LOG_E(fmt, ...) do { if (g_sock_log_level >= SOCK_LOG_ERROR) { fprintf(stderr, "[SOCK_ERR] " fmt "\n", ##__VA_ARGS__); } } while(0)
#define SOCK_LOG_I(fmt, ...) do { if (g_sock_log_level >= SOCK_LOG_INFO) { printf("[SOCK_INFO] " fmt "\n", ##__VA_ARGS__); } } while(0)
#define SOCK_LOG_D(fmt, ...) do { if (g_sock_log_level >= SOCK_LOG_DEBUG) { printf("[SOCK_DBG] " fmt "\n", ##__VA_ARGS__); } } while(0)

// Forward declaration - implemented in main.cpp
extern int init_display_callback(uint16_t width, uint16_t height, uint8_t color_depth);

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
    // Allocate buffer for decoded data (COBS + CRC32)
    uint8_t *decoded_buffer = (uint8_t*)malloc(encoded_len);
    if (!decoded_buffer) {
        fprintf(stderr, "Failed to allocate decode buffer\n");
        return -1;
    }

    // COBS decode
    ssize_t decoded_len = fmrb_link_cobs_decode(encoded_data, encoded_len, decoded_buffer);
    if (decoded_len < (ssize_t)sizeof(uint32_t)) {
        fprintf(stderr, "COBS decode failed or frame too small\n");
        free(decoded_buffer);
        return -1;
    }

    // Separate msgpack data and CRC32
    size_t msgpack_len = decoded_len - sizeof(uint32_t);
    uint8_t *msgpack_data = decoded_buffer;
    uint32_t received_crc;
    memcpy(&received_crc, decoded_buffer + msgpack_len, sizeof(uint32_t));

    // Verify CRC32
    uint32_t calculated_crc = fmrb_link_crc32_update(0, msgpack_data, msgpack_len);
    if (received_crc != calculated_crc) {
        fprintf(stderr, "CRC32 mismatch: expected=0x%08x, actual=0x%08x\n", calculated_crc, received_crc);
        free(decoded_buffer);
        return -1;
    }

    // Unpack msgpack array: [type, seq, sub_cmd, payload]
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);
    msgpack_unpack_return ret = msgpack_unpack_next(&msg, (const char*)msgpack_data, msgpack_len, NULL);

    if (ret != MSGPACK_UNPACK_SUCCESS) {
        fprintf(stderr, "msgpack unpack failed\n");
        msgpack_unpacked_destroy(&msg);
        free(decoded_buffer);
        return -1;
    }

    msgpack_object root = msg.data;
    if (root.type != MSGPACK_OBJECT_ARRAY || root.via.array.size != 4) {
        fprintf(stderr, "Invalid msgpack format: not array or size != 4\n");
        msgpack_unpacked_destroy(&msg);
        free(decoded_buffer);
        return -1;
    }

    // Extract fields
    uint8_t type = root.via.array.ptr[0].via.u64;
    uint8_t seq = root.via.array.ptr[1].via.u64;
    uint8_t sub_cmd = root.via.array.ptr[2].via.u64;

    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    if (root.via.array.ptr[3].type == MSGPACK_OBJECT_BIN) {
        payload = (const uint8_t*)root.via.array.ptr[3].via.bin.ptr;
        payload_len = root.via.array.ptr[3].via.bin.size;
    }

    // Debug log for GRAPHICS commands (controlled by log level)
    if (type == FMRB_LINK_TYPE_GRAPHICS) {
        SOCK_LOG_D("RX msgpack: type=%d seq=%d sub_cmd=0x%02x payload_len=%zu msgpack_len=%zu",
               type, seq, sub_cmd, payload_len, msgpack_len);
        if (g_sock_log_level >= SOCK_LOG_DEBUG) {
            printf("RX msgpack bytes (%zu): ", msgpack_len);
            for (size_t i = 0; i < msgpack_len && i < 64; i++) {
                printf("%02X ", msgpack_data[i]);
                if ((i + 1) % 16 == 0) printf("\n");
            }
            if (msgpack_len > 0) printf("\n");
            fflush(stdout);
        }
    }

    // sub_cmd contains the command type, payload contains only structure data
    // Pass sub_cmd as cmd_type to handlers
    const uint8_t *cmd_buffer = payload;
    size_t cmd_len = payload_len;

    // Process based on type
    int result = 0;
    switch (type & 0x7F) {
        case FMRB_LINK_TYPE_CONTROL:
            // For control commands, sub_cmd is the command type
            if (sub_cmd == FMRB_LINK_CONTROL_VERSION && cmd_len >= 1) {
                // Version check request
                uint8_t remote_version = cmd_buffer[0];
                uint8_t local_version = FMRB_LINK_PROTOCOL_VERSION;

                printf("Received VERSION check: remote=%d, local=%d, seq=%u, client_fd=%d\n",
                       remote_version, local_version, seq, client_fd);

                // Send version response via ACK with version in payload
                result = socket_server_send_ack(type, seq, &local_version, sizeof(local_version));

                if (result == 0) {
                    printf("VERSION ACK sent successfully\n");
                } else {
                    fprintf(stderr, "VERSION ACK send failed: result=%d\n", result);
                }

                if (remote_version != local_version) {
                    fprintf(stderr, "WARNING: Protocol version mismatch! remote=%d, local=%d\n",
                            remote_version, local_version);
                }
            } else if (sub_cmd == FMRB_LINK_CONTROL_INIT_DISPLAY && cmd_len >= sizeof(fmrb_control_init_display_t)) {
                const fmrb_control_init_display_t *init_cmd = (const fmrb_control_init_display_t*)cmd_buffer;
                printf("Received INIT_DISPLAY: %dx%d, %d-bit\n",
                       init_cmd->width, init_cmd->height, init_cmd->color_depth);
                result = init_display_callback(init_cmd->width, init_cmd->height, init_cmd->color_depth);

                // Send ACK to prevent retransmission
                if (result == 0) {
                    socket_server_send_ack(type, seq, NULL, 0);
                }
            } else {
                fprintf(stderr, "Unknown control command: 0x%02x\n", sub_cmd);
                result = -1;
            }
            break;

        case FMRB_LINK_TYPE_GRAPHICS:
            // Pass msg_type and sub_cmd as graphics cmd_type
            result = graphics_handler_process_command(type, sub_cmd, seq, cmd_buffer, cmd_len);
            // Send ACK to prevent retransmission
            if (result == 0) {
                socket_server_send_ack(type, seq, NULL, 0);
            }
            break;

        case FMRB_LINK_TYPE_AUDIO:
            result = audio_handler_process_command(cmd_buffer, cmd_len);
            break;

        default:
            fprintf(stderr, "Unknown frame type: %u\n", type);
            result = -1;
            break;
    }

    // cmd_buffer now points to payload inside decoded_buffer, don't free separately
    msgpack_unpacked_destroy(&msg);
    free(decoded_buffer);
    return result;
}

// Legacy process_message() removed - now using msgpack + COBS protocol via process_cobs_frame()

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

// Send ACK response with optional payload
int socket_server_send_ack(uint8_t type, uint8_t seq, const uint8_t *response_data, uint16_t response_len) {
    if (client_fd == -1) {
        fprintf(stderr, "Cannot send ACK: no client connected\n");
        return -1;
    }

    // Build msgpack response: [type, seq, sub_cmd=0xF0 (ACK), payload]
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    // Pack as array: [type, seq, 0xF0 (ACK), response_data]
    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, type);
    msgpack_pack_uint8(&pk, seq);
    msgpack_pack_uint8(&pk, 0xF0);  // ACK sub-command

    // Pack response data as binary
    if (response_data && response_len > 0) {
        msgpack_pack_bin(&pk, response_len);
        msgpack_pack_bin_body(&pk, response_data, response_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    // Add CRC32 to msgpack message
    uint32_t crc = fmrb_link_crc32_update(0, (const uint8_t*)sbuf.data, sbuf.size);
    size_t msg_with_crc_len = sbuf.size + sizeof(uint32_t);
    uint8_t *msg_with_crc = (uint8_t*)malloc(msg_with_crc_len);
    if (!msg_with_crc) {
        msgpack_sbuffer_destroy(&sbuf);
        fprintf(stderr, "Failed to allocate buffer for CRC\n");
        return -1;
    }

    memcpy(msg_with_crc, sbuf.data, sbuf.size);
    memcpy(msg_with_crc + sbuf.size, &crc, sizeof(uint32_t));
    msgpack_sbuffer_destroy(&sbuf);

    // COBS encode the msgpack + CRC32
    uint8_t encoded_buffer[BUFFER_SIZE];
    size_t encoded_len = fmrb_link_cobs_encode(msg_with_crc, msg_with_crc_len, encoded_buffer);
    free(msg_with_crc);

    if (encoded_len == 0) {
        fprintf(stderr, "COBS encode failed for ACK\n");
        return -1;
    }

    // Add 0x00 terminator
    encoded_buffer[encoded_len] = 0x00;
    encoded_len++;

    // Send to client
    ssize_t written = write(client_fd, encoded_buffer, encoded_len);
    if (written != (ssize_t)encoded_len) {
        fprintf(stderr, "Failed to write ACK response: %zd/%zu (client_fd=%d, errno=%d: %s)\n",
                written, encoded_len, client_fd, errno, strerror(errno));
        return -1;
    }

    SOCK_LOG_D("ACK sent: type=%u seq=%u response_len=%u", type, seq, response_len);
    return 0;
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