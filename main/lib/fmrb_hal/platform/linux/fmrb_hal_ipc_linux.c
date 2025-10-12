#include "../../fmrb_hal_ipc.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define SOCKET_PATH_PREFIX "/tmp/fmrb_ipc_"

typedef struct {
    int socket_fd;
    pthread_t thread;
    fmrb_ipc_callback_t callback;
    void *user_data;
    bool running;
} linux_ipc_channel_t;

static linux_ipc_channel_t channels[FMRB_IPC_MAX_CHANNELS];
static bool ipc_initialized = false;

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

    ESP_LOGI(TAG, "Linux IPC deinitialized");
    ipc_initialized = false;
}

fmrb_err_t fmrb_hal_ipc_send(fmrb_ipc_channel_t channel,
                              const fmrb_ipc_message_t *msg,
                              uint32_t timeout_ms) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    ESP_LOGI(TAG, "Linux IPC send %zu bytes to channel %d", msg->size, channel);
    // Simulate sending - would actually send via socket
    fmrb_hal_time_delay_ms(1);
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

    void *ptr = malloc(size);
    ESP_LOGI(TAG, "Allocated shared memory: %p, size: %zu", ptr, size);
    return ptr;
}

void fmrb_hal_ipc_release_shared_memory(void *ptr) {
    if (ptr) {
        ESP_LOGI(TAG, "Released shared memory: %p", ptr);
        free(ptr);
    }
}