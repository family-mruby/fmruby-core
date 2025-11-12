/**
 * @file input_socket.c
 * @brief Unix socket server for HID input events
 */

#include "input_socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#define INPUT_SOCKET_PATH "/tmp/fmrb_input_socket"
#define MAX_PACKET_SIZE 512

static int g_server_fd = -1;
static int g_client_fd = -1;

int input_socket_start(void) {
    if (g_server_fd >= 0) {
        fprintf(stderr, "[INPUT_SOCKET] Server already running\n");
        return 0;
    }

    // Remove existing socket file
    unlink(INPUT_SOCKET_PATH);

    // Create socket
    g_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("[INPUT_SOCKET] Failed to create socket");
        return -1;
    }

    // Bind socket
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, INPUT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[INPUT_SOCKET] Failed to bind socket");
        close(g_server_fd);
        g_server_fd = -1;
        return -1;
    }

    // Listen
    if (listen(g_server_fd, 1) < 0) {
        perror("[INPUT_SOCKET] Failed to listen");
        close(g_server_fd);
        g_server_fd = -1;
        unlink(INPUT_SOCKET_PATH);
        return -1;
    }

    // Set non-blocking
    int flags = fcntl(g_server_fd, F_GETFL, 0);
    fcntl(g_server_fd, F_SETFL, flags | O_NONBLOCK);

    printf("[INPUT_SOCKET] Server started on %s\n", INPUT_SOCKET_PATH);

    // Try to accept client (non-blocking)
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    g_client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (g_client_fd >= 0) {
        printf("[INPUT_SOCKET] Client connected\n");
    }

    return 0;
}

void input_socket_stop(void) {
    if (g_client_fd >= 0) {
        close(g_client_fd);
        g_client_fd = -1;
    }

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
        unlink(INPUT_SOCKET_PATH);
        printf("[INPUT_SOCKET] Server stopped\n");
    }
}

int input_socket_send_event(uint8_t type, const void* data, uint16_t len) {
    // Try to accept client if not connected
    if (g_client_fd < 0 && g_server_fd >= 0) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        g_client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (g_client_fd >= 0) {
            printf("[INPUT_SOCKET] Client connected\n");
        }
    }

    if (g_client_fd < 0) {
        // No client connected, silently ignore
        return 0;
    }

    // Build packet: [type(1)][len(2)][data(len)]
    uint8_t packet[MAX_PACKET_SIZE];
    if (3 + len > sizeof(packet)) {
        fprintf(stderr, "[INPUT_SOCKET] Packet too large: %u bytes\n", len);
        return -1;
    }

    packet[0] = type;
    packet[1] = (uint8_t)(len & 0xFF);
    packet[2] = (uint8_t)((len >> 8) & 0xFF);
    if (data && len > 0) {
        memcpy(packet + 3, data, len);
    }

    ssize_t sent = send(g_client_fd, packet, 3 + len, MSG_NOSIGNAL);
    if (sent < 0) {
        if (errno == EPIPE || errno == ECONNRESET) {
            printf("[INPUT_SOCKET] Client disconnected\n");
            close(g_client_fd);
            g_client_fd = -1;
        } else {
            perror("[INPUT_SOCKET] Send failed");
        }
        return -1;
    }

    return 0;
}

int input_socket_is_connected(void) {
    return (g_client_fd >= 0) ? 1 : 0;
}
