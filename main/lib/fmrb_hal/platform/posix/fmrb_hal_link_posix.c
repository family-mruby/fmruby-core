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

    ESP_LOGI(TAG, "Sent %zu bytes (payload+crc: %zu, encoded: %zu) to channel %d", msg->size, total_size, encoded_len, channel);

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

    ESP_LOGI(TAG, "Linux IPC receive from channel %d", channel);
    // Simulate receive - would actually receive via socket
    static uint8_t dummy_data[] = {0x01, 0x02, 0x03, 0x04};
    msg->data = dummy_data;
    msg->size = sizeof(dummy_data);
    fmrb_hal_time_delay_ms(1);
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