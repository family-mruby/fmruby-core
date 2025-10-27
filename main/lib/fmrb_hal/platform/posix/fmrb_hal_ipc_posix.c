#include "../../fmrb_hal_ipc.h"
#include "../../../fmrb_ipc/fmrb_ipc_cobs.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>

#define SOCKET_PATH "/tmp/fmrb_socket"

typedef struct {
    int socket_fd;
    pthread_t thread;
    fmrb_ipc_callback_t callback;
    void *user_data;
    bool running;
} linux_ipc_channel_t;

static linux_ipc_channel_t channels[FMRB_IPC_MAX_CHANNELS];
static bool ipc_initialized = false;
static int global_socket_fd = -1;
static pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *TAG = "fmrb_hal_ipc";

static void* linux_ipc_thread(void *arg) {
    fmrb_ipc_channel_t channel = (fmrb_ipc_channel_t)(uintptr_t)arg;
    linux_ipc_channel_t *ch = &channels[channel];

    while (ch->running) {
        uint8_t buffer[1024];
        ssize_t received = recv(ch->socket_fd, buffer, sizeof(buffer), 0);

        if (received > 0 && ch->callback) {
            fmrb_ipc_message_t msg = {
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

fmrb_err_t fmrb_hal_ipc_init(void) {
    if (ipc_initialized) {
        return FMRB_OK;
    }

    for (int i = 0; i < FMRB_IPC_MAX_CHANNELS; i++) {
        channels[i].socket_fd = -1;
        channels[i].callback = NULL;
        channels[i].user_data = NULL;
        channels[i].running = false;
    }

    // Connect to socket server
    fmrb_err_t err = connect_to_socket();
    if (err != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to connect to socket server");
        return err;
    }

    ESP_LOGI(TAG, "Linux IPC initialized");
    ipc_initialized = true;
    return FMRB_OK;
}

void fmrb_hal_ipc_deinit(void) {
    if (!ipc_initialized) {
        return;
    }

    for (int i = 0; i < FMRB_IPC_MAX_CHANNELS; i++) {
        if (channels[i].running) {
            channels[i].running = false;
            pthread_join(channels[i].thread, NULL);
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

    ESP_LOGI(TAG, "Linux IPC deinitialized");
    ipc_initialized = false;
}

fmrb_err_t fmrb_hal_ipc_send(fmrb_ipc_channel_t channel,
                              const fmrb_ipc_message_t *msg,
                              uint32_t timeout_ms) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (global_socket_fd < 0) {
        ESP_LOGE(TAG, "Socket not connected");
        return FMRB_ERR_FAILED;
    }

    pthread_mutex_lock(&socket_mutex);

    // The message already contains [frame_hdr | payload]
    // We need to add CRC32 and COBS encode

    // Prepare buffer: [data | CRC32]
    size_t total_size = msg->size + sizeof(uint32_t);
    uint8_t *buffer = (uint8_t*)fmrb_sys_malloc(total_size);
    if (!buffer) {
        pthread_mutex_unlock(&socket_mutex);
        return FMRB_ERR_NO_MEMORY;
    }

    memcpy(buffer, msg->data, msg->size);
    uint32_t crc = fmrb_ipc_crc32_update(0, msg->data, msg->size);
    memcpy(buffer + msg->size, &crc, sizeof(uint32_t));

    // COBS encode
    size_t max_encoded_size = COBS_ENC_MAX(total_size);
    uint8_t *encoded = (uint8_t*)fmrb_sys_malloc(max_encoded_size);
    if (!encoded) {
        fmrb_sys_free(buffer);
        pthread_mutex_unlock(&socket_mutex);
        return FMRB_ERR_NO_MEMORY;
    }

    size_t encoded_len = fmrb_ipc_cobs_encode(buffer, total_size, encoded);
    fmrb_sys_free(buffer);

    // Send encoded data
    ssize_t sent = send(global_socket_fd, encoded, encoded_len, 0);

    ESP_LOGI(TAG, "Sent %zu bytes (payload+crc: %zu, encoded: %zu) to channel %d", msg->size, total_size, encoded_len, channel);

    fmrb_sys_free(encoded);

    pthread_mutex_unlock(&socket_mutex);

    if (sent != (ssize_t)encoded_len) {
        ESP_LOGE(TAG, "Failed to send data: %s", strerror(errno));
        return FMRB_ERR_FAILED;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_ipc_receive(fmrb_ipc_channel_t channel,
                                 fmrb_ipc_message_t *msg,
                                 uint32_t timeout_ms) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !msg) {
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

fmrb_err_t fmrb_hal_ipc_register_callback(fmrb_ipc_channel_t channel,
                                           fmrb_ipc_callback_t callback,
                                           void *user_data) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !callback) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_ipc_channel_t *ch = &channels[channel];
    ch->callback = callback;
    ch->user_data = user_data;
    ch->running = true;

    if (pthread_create(&ch->thread, NULL, linux_ipc_thread, (void*)(uintptr_t)channel) != 0) {
        return FMRB_ERR_FAILED;
    }

    ESP_LOGI(TAG, "Linux IPC callback registered for channel %d", channel);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_ipc_unregister_callback(fmrb_ipc_channel_t channel) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_ipc_channel_t *ch = &channels[channel];
    if (ch->running) {
        ch->running = false;
        pthread_join(ch->thread, NULL);
    }
    ch->callback = NULL;
    ch->user_data = NULL;

    return FMRB_OK;
}

void* fmrb_hal_ipc_get_shared_memory(size_t size) {
    if (!ipc_initialized || size == 0) {
        return NULL;
    }

    void *ptr = fmrb_sys_malloc(size);
    ESP_LOGI(TAG, "Allocated shared memory: %p, size: %zu", ptr, size);
    return ptr;
}

void fmrb_hal_ipc_release_shared_memory(void *ptr) {
    if (ptr) {
        ESP_LOGI(TAG, "Released shared memory: %p", ptr);
        fmrb_sys_free(ptr);
    }
}