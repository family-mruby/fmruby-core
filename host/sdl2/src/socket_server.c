#include "socket_server.h"
#include "graphics_handler.h"
#include "audio_handler.h"
#include "../common/protocol.h"
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

    // Debug log
#ifdef FMRB_IPC_DEBUG
    printf("RX msgpack: type=%d seq=%d sub_cmd=0x%02x payload_len=%zu msgpack_len=%zu\n",
           type, seq, sub_cmd, payload_len, msgpack_len);
    printf("RX msgpack bytes (%zu): ", msgpack_len);
    for (size_t i = 0; i < msgpack_len && i < 64; i++) {
        printf("%02X ", msgpack_data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
    fflush(stdout);
#endif

    // sub_cmd contains the command type, payload contains only structure data
    // Pass sub_cmd as cmd_type to handlers
    const uint8_t *cmd_buffer = payload;
    size_t cmd_len = payload_len;

    // Process based on type
    int result = 0;
    switch (type & 0x7F) {
        case 1: // FMRB_IPC_TYPE_CONTROL
            // For control commands, sub_cmd is the command type
            if (sub_cmd == FMRB_CONTROL_CMD_INIT_DISPLAY && cmd_len >= sizeof(fmrb_control_init_display_t)) {
                const fmrb_control_init_display_t *init_cmd = (const fmrb_control_init_display_t*)cmd_buffer;
                printf("Received INIT_DISPLAY: %dx%d, %d-bit\n",
                       init_cmd->width, init_cmd->height, init_cmd->color_depth);
                result = init_display_callback(init_cmd->width, init_cmd->height, init_cmd->color_depth);
            } else {
                fprintf(stderr, "Unknown control command: 0x%02x\n", sub_cmd);
                result = -1;
            }
            break;

        case 2: // FMRB_IPC_TYPE_GRAPHICS
            // Pass msg_type and sub_cmd as graphics cmd_type
            result = graphics_handler_process_command(type, sub_cmd, seq, cmd_buffer, cmd_len);
            break;

        case 4: // FMRB_IPC_TYPE_AUDIO
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
            // Legacy: payload[0] is cmd_type, rest is data
            if (payload_size < 1) {
                fprintf(stderr, "Graphics payload too small\n");
                return -1;
            }
            return graphics_handler_process_command(2, payload[0], 0, payload + 1, payload_size - 1);  // type=2 (GRAPHICS), seq=0 (legacy)

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

    // Build fmrb_link_header_t
    typedef struct __attribute__((packed)) {
        uint32_t magic;       // 0x464D5242 "FMRB"
        uint8_t version;      // Protocol version
        uint8_t msg_type;     // Message type
        uint16_t sequence;    // Sequence number
        uint32_t payload_len; // Payload length
        uint32_t checksum;    // CRC32 checksum of payload
    } fmrb_link_header_t;

    fmrb_link_header_t header;
    header.magic = 0x464D5242;  // FMRB_LINK_MAGIC
    header.version = 1;
    header.msg_type = 0xF0;  // FMRB_LINK_MSG_ACK
    header.sequence = seq;
    header.payload_len = sbuf.size;
    header.checksum = fmrb_link_crc32_update(0, (const uint8_t*)sbuf.data, sbuf.size);

    // Prepare complete message: header + msgpack payload
    size_t total_msg_len = sizeof(header) + sbuf.size;
    uint8_t *complete_msg = (uint8_t*)malloc(total_msg_len);
    if (!complete_msg) {
        fprintf(stderr, "Failed to allocate buffer for ACK message\n");
        msgpack_sbuffer_destroy(&sbuf);
        return -1;
    }

    memcpy(complete_msg, &header, sizeof(header));
    memcpy(complete_msg + sizeof(header), sbuf.data, sbuf.size);
    msgpack_sbuffer_destroy(&sbuf);

    // COBS encode the complete message
    uint8_t encoded_buffer[BUFFER_SIZE];
    size_t encoded_len = fmrb_link_cobs_encode(complete_msg, total_msg_len, encoded_buffer);

    free(complete_msg);

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
        fprintf(stderr, "Failed to write ACK response: %zd/%zu\n", written, encoded_len);
        return -1;
    }

    printf("ACK sent: type=%u seq=%u response_len=%u\n", type, seq, response_len);
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