#include "socket_server.h"
#include "graphics_handler.h"
#include "audio_handler.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>

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

    // Process complete messages
    int messages_processed = 0;
    while (buffer_pos >= sizeof(fmrb_message_header_t)) {
        const fmrb_message_header_t *header = (const fmrb_message_header_t*)buffer;

        if (header->magic != FMRB_MAGIC) {
            fprintf(stderr, "Invalid magic, resetting buffer\n");
            buffer_pos = 0;
            break;
        }

        if (header->size > BUFFER_SIZE) {
            fprintf(stderr, "Message too large: %u\n", header->size);
            buffer_pos = 0;
            break;
        }

        if (buffer_pos < header->size) {
            // Need more data
            break;
        }

        // Process complete message
        if (process_message(buffer, header->size) == 0) {
            messages_processed++;
        }

        // Remove processed message from buffer
        size_t remaining = buffer_pos - header->size;
        if (remaining > 0) {
            memmove(buffer, buffer + header->size, remaining);
        }
        buffer_pos = remaining;
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