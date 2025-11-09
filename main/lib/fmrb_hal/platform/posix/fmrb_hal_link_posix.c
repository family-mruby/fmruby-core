#include "../../fmrb_hal.h"
#include "../../fmrb_hal_link.h"
#include "../../../fmrb_link/fmrb_link_cobs.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define SOCKET_PATH "/tmp/fmrb_socket"

typedef struct {
    int socket_fd;
    fmrb_task_handle_t thread;
    fmrb_link_callback_t callback;
    void *user_data;
    bool running;
} linux_link_channel_t;

static linux_link_channel_t channels[FMRB_LINK_MAX_CHANNELS];
static bool link_initialized = false;
static int global_socket_fd = -1;
static fmrb_semaphore_t socket_mutex = NULL;

static const char *TAG = "fmrb_hal_link";

static void* linux_link_thread(void *arg) {
    fmrb_link_channel_t channel = (fmrb_link_channel_t)(uintptr_t)arg;
    linux_link_channel_t *ch = &channels[channel];

    while (ch->running) {
        uint8_t buffer[1024];
        ssize_t received = recv(ch->socket_fd, buffer, sizeof(buffer), 0);

        if (received > 0 && ch->callback) {
            fmrb_link_message_t msg = {
                .data = buffer,
                .size = received
            };
            ch->callback(channel, &msg, ch->user_data);
        }

        if (received <= 0) {
            fmrb_hal_time_delay_ms(10);
        }
    }

    return NULL;
}

