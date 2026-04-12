#include "fmrb_hal_link.h"
#include "fmrb_link_cobs.h"
#include "fmrb_mem.h"
#include "fmrb_pin_assign.h"
#include "uart_link_frame.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>

#define UART_PORT_NUM       UART_NUM_1
#define UART_BAUD_RATE      921600
#define UART_RX_BUF_SIZE    2048
#define UART_TX_BUF_SIZE    512
#define ACK_RECV_QUEUE_SIZE 32

// RX state machine states
typedef enum {
    RX_STATE_SYNC,      // Scanning for sync byte (0x55)
    RX_STATE_HEADER,    // Reading 6-byte header
    RX_STATE_DATA,      // Reading data_len bytes
    RX_STATE_CRC,       // Reading 2-byte CRC
} rx_state_t;

typedef struct {
    fmrb_link_callback_t callback;
    void *user_data;
    volatile bool ack_received;
    uint8_t expected_seq;
} uart_link_channel_t;

typedef struct {
    uint8_t *data;
    size_t size;
} ack_queue_entry_t;

static uart_link_channel_t channels[FMRB_LINK_MAX_CHANNELS];
static bool link_initialized = false;
static SemaphoreHandle_t uart_mutex = NULL;
static QueueHandle_t ack_recv_queue = NULL;

// TX buffer
static uint8_t s_tx_buf[UART_LINK_MAX_FRAME_SIZE];

// RX state machine
static rx_state_t s_rx_state = RX_STATE_SYNC;
static uint8_t s_rx_header[UART_LINK_HEADER_SIZE];
static uint8_t s_rx_data[UART_LINK_MAX_DATA];
static uint8_t s_rx_crc[UART_LINK_CRC_SIZE];
static size_t s_rx_pos = 0;
static uint16_t s_rx_data_len = 0;

static const char *TAG = "uart_link";

// Extract seq from msgpack [type, seq, sub_cmd, payload]
static uint8_t extract_seq_from_msgpack(const uint8_t *data, size_t len) {
    if (len < 3 || data[0] != 0x94) return 0;
    size_t pos = 1;
    if (data[pos] <= 0x7F) { pos += 1; }
    else if (data[pos] == 0xCC) { pos += 2; }
    else { return 0; }
    if (pos >= len) return 0;
    if (data[pos] <= 0x7F) return data[pos];
    if (data[pos] == 0xCC && pos + 1 < len) return data[pos + 1];
    return 0;
}

// Reset RX state machine
static void rx_reset(void) {
    s_rx_state = RX_STATE_SYNC;
    s_rx_pos = 0;
    s_rx_data_len = 0;
}

