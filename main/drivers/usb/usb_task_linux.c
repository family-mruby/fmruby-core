/**
 * @file usb_task_linux.c
 * @brief USB Host task implementation for Linux target (receives HID events from host via socket)
 */

#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_hid_event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "usb_task.h"
#include "kernel/host/host_task.h"

#define INPUT_SOCKET_PATH "/tmp/fmrb_input_socket"
#define MAX_PACKET_SIZE 512

static const char *TAG = "usb_task";

static int g_socket_fd = -1;
static bool g_running = false;
static fmrb_task_handle_t g_task_handle = 0;

/**
 * @brief Process received HID event
 */
static void process_hid_event(uint8_t type, const uint8_t *data, uint16_t len) {
    switch (type) {
        case HID_EVENT_KEY_DOWN:
            if (len >= sizeof(hid_keyboard_event_t)) {
                const hid_keyboard_event_t *kbd = (const hid_keyboard_event_t*)data;
                FMRB_LOGI(TAG, "Keyboard DOWN: scancode=%u keycode=%u modifier=0x%02x",
                         kbd->scancode, kbd->keycode, kbd->modifier);
                fmrb_host_send_key_down(kbd->keycode);
            }
            break;

        case HID_EVENT_KEY_UP:
            if (len >= sizeof(hid_keyboard_event_t)) {
                const hid_keyboard_event_t *kbd = (const hid_keyboard_event_t*)data;
                FMRB_LOGI(TAG, "Keyboard UP: scancode=%u keycode=%u modifier=0x%02x",
                         kbd->scancode, kbd->keycode, kbd->modifier);
                fmrb_host_send_key_up(kbd->keycode);
            }
            break;

        case HID_EVENT_MOUSE_BUTTON:
            if (len >= sizeof(hid_mouse_button_event_t)) {
                const hid_mouse_button_event_t *mouse = (const hid_mouse_button_event_t*)data;
                FMRB_LOGI(TAG, "Mouse button %u %s at (%u, %u)",
                         mouse->button,
                         (mouse->state) ? "pressed" : "released",
                         mouse->x, mouse->y);
                fmrb_host_send_mouse_click(mouse->x, mouse->y, mouse->button, mouse->state);
            }
            break;

        case HID_EVENT_MOUSE_MOTION:
            if (len >= sizeof(hid_mouse_motion_event_t)) {
                const hid_mouse_motion_event_t *motion = (const hid_mouse_motion_event_t*)data;
                FMRB_LOGD(TAG, "Mouse motion to (%u, %u)", motion->x, motion->y);
                fmrb_host_send_mouse_move(motion->x, motion->y);
            }
            break;

        default:
            FMRB_LOGW(TAG, "Unknown HID event type: 0x%02x", type);
            break;
    }
}

/**
 * @brief USB task thread (receives HID events from socket)
 */
static void usb_task_thread(void *arg) {
    (void)arg;

    FMRB_LOGI(TAG, "USB task thread started");

    uint8_t recv_buffer[MAX_PACKET_SIZE];
    size_t recv_pos = 0;

    while (g_running) {
        // Try to receive data
        ssize_t received = recv(g_socket_fd, recv_buffer + recv_pos, sizeof(recv_buffer) - recv_pos, MSG_DONTWAIT);

        if (received > 0) {
            recv_pos += received;

            // Process complete packets: [type(1)][len(2)][data(len)]
            while (recv_pos >= 3) {
                uint8_t type = recv_buffer[0];
                uint16_t len = recv_buffer[1] | (recv_buffer[2] << 8);

                if (recv_pos < 3 + len) {
                    // Incomplete packet
                    break;
                }

                // Process event
                process_hid_event(type, recv_buffer + 3, len);

                // Remove processed packet
                size_t remaining = recv_pos - (3 + len);
                if (remaining > 0) {
                    memmove(recv_buffer, recv_buffer + 3 + len, remaining);
                }
                recv_pos = remaining;
            }
        } else if (received == 0) {
            // Connection closed
            FMRB_LOGW(TAG, "Host disconnected");
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                FMRB_LOGE(TAG, "recv error: %s", strerror(errno));
                break;
            }
        }

        // Sleep to avoid busy waiting
        fmrb_hal_time_delay_ms(10);
    }

    FMRB_LOGI(TAG, "USB task thread exiting");
    fmrb_task_delete(0);
}

/**
 * @brief Initialize USB Host (Linux: connect to input socket)
 */
fmrb_err_t usb_task_init(void)
{
    FMRB_LOGI(TAG, "USB task init (Linux - connecting to input socket)");

    // Create socket
    g_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_socket_fd < 0) {
        FMRB_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        return FMRB_ERR_FAILED;
    }

    // Set non-blocking
    int flags = fcntl(g_socket_fd, F_GETFL, 0);
    fcntl(g_socket_fd, F_SETFL, flags | O_NONBLOCK);

    // Connect to input socket server
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, INPUT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Try to connect with retry
    int retry_count = 0;
    while (retry_count < 20) {
        if (connect(g_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            FMRB_LOGI(TAG, "Connected to %s", INPUT_SOCKET_PATH);
            break;
        }

        if (errno != ENOENT && errno != ECONNREFUSED && errno != EINPROGRESS) {
            FMRB_LOGE(TAG, "Failed to connect: %s", strerror(errno));
            close(g_socket_fd);
            g_socket_fd = -1;
            return FMRB_ERR_FAILED;
        }

        usleep(100000); // Wait 100ms
        retry_count++;
    }

    if (retry_count >= 20) {
        FMRB_LOGE(TAG, "Failed to connect after %d retries", retry_count);
        close(g_socket_fd);
        g_socket_fd = -1;
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "USB task initialized");

    //start task
    usb_task_start();
    FMRB_LOGI(TAG, "USB task started");

    return FMRB_OK;
}

/**
 * @brief Start USB Host task (Linux: start receive thread)
 */
void usb_task_start(void)
{
    if (g_socket_fd < 0) {
        FMRB_LOGW(TAG, "Socket not connected, cannot start task");
        return;
    }

    FMRB_LOGI(TAG, "Starting USB task");

    g_running = true;

    fmrb_base_type_t ret = fmrb_task_create(
        usb_task_thread,
        "usb_rx",
        4096,  // stack size
        NULL,
        5,     // priority
        &g_task_handle
    );

    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create USB task");
        g_running = false;
        return;
    }

    FMRB_LOGI(TAG, "USB task started");
}