static fmrb_err_t connect_to_socket(void) {
    if (global_socket_fd >= 0) {
        return FMRB_OK; // Already connected
    }

    global_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (global_socket_fd == -1) {
        ESP_LOGE(TAG, "Failed to create socket: %s", strerror(errno));
        return FMRB_ERR_FAILED;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Try to connect with retry
    int retry_count = 0;
    while (retry_count < 10) {
        if (connect(global_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            ESP_LOGI(TAG, "Connected to %s", SOCKET_PATH);
            return FMRB_OK;
        }

        if (errno != ENOENT && errno != ECONNREFUSED) {
            ESP_LOGE(TAG, "Failed to connect: %s", strerror(errno));
            close(global_socket_fd);
            global_socket_fd = -1;
            return FMRB_ERR_FAILED;
        }

        usleep(100000); // Wait 100ms
        retry_count++;
    }

    ESP_LOGE(TAG, "Failed to connect after retries");
    close(global_socket_fd);
    global_socket_fd = -1;
    return FMRB_ERR_FAILED;
}

fmrb_err_t fmrb_hal_link_init(void) {
    if (link_initialized) {
        return FMRB_OK;
    }

    // Create mutex for socket access
    socket_mutex = fmrb_semaphore_create_mutex();
    if (socket_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create socket mutex");
        return FMRB_ERR_NO_MEMORY;
    }

    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        channels[i].socket_fd = -1;
        channels[i].callback = NULL;
        channels[i].user_data = NULL;
        channels[i].running = false;
    }

    // Connect to socket server
    fmrb_err_t err = connect_to_socket();
    if (err != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to connect to socket server");
        fmrb_semaphore_delete(socket_mutex);
        socket_mutex = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Linux IPC initialized");
    link_initialized = true;
    return FMRB_OK;
}

void fmrb_hal_link_deinit(void) {
    if (!link_initialized) {
        return;
    }

    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        if (channels[i].running) {
            channels[i].running = false;
            // Wait for task to finish
            while (eTaskGetState(channels[i].thread) != eDeleted) {
                fmrb_task_delay(FMRB_MS_TO_TICKS(10));
            }
        }

        if (channels[i].socket_fd >= 0) {
            close(channels[i].socket_fd);
            channels[i].socket_fd = -1;
        }
    }

    if (global_socket_fd >= 0) {
        close(global_socket_fd);
        global_socket_fd = -1;
    }

    if (socket_mutex != NULL) {
        fmrb_semaphore_delete(socket_mutex);
        socket_mutex = NULL;
    }

    ESP_LOGI(TAG, "Linux IPC deinitialized");
    link_initialized = false;
}

fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                              const fmrb_link_message_t *msg,
                              uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (global_socket_fd < 0) {
        ESP_LOGE(TAG, "Socket not connected");
        return FMRB_ERR_FAILED;
    }

    fmrb_semaphore_take(socket_mutex, FMRB_TICK_MAX);

    // The message already contains [frame_hdr | payload]
    // We need to add CRC32 and COBS encode

    // Prepare buffer: [data | CRC32]
    size_t total_size = msg->size + sizeof(uint32_t);
    uint8_t *buffer = (uint8_t*)fmrb_sys_malloc(total_size);
    if (!buffer) {
        fmrb_semaphore_give(socket_mutex);
        return FMRB_ERR_NO_MEMORY;
    }

    memcpy(buffer, msg->data, msg->size);
    uint32_t crc = fmrb_link_crc32_update(0, msg->data, msg->size);
    memcpy(buffer + msg->size, &crc, sizeof(uint32_t));

    // COBS encode
    size_t max_encoded_size = COBS_ENC_MAX(total_size);
    uint8_t *encoded = (uint8_t*)fmrb_sys_malloc(max_encoded_size);
    if (!encoded) {
        fmrb_sys_free(buffer);
        fmrb_semaphore_give(socket_mutex);
        return FMRB_ERR_NO_MEMORY;
    }

    size_t encoded_len = fmrb_link_cobs_encode(buffer, total_size, encoded);
    fmrb_sys_free(buffer);

    // Send encoded data
    ssize_t sent = send(global_socket_fd, encoded, encoded_len, 0);

    ESP_LOGD(TAG, "Sent %zu bytes (payload+crc: %zu, encoded: %zu) to channel %d", msg->size, total_size, encoded_len, channel);

    fmrb_sys_free(encoded);

    fmrb_semaphore_give(socket_mutex);

    if (sent != (ssize_t)encoded_len) {
        ESP_LOGE(TAG, "Failed to send data: %s", strerror(errno));
        return FMRB_ERR_FAILED;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_receive(fmrb_link_channel_t channel,
                                 fmrb_link_message_t *msg,
                                 uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (global_socket_fd < 0) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(global_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Static receive buffer for COBS frames
    static uint8_t recv_buffer[4096];
    static size_t recv_pos = 0;

    // Only call recv() if buffer is empty or doesn't contain complete frame
    if (recv_pos == 0) {
        // Try to receive data
        ssize_t received = recv(global_socket_fd, recv_buffer + recv_pos, sizeof(recv_buffer) - recv_pos, 0);
        if (received <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return FMRB_ERR_TIMEOUT;
            }
            return FMRB_ERR_FAILED;
        }

        ESP_LOGD(TAG, "Received %zd bytes from socket", received);
        recv_pos += received;
    } else {
        ESP_LOGD(TAG, "Processing buffered data (recv_pos=%zu)", recv_pos);
    }

    // Skip leading null bytes (leftover frame terminators)
    while (recv_pos > 0 && recv_buffer[0] == 0x00) {
        memmove(recv_buffer, recv_buffer + 1, recv_pos - 1);
        recv_pos--;
    }

    if (recv_pos == 0) {
        return FMRB_ERR_NOT_FOUND;
    }

    // Look for COBS frame terminator (0x00)
    size_t frame_end = 0;
    while (frame_end < recv_pos && recv_buffer[frame_end] != 0x00) {
        frame_end++;
    }

    if (frame_end >= recv_pos) {
        // No complete frame yet
        ESP_LOGD(TAG, "No complete frame yet (recv_pos=%zu)", recv_pos);
        return FMRB_ERR_NOT_FOUND;
    }

    ESP_LOGD(TAG, "Found COBS frame: frame_end=%zu, recv_pos=%zu", frame_end, recv_pos);

    // Decode COBS frame
    static uint8_t decoded_buffer[4096];
    ssize_t decoded_len = fmrb_link_cobs_decode(recv_buffer, frame_end, decoded_buffer);
    if (decoded_len <= 0) {
        ESP_LOGE(TAG, "COBS decode failed: frame_len=%zu, decoded_len=%zd", frame_end, decoded_len);
        // Remove invalid frame
        size_t remaining = recv_pos - (frame_end + 1);
        if (remaining > 0) {
            memmove(recv_buffer, recv_buffer + frame_end + 1, remaining);
        }
        recv_pos = remaining;
        return FMRB_ERR_FAILED;
    }

    ESP_LOGD(TAG, "COBS decoded %zd bytes", decoded_len);

    // Debug: print header info
    if (decoded_len >= 14) {  // Minimum header size
        uint32_t magic = *(uint32_t*)decoded_buffer;
        uint8_t version = decoded_buffer[4];
        uint8_t msg_type = decoded_buffer[5];
        uint16_t sequence = *(uint16_t*)(decoded_buffer + 6);
        uint32_t payload_len = *(uint32_t*)(decoded_buffer + 8);
        uint32_t checksum = *(uint32_t*)(decoded_buffer + 12);
        ESP_LOGD(TAG, "Header: magic=0x%08x ver=%u type=0x%02x seq=%u plen=%u csum=0x%08x",
                 magic, version, msg_type, sequence, payload_len, checksum);
    }

    // Remove processed frame from buffer
    size_t remaining = recv_pos - (frame_end + 1);
    if (remaining > 0) {
        memmove(recv_buffer, recv_buffer + frame_end + 1, remaining);
    }
    recv_pos = remaining;

    // Return decoded data
    msg->data = decoded_buffer;
    msg->size = decoded_len;

    ESP_LOGD(TAG, "Received %zd bytes from channel %d", decoded_len, channel);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_register_callback(fmrb_link_channel_t channel,
                                           fmrb_link_callback_t callback,
                                           void *user_data) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !callback) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_link_channel_t *ch = &channels[channel];
    ch->callback = callback;
    ch->user_data = user_data;
    ch->running = true;

    // Create FreeRTOS task
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "link_rx_%d", channel);

    fmrb_base_type_t ret = fmrb_task_create(
        linux_link_thread,
        task_name,
        4096,  // stack size
        (void*)(uintptr_t)channel,
        5,     // priority
        &ch->thread
    );

    if (ret != FMRB_PASS) {
        ch->running = false;
        return FMRB_ERR_FAILED;
    }

    ESP_LOGI(TAG, "Linux IPC callback registered for channel %d", channel);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_unregister_callback(fmrb_link_channel_t channel) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_link_channel_t *ch = &channels[channel];
    if (ch->running) {
        ch->running = false;
        // Wait for task to finish
        while (eTaskGetState(ch->thread) != eDeleted) {
            fmrb_task_delay(FMRB_MS_TO_TICKS(10));
        }
    }
    ch->callback = NULL;
    ch->user_data = NULL;

    return FMRB_OK;
}

void* fmrb_hal_link_get_shared_memory(size_t size) {
    if (!link_initialized || size == 0) {
        return NULL;
    }

    void *ptr = fmrb_sys_malloc(size);
    ESP_LOGI(TAG, "Allocated shared memory: %p, size: %zu", ptr, size);
    return ptr;
}

void fmrb_hal_link_release_shared_memory(void *ptr) {
    if (ptr) {
        ESP_LOGI(TAG, "Released shared memory: %p", ptr);
        fmrb_sys_free(ptr);
    }
}