// Process a complete received frame (header + data + CRC validated)
static void process_received_frame(fmrb_link_channel_t channel,
                                   const uart_link_header_t *hdr,
                                   const uint8_t *data, uint16_t data_len) {
    uart_link_channel_t *ch = &channels[channel];

    if (hdr->ack_seq == 0 && hdr->status == UART_LINK_STS_BOOT) {
        return;
    }

    // With fire-and-forget sends, we no longer gate on expected_seq.
    // Transport layer handles seq matching for sync requests.

    if (hdr->status == UART_LINK_STS_RX_OK) {
        ESP_LOGD(TAG, "RX_OK for seq=%u", hdr->ack_seq);
        return;
    }

    if (hdr->status == UART_LINK_STS_CRC_ERR) {
        ESP_LOGW(TAG, "Slave reported CRC error for seq=%u", hdr->ack_seq);
        return;
    }

    // Final status: APP_OK or APP_ERR
    if (data_len > 0) {
        // Parse COBS messages from data
        size_t pos = 0;
        while (pos < data_len) {
            while (pos < data_len && data[pos] == 0x00) pos++;
            if (pos >= data_len) break;

            size_t frame_start = pos;
            while (pos < data_len && data[pos] != 0x00) pos++;
            size_t frame_len = pos - frame_start;

            uint8_t decoded[UART_LINK_MAX_DATA];
            ssize_t decoded_len = fmrb_link_cobs_decode(
                data + frame_start, frame_len, decoded);

            if (decoded_len <= 0) continue;

            // Skip EMPTY frame
            if (decoded_len >= 2 && decoded[0] == 0x94 && decoded[1] == 0x00) {
                continue;
            }

            // Queue for transport layer
            if (ack_recv_queue) {
                uint8_t *queued_data = (uint8_t*)fmrb_sys_malloc(decoded_len);
                if (queued_data) {
                    memcpy(queued_data, decoded, decoded_len);
                    ack_queue_entry_t entry = { .data = queued_data, .size = (size_t)decoded_len };
                    if (xQueueSend(ack_recv_queue, &entry, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "ACK queue full, dropping data");
                        fmrb_sys_free(queued_data);
                    }
                }
            }

            // Notify via callback
            if (ch->callback) {
                uint8_t *copy = (uint8_t*)fmrb_sys_malloc(decoded_len);
                if (copy) {
                    memcpy(copy, decoded, decoded_len);
                    fmrb_link_message_t ack_msg = {
                        .data = copy,
                        .size = (size_t)decoded_len
                    };
                    ch->callback(channel, &ack_msg, ch->user_data);
                }
            }
        }
    }

    if (hdr->ack_seq == ch->expected_seq) {
        ch->ack_received = true;
    }
    ESP_LOGD(TAG, "ACK final: seq=%u status=0x%02x data_len=%u",
             hdr->ack_seq, hdr->status, data_len);
}

// Feed one byte into RX state machine. Returns true if a complete frame was processed.
static bool rx_feed_byte(fmrb_link_channel_t channel, uint8_t byte) {
    switch (s_rx_state) {
    case RX_STATE_SYNC:
        if (byte == UART_LINK_SYNC_BYTE) {
            s_rx_state = RX_STATE_HEADER;
            s_rx_pos = 0;
        }
        return false;

    case RX_STATE_HEADER:
        s_rx_header[s_rx_pos++] = byte;
        if (s_rx_pos >= UART_LINK_HEADER_SIZE) {
            const uart_link_header_t *hdr = (const uart_link_header_t *)s_rx_header;
            if (hdr->magic != UART_LINK_MAGIC || hdr->data_len > UART_LINK_MAX_DATA) {
                ESP_LOGW(TAG, "Invalid header: magic=0x%02X data_len=%u",
                         hdr->magic, hdr->data_len);
                rx_reset();
                return false;
            }
            s_rx_data_len = hdr->data_len;
            s_rx_pos = 0;
            if (s_rx_data_len > 0) {
                s_rx_state = RX_STATE_DATA;
            } else {
                s_rx_state = RX_STATE_CRC;
            }
        }
        return false;

    case RX_STATE_DATA:
        s_rx_data[s_rx_pos++] = byte;
        if (s_rx_pos >= s_rx_data_len) {
            s_rx_pos = 0;
            s_rx_state = RX_STATE_CRC;
        }
        return false;

    case RX_STATE_CRC:
        s_rx_crc[s_rx_pos++] = byte;
        if (s_rx_pos >= UART_LINK_CRC_SIZE) {
            // Validate CRC: compute over header + data
            size_t crc_data_len = UART_LINK_HEADER_SIZE + s_rx_data_len;
            uint8_t crc_buf[UART_LINK_HEADER_SIZE + UART_LINK_MAX_DATA];
            memcpy(crc_buf, s_rx_header, UART_LINK_HEADER_SIZE);
            if (s_rx_data_len > 0) {
                memcpy(crc_buf + UART_LINK_HEADER_SIZE, s_rx_data, s_rx_data_len);
            }
            uint16_t expected_crc = uart_link_crc16(crc_buf, crc_data_len);
            uint16_t actual_crc = (uint16_t)s_rx_crc[0] | ((uint16_t)s_rx_crc[1] << 8);

            bool valid = (expected_crc == actual_crc);
            if (valid) {
                const uart_link_header_t *hdr = (const uart_link_header_t *)s_rx_header;
                process_received_frame(channel, hdr, s_rx_data, s_rx_data_len);
            } else {
                ESP_LOGW(TAG, "CRC mismatch: expected=0x%04X actual=0x%04X",
                         expected_crc, actual_crc);
            }

            rx_reset();
            return valid;
        }
        return false;
    }

    rx_reset();
    return false;
}

