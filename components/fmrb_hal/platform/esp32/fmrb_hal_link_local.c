// HAL link implementation using FreeRTOS Message Buffers
// for local inter-task communication on the same ESP32-S3.
// Bidirectional: TX buffer (Core -> m5gfx) and RX buffer (m5gfx -> Core for ACK).

#include <string.h>
#include "fmrb_hal_link.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"

#include "freertos/message_buffer.h"

static const char *TAG = "hal_link_local";

#define LINK_LOCAL_TX_BUFFER_SIZE (8 * 1024)   // Core -> m5gfx (commands)
#define LINK_LOCAL_RX_BUFFER_SIZE (2 * 1024)   // m5gfx -> Core (ACK responses)
#define LINK_LOCAL_RECV_BUF_SIZE  (4096)

typedef struct {
    MessageBufferHandle_t tx_buffer;   // Core sends, m5gfx receives
    MessageBufferHandle_t rx_buffer;   // m5gfx sends (ACK), Core receives
    SemaphoreHandle_t send_mutex;
    fmrb_link_callback_t callback;
    void *callback_user_data;
    TaskHandle_t recv_task;
    bool initialized;
} link_local_channel_t;

static link_local_channel_t g_channels[FMRB_LINK_MAX_CHANNELS];

// Callback dispatch task: receives from TX buffer and invokes callback
static void link_local_recv_task(void *arg)
{
    int ch_idx = (int)(uintptr_t)arg;
    link_local_channel_t *ch = &g_channels[ch_idx];
    static uint8_t recv_buf[LINK_LOCAL_RECV_BUF_SIZE];

    FMRB_LOGI(TAG, "Callback dispatch task started for channel %d", ch_idx);

    while (1) {
        size_t received = xMessageBufferReceive(ch->tx_buffer, recv_buf,
                                                 sizeof(recv_buf),
                                                 FMRB_MS_TO_TICKS(1000));
        if (received > 0 && ch->callback) {
            fmrb_link_message_t msg = {
                .data = recv_buf,
                .size = received,
            };
            ch->callback((fmrb_link_channel_t)ch_idx, &msg, ch->callback_user_data);
        }
    }
}

fmrb_err_t fmrb_hal_link_init(void)
{
    FMRB_LOGI(TAG, "Initializing local link (Message Buffer)");

    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        link_local_channel_t *ch = &g_channels[i];
        memset(ch, 0, sizeof(*ch));

        ch->tx_buffer = xMessageBufferCreate(LINK_LOCAL_TX_BUFFER_SIZE);
        if (!ch->tx_buffer) {
            FMRB_LOGE(TAG, "Failed to create TX buffer for channel %d", i);
            fmrb_hal_link_deinit();
            return FMRB_ERR_NO_MEMORY;
        }

        ch->rx_buffer = xMessageBufferCreate(LINK_LOCAL_RX_BUFFER_SIZE);
        if (!ch->rx_buffer) {
            FMRB_LOGE(TAG, "Failed to create RX buffer for channel %d", i);
            fmrb_hal_link_deinit();
            return FMRB_ERR_NO_MEMORY;
        }

        ch->send_mutex = fmrb_semaphore_create_mutex();
        if (!ch->send_mutex) {
            FMRB_LOGE(TAG, "Failed to create mutex for channel %d", i);
            fmrb_hal_link_deinit();
            return FMRB_ERR_NO_MEMORY;
        }

        ch->initialized = true;
        FMRB_LOGI(TAG, "Channel %d initialized (tx=%uB, rx=%uB)",
                   i, LINK_LOCAL_TX_BUFFER_SIZE, LINK_LOCAL_RX_BUFFER_SIZE);
    }

    return FMRB_OK;
}

void fmrb_hal_link_deinit(void)
{
    FMRB_LOGI(TAG, "Deinitializing local link");

    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        link_local_channel_t *ch = &g_channels[i];

        if (ch->recv_task) {
            fmrb_task_delete(ch->recv_task);
            ch->recv_task = NULL;
        }
        if (ch->tx_buffer) {
            vMessageBufferDelete(ch->tx_buffer);
            ch->tx_buffer = NULL;
        }
        if (ch->rx_buffer) {
            vMessageBufferDelete(ch->rx_buffer);
            ch->rx_buffer = NULL;
        }
        if (ch->send_mutex) {
            fmrb_semaphore_delete(ch->send_mutex);
            ch->send_mutex = NULL;
        }
        ch->initialized = false;
    }
}

