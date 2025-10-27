#include "../../fmrb_hal_ipc.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define IPC_QUEUE_SIZE 10

typedef struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    fmrb_ipc_callback_t callback;
    void *user_data;
} esp32_ipc_channel_t;

static esp32_ipc_channel_t channels[FMRB_IPC_MAX_CHANNELS];
static bool ipc_initialized = false;

static const char *TAG = "fmrb_hal_ipc";

static void esp32_ipc_task(void *arg) {
    fmrb_ipc_channel_t channel = (fmrb_ipc_channel_t)(uintptr_t)arg;
    esp32_ipc_channel_t *ch = &channels[channel];

    fmrb_ipc_message_t msg;
    while (1) {
        if (xQueueReceive(ch->queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (ch->callback) {
                ch->callback(channel, &msg, ch->user_data);
            }

            // Free the message data
            if (msg.data) {
                fmrb_sys_free(msg.data);
            }
        }
    }
}

fmrb_err_t fmrb_hal_ipc_init(void) {
    if (ipc_initialized) {
        return FMRB_OK;
    }

    for (int i = 0; i < FMRB_IPC_MAX_CHANNELS; i++) {
        channels[i].queue = xQueueCreate(IPC_QUEUE_SIZE, sizeof(fmrb_ipc_message_t));
        if (!channels[i].queue) {
            return FMRB_ERR_NO_MEMORY;
        }

        channels[i].callback = NULL;
        channels[i].user_data = NULL;
        channels[i].task = NULL;
    }

    ESP_LOGI(TAG, "ESP32 IPC initialized");
    ipc_initialized = true;
    return FMRB_OK;
}

void fmrb_hal_ipc_deinit(void) {
    if (!ipc_initialized) {
        return;
    }

    for (int i = 0; i < FMRB_IPC_MAX_CHANNELS; i++) {
        if (channels[i].task) {
            vTaskDelete(channels[i].task);
            channels[i].task = NULL;
        }

        if (channels[i].queue) {
            vQueueDelete(channels[i].queue);
            channels[i].queue = NULL;
        }
    }

    ESP_LOGI(TAG, "ESP32 IPC deinitialized");
    ipc_initialized = false;
}

fmrb_err_t fmrb_hal_ipc_send(fmrb_ipc_channel_t channel,
                              const fmrb_ipc_message_t *msg,
                              uint32_t timeout_ms) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_ipc_channel_t *ch = &channels[channel];
    if (!ch->queue) {
        return FMRB_ERR_FAILED;
    }

    // Create a copy of the message
    fmrb_ipc_message_t msg_copy;
    msg_copy.size = msg->size;
    msg_copy.data = fmrb_sys_malloc(msg->size);
    if (!msg_copy.data) {
        return FMRB_ERR_NO_MEMORY;
    }
    memcpy(msg_copy.data, msg->data, msg->size);

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(ch->queue, &msg_copy, ticks) != pdTRUE) {
        fmrb_sys_free(msg_copy.data);
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_ipc_receive(fmrb_ipc_channel_t channel,
                                 fmrb_ipc_message_t *msg,
                                 uint32_t timeout_ms) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_ipc_channel_t *ch = &channels[channel];
    if (!ch->queue) {
        return FMRB_ERR_FAILED;
    }

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(ch->queue, msg, ticks) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_ipc_register_callback(fmrb_ipc_channel_t channel,
                                           fmrb_ipc_callback_t callback,
                                           void *user_data) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS || !callback) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_ipc_channel_t *ch = &channels[channel];
    ch->callback = callback;
    ch->user_data = user_data;

    BaseType_t result = xTaskCreate(esp32_ipc_task, "ipc_task", 2048,
                                   (void*)(uintptr_t)channel, 5, &ch->task);
    if (result != pdPASS) {
        return FMRB_ERR_FAILED;
    }

    ESP_LOGI(TAG, "ESP32 IPC callback registered for channel %d", channel);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_ipc_unregister_callback(fmrb_ipc_channel_t channel) {
    if (!ipc_initialized || channel >= FMRB_IPC_MAX_CHANNELS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_ipc_channel_t *ch = &channels[channel];
    if (ch->task) {
        vTaskDelete(ch->task);
        ch->task = NULL;
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