// Read available bytes from UART and feed into RX state machine
static bool poll_uart_rx(fmrb_link_channel_t channel, uint32_t timeout_ms) {
    uint8_t buf[64];
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    int len = uart_read_bytes(UART_PORT_NUM, buf, sizeof(buf), timeout_ticks);
    if (len <= 0) {
        return false;
    }

    bool frame_received = false;
    for (int i = 0; i < len; i++) {
        if (rx_feed_byte(channel, buf[i])) {
            frame_received = true;
        }
    }
    return frame_received;
}

// Send frame and wait for ACK (called with uart_mutex held)
static fmrb_err_t send_frame_and_wait_ack(fmrb_link_channel_t channel,
                                           uint8_t seq,
                                           const uint8_t *data, uint16_t data_len,
                                           uint32_t timeout_ms) {
    uart_link_channel_t *ch = &channels[channel];

    // Build and send frame
    size_t frame_size = uart_link_build_frame(s_tx_buf, seq, 0, 0, data, data_len);

    int written = uart_write_bytes(UART_PORT_NUM, s_tx_buf, frame_size);
    if (written < 0) {
        ESP_LOGE(TAG, "UART write failed");
        return FMRB_ERR_FAILED;
    }

    // Wait for TX to complete
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));

    // Reset ACK state
    ch->ack_received = false;
    ch->expected_seq = seq;

    // Wait for ACK response
    fmrb_time_t start_time = fmrb_hal_time_get_us();

    while (!ch->ack_received) {
        if (fmrb_hal_time_is_timeout(start_time, timeout_ms * 1000)) {
            ESP_LOGW(TAG, "ACK timeout after %u ms (seq=%u)", timeout_ms, seq);
            return FMRB_ERR_TIMEOUT;
        }

        poll_uart_rx(channel, 10);

        // Delay to allow IDLE task to run and feed WDT
        if (!ch->ack_received) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_init(void) {
    if (link_initialized) {
        return FMRB_OK;
    }

    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        channels[i].callback = NULL;
        channels[i].user_data = NULL;
        channels[i].ack_received = false;
        channels[i].expected_seq = 0;
    }

    uart_mutex = xSemaphoreCreateMutex();
    if (!uart_mutex) {
        ESP_LOGE(TAG, "Failed to create UART mutex");
        return FMRB_ERR_NO_MEMORY;
    }

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %d", err);
        vSemaphoreDelete(uart_mutex);
        uart_mutex = NULL;
        return FMRB_ERR_FAILED;
    }

    err = uart_set_pin(UART_PORT_NUM,
                       FMRB_PIN_GFX_UART_TX, FMRB_PIN_GFX_UART_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %d", err);
        vSemaphoreDelete(uart_mutex);
        uart_mutex = NULL;
        return FMRB_ERR_FAILED;
    }

    err = uart_driver_install(UART_PORT_NUM, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE,
                              0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %d", err);
        vSemaphoreDelete(uart_mutex);
        uart_mutex = NULL;
        return FMRB_ERR_FAILED;
    }

    // Flush any stale data from RX buffer (noise during boot)
    uart_flush_input(UART_PORT_NUM);

    // Create ACK receive queue
    ack_recv_queue = xQueueCreate(ACK_RECV_QUEUE_SIZE, sizeof(ack_queue_entry_t));
    if (!ack_recv_queue) {
        ESP_LOGE(TAG, "Failed to create ACK receive queue");
        uart_driver_delete(UART_PORT_NUM);
        vSemaphoreDelete(uart_mutex);
        uart_mutex = NULL;
        return FMRB_ERR_NO_MEMORY;
    }

    rx_reset();

    ESP_LOGI(TAG, "UART link initialized (TX=%d, RX=%d, %d bps)",
             FMRB_PIN_GFX_UART_TX, FMRB_PIN_GFX_UART_RX, UART_BAUD_RATE);

    link_initialized = true;
    return FMRB_OK;
}