// send: Core -> m5gfx (via TX buffer)
fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                               const fmrb_link_message_t *msg,
                               uint32_t timeout_ms)
{
    if (channel >= FMRB_LINK_MAX_CHANNELS || !msg || !msg->data) {
        return FMRB_ERR_INVALID_PARAM;
    }

    link_local_channel_t *ch = &g_channels[channel];
    if (!ch->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_semaphore_take(ch->send_mutex, FMRB_TICK_MAX);

    size_t sent = xMessageBufferSend(ch->tx_buffer, msg->data, msg->size,
                                      FMRB_MS_TO_TICKS(timeout_ms));

    fmrb_semaphore_give(ch->send_mutex);

    if (sent == 0) {
        FMRB_LOGW(TAG, "Send timeout on channel %d (size=%zu)", channel, msg->size);
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}

// receive: Core reads ACK from m5gfx (via RX buffer)
// Internal buffer for callers that don't provide their own
static uint8_t g_recv_internal_buf[LINK_LOCAL_RECV_BUF_SIZE];

fmrb_err_t fmrb_hal_link_receive(fmrb_link_channel_t channel,
                                  fmrb_link_message_t *msg,
                                  uint32_t timeout_ms)
{
    if (channel >= FMRB_LINK_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    link_local_channel_t *ch = &g_channels[channel];
    if (!ch->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Always use internal buffer for safety.
    // Callers like fmrb_link_transport_process() may pass uninitialized msg->data.
    uint8_t *recv_buf = g_recv_internal_buf;
    size_t recv_buf_size = sizeof(g_recv_internal_buf);

    size_t received = xMessageBufferReceive(ch->rx_buffer, recv_buf, recv_buf_size,
                                             FMRB_MS_TO_TICKS(timeout_ms));
    if (received == 0) {
        return FMRB_ERR_TIMEOUT;
    }

    msg->data = recv_buf;
    msg->size = received;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_register_callback(fmrb_link_channel_t channel,
                                            fmrb_link_callback_t callback,
                                            void *user_data)
{
    if (channel >= FMRB_LINK_MAX_CHANNELS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    link_local_channel_t *ch = &g_channels[channel];
    if (!ch->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    ch->callback = callback;
    ch->callback_user_data = user_data;

    if (!ch->recv_task && callback) {
        char name[16];
        snprintf(name, sizeof(name), "link_cb_%d", channel);

        fmrb_base_type_t result = fmrb_task_create_ex(
            link_local_recv_task,
            name,
            FMRB_M5GFX_TASK_STACK_SIZE,
            (void *)(uintptr_t)channel,
            FMRB_M5GFX_TASK_PRIORITY,
            &ch->recv_task,
            FMRB_M5GFX_TASK_FLAGS
        );

        if (result != FMRB_PASS) {
            FMRB_LOGE(TAG, "Failed to create callback task for channel %d", channel);
            ch->callback = NULL;
            return FMRB_ERR_FAILED;
        }
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_unregister_callback(fmrb_link_channel_t channel)
{
    if (channel >= FMRB_LINK_MAX_CHANNELS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    link_local_channel_t *ch = &g_channels[channel];

    if (ch->recv_task) {
        fmrb_task_delete(ch->recv_task);
        ch->recv_task = NULL;
    }
    ch->callback = NULL;
    ch->callback_user_data = NULL;

    return FMRB_OK;
}

void *fmrb_hal_link_get_shared_memory(size_t size)
{
    return fmrb_sys_malloc(size);
}

void fmrb_hal_link_release_shared_memory(void *ptr)
{
    fmrb_sys_free(ptr);
}

// ============================================================
// Local link extended API (for m5gfx_task to read commands and send ACK)
// ============================================================

// m5gfx reads commands from TX buffer
fmrb_err_t fmrb_hal_link_local_receive_cmd(fmrb_link_channel_t channel,
                                            fmrb_link_message_t *msg,
                                            uint32_t timeout_ms)
{
    if (channel >= FMRB_LINK_MAX_CHANNELS || !msg || !msg->data) {
        return FMRB_ERR_INVALID_PARAM;
    }

    link_local_channel_t *ch = &g_channels[channel];
    if (!ch->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    size_t received = xMessageBufferReceive(ch->tx_buffer, msg->data, msg->size,
                                             FMRB_MS_TO_TICKS(timeout_ms));
    if (received == 0) {
        return FMRB_ERR_TIMEOUT;
    }

    msg->size = received;
    return FMRB_OK;
}

// m5gfx sends ACK/response to Core via RX buffer
fmrb_err_t fmrb_hal_link_local_send_response(fmrb_link_channel_t channel,
                                              const fmrb_link_message_t *msg,
                                              uint32_t timeout_ms)
{
    if (channel >= FMRB_LINK_MAX_CHANNELS || !msg || !msg->data) {
        return FMRB_ERR_INVALID_PARAM;
    }

    link_local_channel_t *ch = &g_channels[channel];
    if (!ch->initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    size_t sent = xMessageBufferSend(ch->rx_buffer, msg->data, msg->size,
                                      FMRB_MS_TO_TICKS(timeout_ms));
    if (sent == 0) {
        FMRB_LOGW(TAG, "Response send timeout on channel %d", channel);
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}