void fmrb_hal_link_deinit(void) {
    if (!link_initialized) {
        return;
    }

    uart_driver_delete(UART_PORT_NUM);

    if (uart_mutex) {
        vSemaphoreDelete(uart_mutex);
        uart_mutex = NULL;
    }

    if (ack_recv_queue) {
        ack_queue_entry_t entry;
        while (xQueueReceive(ack_recv_queue, &entry, 0) == pdTRUE) {
            if (entry.data) fmrb_sys_free(entry.data);
        }
        vQueueDelete(ack_recv_queue);
        ack_recv_queue = NULL;
    }

    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        channels[i].callback = NULL;
        channels[i].user_data = NULL;
    }

    ESP_LOGI(TAG, "UART link deinitialized");
    link_initialized = false;
}

// Send frame without waiting for ACK (called with uart_mutex held)
static fmrb_err_t send_frame_no_ack(fmrb_link_channel_t channel,
                                     uint8_t seq,
                                     const uint8_t *data, uint16_t data_len) {
    // Drain stale RX data to prevent UART RX buffer overflow
    poll_uart_rx(channel, 0);

    size_t frame_size = uart_link_build_frame(s_tx_buf, seq, 0, 0, data, data_len);
    int written = uart_write_bytes(UART_PORT_NUM, s_tx_buf, frame_size);
    if (written < 0) {
        ESP_LOGE(TAG, "UART write failed (noack)");
        return FMRB_ERR_FAILED;
    }
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100));
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                              const fmrb_link_message_t *msgs,
                              size_t msg_count,
                              uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msgs || msg_count == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    uint8_t frame_data[UART_LINK_MAX_DATA];
    size_t frame_data_len = 0;
    uint8_t last_seq = 0;
    fmrb_err_t result = FMRB_OK;

    uint8_t cobs_buf[UART_LINK_MAX_DATA];

    for (size_t i = 0; i < msg_count; i++) {
        const fmrb_link_message_t *msg = &msgs[i];

        if (msg->size == 0 || msg->data == NULL) {
            ESP_LOGW(TAG, "Skipping empty message %zu/%zu (size=%zu data=%p)",
                     i, msg_count, msg->size, msg->data);
            continue;
        }

        uint8_t seq = extract_seq_from_msgpack(msg->data, msg->size);

        size_t cobs_len = fmrb_link_cobs_encode(msg->data, msg->size, cobs_buf);

        if (cobs_len == 2 && cobs_buf[0] == 0x01) {
            ESP_LOGW(TAG, "Empty COBS from msg %zu/%zu (msg_size=%zu, data[0..3]=%02X %02X %02X %02X)",
                     i, msg_count, msg->size,
                     msg->size > 0 ? msg->data[0] : 0,
                     msg->size > 1 ? msg->data[1] : 0,
                     msg->size > 2 ? msg->data[2] : 0,
                     msg->size > 3 ? msg->data[3] : 0);
        }

        if (frame_data_len + cobs_len > UART_LINK_MAX_DATA) {
            if (frame_data_len == 0) {
                ESP_LOGE(TAG, "COBS encoded data too large: %zu > %d",
                         cobs_len, UART_LINK_MAX_DATA);
                result = FMRB_ERR_INVALID_PARAM;
                break;
            }

            // Flush current frame
            result = send_frame_and_wait_ack(channel, last_seq,
                                             frame_data, (uint16_t)frame_data_len,
                                             timeout_ms);
            if (result != FMRB_OK) {
                break;
            }

            ESP_LOGD(TAG, "Batch frame sent: seq=%u, data_len=%zu", last_seq, frame_data_len);
            frame_data_len = 0;
        }

        memcpy(frame_data + frame_data_len, cobs_buf, cobs_len);
        frame_data_len += cobs_len;
        last_seq = seq;
    }

    // Send remaining data
    if (result == FMRB_OK && frame_data_len > 0) {
        result = send_frame_and_wait_ack(channel, last_seq,
                                         frame_data, (uint16_t)frame_data_len,
                                         timeout_ms);

        if (result == FMRB_OK) {
            ESP_LOGD(TAG, "Batch frame sent: seq=%u, data_len=%zu, msgs=%zu",
                     last_seq, frame_data_len, msg_count);
        }
    }

    xSemaphoreGive(uart_mutex);
    return result;
}

fmrb_err_t fmrb_hal_link_send_noack(fmrb_link_channel_t channel,
                                     const fmrb_link_message_t *msgs,
                                     size_t msg_count,
                                     uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msgs || msg_count == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (xSemaphoreTake(uart_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    uint8_t frame_data[UART_LINK_MAX_DATA];
    size_t frame_data_len = 0;
    uint8_t last_seq = 0;
    fmrb_err_t result = FMRB_OK;
    uint8_t cobs_buf[UART_LINK_MAX_DATA];

    for (size_t i = 0; i < msg_count; i++) {
        const fmrb_link_message_t *msg = &msgs[i];
        if (msg->size == 0 || msg->data == NULL) {
            continue;
        }

        uint8_t seq = extract_seq_from_msgpack(msg->data, msg->size);
        size_t cobs_len = fmrb_link_cobs_encode(msg->data, msg->size, cobs_buf);

        if (frame_data_len + cobs_len > UART_LINK_MAX_DATA) {
            if (frame_data_len == 0) {
                ESP_LOGE(TAG, "COBS encoded data too large: %zu > %d",
                         cobs_len, UART_LINK_MAX_DATA);
                result = FMRB_ERR_INVALID_PARAM;
                break;
            }
            result = send_frame_no_ack(channel, last_seq,
                                        frame_data, (uint16_t)frame_data_len);
            if (result != FMRB_OK) break;
            frame_data_len = 0;
        }

        memcpy(frame_data + frame_data_len, cobs_buf, cobs_len);
        frame_data_len += cobs_len;
        last_seq = seq;
    }

    if (result == FMRB_OK && frame_data_len > 0) {
        result = send_frame_no_ack(channel, last_seq,
                                    frame_data, (uint16_t)frame_data_len);
    }

    xSemaphoreGive(uart_mutex);
    return result;
}

fmrb_err_t fmrb_hal_link_receive(fmrb_link_channel_t channel,
                                 fmrb_link_message_t *msg,
                                 uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Actively poll UART RX to receive app-level responses.
    // With fire-and-forget sends, RX is no longer polled during send,
    // so we must poll here to pick up responses for sync requests.
    if (xSemaphoreTake(uart_mutex, 0) == pdTRUE) {
        poll_uart_rx(channel, 0);
        xSemaphoreGive(uart_mutex);
    }

    // Check ACK queue
    if (ack_recv_queue) {
        ack_queue_entry_t entry;
        if (xQueueReceive(ack_recv_queue, &entry, 0) == pdTRUE) {
            msg->data = entry.data;
            msg->size = entry.size;
            ESP_LOGD(TAG, "Dequeued ACK for transport: %zu bytes", entry.size);
            return FMRB_OK;
        }
    }

    return FMRB_ERR_TIMEOUT;
}

fmrb_err_t fmrb_hal_link_register_callback(fmrb_link_channel_t channel,
                                           fmrb_link_callback_t callback,
                                           void *user_data) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !callback) {
        return FMRB_ERR_INVALID_PARAM;
    }

    channels[channel].callback = callback;
    channels[channel].user_data = user_data;

    ESP_LOGI(TAG, "Callback registered for channel %d", channel);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_link_unregister_callback(fmrb_link_channel_t channel) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    channels[channel].callback = NULL;
    channels[channel].user_data = NULL;
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
