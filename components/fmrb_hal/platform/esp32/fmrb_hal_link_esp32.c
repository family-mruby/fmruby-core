#include "fmrb_hal_link.h"
#include "fmrb_hal_spi.h"
#include "fmrb_hal_gpio.h"
#include "fmrb_link_cobs.h"
#include "fmrb_mem.h"
#include "fmrb_pin_assign.h"
#include "spi_frame.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_heap_caps.h"

// DMA-capable SPI buffers (shared, protected by spi_mutex)
static uint8_t *s_tx_dma_buf = NULL;
static uint8_t *s_rx_dma_buf = NULL;

#define SPI_FREQUENCY (10 * 1000 * 1000)  // 10MHz
#define ACK_RECV_QUEUE_SIZE 32

typedef struct {
    fmrb_link_callback_t callback;
    void *user_data;
    volatile bool ack_received;  // ACK reception flag for synchronous send
    uint8_t expected_seq;         // Expected sequence number for ACK
} esp32_link_channel_t;

// ACK receive queue entry: bridges HAL-level ACK reception to transport layer
typedef struct {
    uint8_t *data;
    size_t size;
} ack_queue_entry_t;

static esp32_link_channel_t channels[FMRB_LINK_MAX_CHANNELS];
static bool link_initialized = false;
static fmrb_spi_handle_t spi_handle = NULL;
static SemaphoreHandle_t spi_mutex = NULL;
static QueueHandle_t ack_recv_queue = NULL;
static SemaphoreHandle_t ack_notify_sem = NULL;  // READY rising edge notification

static const char *TAG = "fmrb_hal_link";

// Forward declarations
static void process_received_frame(fmrb_link_channel_t channel, const spi_frame_t *rx);
static fmrb_err_t poll_for_ack(fmrb_link_channel_t channel, uint32_t timeout_ms);

// Extract seq from msgpack [type, seq, sub_cmd, payload]
// msgpack fixarray(4) = 0x94, then type, then seq
static uint8_t extract_seq_from_msgpack(const uint8_t *data, size_t len) {
    if (len < 3 || data[0] != 0x94) return 0;
    size_t pos = 1;
    // Skip type field
    if (data[pos] <= 0x7F) { pos += 1; }       // positive fixint
    else if (data[pos] == 0xCC) { pos += 2; }   // uint8
    else { return 0; }
    // Read seq field
    if (pos >= len) return 0;
    if (data[pos] <= 0x7F) return data[pos];
    if (data[pos] == 0xCC && pos + 1 < len) return data[pos + 1];
    return 0;
}

// GPIO ISR: called on rising edge of READY pin (slave has queued slots)
static void IRAM_ATTR ready_gpio_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(ack_notify_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Wait for READY=HIGH (with timeout)
static bool wait_ready(uint32_t timeout_ms) {
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        if (fmrb_hal_gpio_get_level(FMRB_PIN_GFX_SPI_INTR) == 1) {
            return true;
        }
        if (ack_notify_sem) {
            xSemaphoreTake(ack_notify_sem, pdMS_TO_TICKS(10));
        } else {
            fmrb_hal_time_delay_ms(1);
        }
    }
    return false;
}

// SPI transfer: send/receive spi_frame_t via DMA buffers
static fmrb_err_t spi_transfer_frame(const spi_frame_t *tx, spi_frame_t *rx) {
    memcpy(s_tx_dma_buf, tx, FMRB_LINK_FRAME_SIZE);
    memset(s_rx_dma_buf, 0, FMRB_LINK_FRAME_SIZE);

    fmrb_err_t ret = fmrb_hal_spi_transfer(spi_handle, s_tx_dma_buf, s_rx_dma_buf,
                                             FMRB_LINK_FRAME_SIZE, 100);
    if (ret == FMRB_OK && rx) {
        memcpy(rx, s_rx_dma_buf, FMRB_LINK_FRAME_SIZE);
    }
    return ret;
}

// Build a command frame with COBS payload in data[]
static void build_command_frame(spi_frame_t *f, uint8_t seq,
                                const uint8_t *cobs_data, uint8_t cobs_len) {
    memset(f, 0, FMRB_LINK_FRAME_SIZE);
    f->magic = SPI_FRAME_MAGIC;
    f->seq = seq;
    f->ack_seq = 0;
    f->status = 0;
    f->data_len = cobs_len;
    if (cobs_data && cobs_len > 0) {
        memcpy(f->data, cobs_data, cobs_len);
    }
    spi_frame_finalize(f);
}

fmrb_err_t fmrb_hal_link_init(void) {
    if (link_initialized) {
        return FMRB_OK;
    }

    // Initialize channels
    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        channels[i].callback = NULL;
        channels[i].user_data = NULL;
        channels[i].ack_received = false;
        channels[i].expected_seq = 0;
    }

    // Create SPI mutex
    spi_mutex = xSemaphoreCreateMutex();
    if (!spi_mutex) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return FMRB_ERR_NO_MEMORY;
    }

    // Initialize SPI Master
    fmrb_spi_config_t spi_config = {
        .mosi_pin = FMRB_PIN_GFX_SPI_MOSI,
        .miso_pin = FMRB_PIN_GFX_SPI_MISO,
        .sclk_pin = FMRB_PIN_GFX_SPI_SCLK,
        .cs_pin = FMRB_PIN_GFX_SPI_CS,
        .frequency = SPI_FREQUENCY
    };

    fmrb_err_t ret = fmrb_hal_spi_init(&spi_config, &spi_handle);
    if (ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI Master: %d", ret);
        vSemaphoreDelete(spi_mutex);
        spi_mutex = NULL;
        return ret;
    }

    // Create ACK receive queue (bridges HAL ACK polling to transport layer)
    ack_recv_queue = xQueueCreate(ACK_RECV_QUEUE_SIZE, sizeof(ack_queue_entry_t));
    if (!ack_recv_queue) {
        ESP_LOGE(TAG, "Failed to create ACK receive queue");
        fmrb_hal_spi_deinit(spi_handle);
        spi_handle = NULL;
        vSemaphoreDelete(spi_mutex);
        spi_mutex = NULL;
        return FMRB_ERR_NO_MEMORY;
    }

    // Initialize READY GPIO as input
    fmrb_hal_gpio_config(FMRB_PIN_GFX_SPI_INTR, FMRB_GPIO_MODE_INPUT, FMRB_GPIO_PULL_NONE);

    // Create READY notification semaphore
    ack_notify_sem = xSemaphoreCreateBinary();
    if (!ack_notify_sem) {
        ESP_LOGE(TAG, "Failed to create READY notify semaphore");
    } else {
        // Install GPIO ISR service (ignore if already installed)
        esp_err_t isr_ret = gpio_install_isr_service(0);
        if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %d", isr_ret);
        }

        // Configure rising-edge interrupt on READY pin (HIGH = slave ready)
        gpio_set_intr_type(FMRB_PIN_GFX_SPI_INTR, GPIO_INTR_POSEDGE);
        gpio_isr_handler_add(FMRB_PIN_GFX_SPI_INTR, ready_gpio_isr_handler, NULL);
        ESP_LOGI(TAG, "READY GPIO ISR registered on pin %d (rising edge)", FMRB_PIN_GFX_SPI_INTR);
    }

    // Allocate DMA-capable SPI buffers
    s_tx_dma_buf = (uint8_t *)heap_caps_malloc(FMRB_LINK_FRAME_SIZE, MALLOC_CAP_DMA);
    s_rx_dma_buf = (uint8_t *)heap_caps_malloc(FMRB_LINK_FRAME_SIZE, MALLOC_CAP_DMA);
    if (!s_tx_dma_buf || !s_rx_dma_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffers");
        heap_caps_free(s_tx_dma_buf);
        heap_caps_free(s_rx_dma_buf);
        s_tx_dma_buf = NULL;
        s_rx_dma_buf = NULL;
        fmrb_hal_spi_deinit(spi_handle);
        spi_handle = NULL;
        vSemaphoreDelete(spi_mutex);
        spi_mutex = NULL;
        return FMRB_ERR_NO_MEMORY;
    }

    ESP_LOGI(TAG, "ESP32 SPI link initialized (MOSI=%d, MISO=%d, SCLK=%d, CS=%d, READY=%d, %dMHz)",
             spi_config.mosi_pin, spi_config.miso_pin, spi_config.sclk_pin,
             spi_config.cs_pin, FMRB_PIN_GFX_SPI_INTR, spi_config.frequency / 1000000);

    link_initialized = true;
    return FMRB_OK;
}

void fmrb_hal_link_deinit(void) {
    if (!link_initialized) {
        return;
    }

    if (spi_handle) {
        fmrb_hal_spi_deinit(spi_handle);
        spi_handle = NULL;
    }

    if (spi_mutex) {
        vSemaphoreDelete(spi_mutex);
        spi_mutex = NULL;
    }

    if (s_tx_dma_buf) {
        heap_caps_free(s_tx_dma_buf);
        s_tx_dma_buf = NULL;
    }
    if (s_rx_dma_buf) {
        heap_caps_free(s_rx_dma_buf);
        s_rx_dma_buf = NULL;
    }

    if (ack_notify_sem) {
        gpio_isr_handler_remove(FMRB_PIN_GFX_SPI_INTR);
        vSemaphoreDelete(ack_notify_sem);
        ack_notify_sem = NULL;
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

    ESP_LOGI(TAG, "ESP32 SPI link deinitialized");
    link_initialized = false;
}

// Send one SPI frame and poll for ACK (called with spi_mutex held)
static fmrb_err_t send_frame_and_wait_ack(fmrb_link_channel_t channel,
                                           const spi_frame_t *tx_frame,
                                           uint8_t seq,
                                           uint32_t timeout_ms) {
    spi_frame_t rx;

    // Wait for READY=HIGH before sending
    if (!wait_ready(timeout_ms)) {
        return FMRB_ERR_TIMEOUT;
    }

    // Clear stale READY notification
    if (ack_notify_sem) {
        xSemaphoreTake(ack_notify_sem, 0);
    }

    // SPI transfer
    fmrb_err_t ret = spi_transfer_frame(tx_frame, &rx);

    // Reset ACK received flag
    esp32_link_channel_t *ch = &channels[channel];
    ch->ack_received = false;
    ch->expected_seq = seq;

    // Process RX frame (may contain response to previous command)
    if (ret == FMRB_OK) {
        process_received_frame(channel, &rx);
    } else {
        ESP_LOGE(TAG, "SPI transfer failed: %d", ret);
        return FMRB_ERR_FAILED;
    }

    // Poll for matching ACK if not yet received
    if (!ch->ack_received && timeout_ms > 0) {
        fmrb_time_t start_time = fmrb_hal_time_get_us();

        while (!ch->ack_received) {
            if (fmrb_hal_time_is_timeout(start_time, timeout_ms * 1000)) {
                ESP_LOGW(TAG, "ACK timeout after %u ms (seq=%u)", timeout_ms, seq);
                return FMRB_ERR_TIMEOUT;
            }

            uint64_t elapsed_us = fmrb_hal_time_get_us() - start_time;
            uint32_t remaining_ms = (elapsed_us / 1000 < timeout_ms) ?
                                    (timeout_ms - (uint32_t)(elapsed_us / 1000)) : 1;

            if (!wait_ready(remaining_ms < 50 ? remaining_ms : 50)) {
                continue;
            }

            poll_for_ack(channel, 10);

            if (!ch->ack_received) {
                fmrb_hal_time_delay_ms(2);
            }
        }
    }

    return ch->ack_received ? FMRB_OK : FMRB_ERR_TIMEOUT;
}

fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                              const fmrb_link_message_t *msgs,
                              size_t msg_count,
                              uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msgs || msg_count == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (!spi_handle) {
        ESP_LOGE(TAG, "SPI not initialized");
        return FMRB_ERR_FAILED;
    }

    // Take SPI mutex
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    // Accumulate COBS-encoded messages into frame data buffer
    uint8_t frame_data[FMRB_LINK_FRAME_MAX_DATA];
    size_t frame_data_len = 0;
    uint8_t last_seq = 0;
    fmrb_err_t result = FMRB_OK;

    // Temporary buffer for COBS encoding a single message
    uint8_t cobs_buf[FMRB_LINK_FRAME_MAX_DATA];

    for (size_t i = 0; i < msg_count; i++) {
        const fmrb_link_message_t *msg = &msgs[i];
        uint8_t seq = extract_seq_from_msgpack(msg->data, msg->size);

        size_t cobs_len = fmrb_link_cobs_encode(msg->data, msg->size, cobs_buf);

        // Check if adding this message would overflow the frame
        if (frame_data_len + cobs_len > FMRB_LINK_FRAME_MAX_DATA) {
            if (frame_data_len == 0) {
                // Single message too large for one frame
                ESP_LOGE(TAG, "COBS encoded data too large: %zu > %d", cobs_len, FMRB_LINK_FRAME_MAX_DATA);
                result = FMRB_ERR_INVALID_PARAM;
                break;
            }

            // Flush current frame before adding this message
            spi_frame_t tx;
            build_command_frame(&tx, last_seq, frame_data, (uint8_t)frame_data_len);

            result = send_frame_and_wait_ack(channel, &tx, last_seq, timeout_ms);
            if (result != FMRB_OK) {
                break;
            }

            ESP_LOGD(TAG, "Batch frame sent: seq=%u, data_len=%zu", last_seq, frame_data_len);
            frame_data_len = 0;
        }

        // Append COBS-encoded message to frame buffer
        memcpy(frame_data + frame_data_len, cobs_buf, cobs_len);
        frame_data_len += cobs_len;
        last_seq = seq;
    }

    // Send remaining data
    if (result == FMRB_OK && frame_data_len > 0) {
        spi_frame_t tx;
        build_command_frame(&tx, last_seq, frame_data, (uint8_t)frame_data_len);

        result = send_frame_and_wait_ack(channel, &tx, last_seq, timeout_ms);

        if (result == FMRB_OK) {
            ESP_LOGD(TAG, "Batch frame sent: seq=%u, data_len=%zu, msgs=%zu",
                     last_seq, frame_data_len, msg_count);
        }
    }

    xSemaphoreGive(spi_mutex);
    return result;
}

fmrb_err_t fmrb_hal_link_receive(fmrb_link_channel_t channel,
                                 fmrb_link_message_t *msg,
                                 uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msg) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (!spi_handle) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Check ACK queue: ACK data consumed by polling is queued here for transport layer
    if (ack_recv_queue) {
        ack_queue_entry_t entry;
        if (xQueueReceive(ack_recv_queue, &entry, 0) == pdTRUE) {
            msg->data = entry.data;  // Transfer ownership (caller must free)
            msg->size = entry.size;
            ESP_LOGD(TAG, "Dequeued ACK for transport: %zu bytes", entry.size);
            return FMRB_OK;
        }
    }

    // Slave only sends ACKs in response to master commands.
    // ACKs are collected during fmrb_hal_link_send() polling.
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

/**
 * Process received spi_frame_t: validate, check ack_seq/status, decode COBS data
 */
static void process_received_frame(fmrb_link_channel_t channel, const spi_frame_t *rx) {
    esp32_link_channel_t *ch = &channels[channel];

    // Validate frame
    if (!spi_frame_validate(rx)) {
        return;  // Invalid frame (magic or CRC16 mismatch)
    }

    // Check if this response matches our expected seq
    if (rx->ack_seq == 0 && rx->status == STS_BOOT) {
        return;  // Slave still in BOOT state, no meaningful response
    }

    // If ack_seq matches and status is intermediate, just note it
    if (rx->ack_seq == ch->expected_seq) {
        if (rx->status == STS_RX_OK) {
            // Slave received our command but hasn't processed it yet
            ESP_LOGD(TAG, "RX_OK for seq=%u, waiting for app result", rx->ack_seq);
            return;
        }

        if (rx->status == STS_CRC_ERR) {
            ESP_LOGW(TAG, "Slave reported CRC error for seq=%u", rx->ack_seq);
            return;
        }

        // Final status: STS_APP_OK or STS_APP_ERR
        // Parse response data from data[] if present
        if (rx->data_len > 0) {
            // Parse multiple COBS messages from data[0..data_len-1]
            size_t pos = 0;
            while (pos < rx->data_len) {
                // Skip leading 0x00
                while (pos < rx->data_len && rx->data[pos] == 0x00) pos++;
                if (pos >= rx->data_len) break;

                // Find COBS frame end
                size_t frame_start = pos;
                while (pos < rx->data_len && rx->data[pos] != 0x00) pos++;
                size_t frame_len = pos - frame_start;

                // COBS decode (no CRC32)
                uint8_t decoded[FMRB_LINK_FRAME_MAX_DATA];
                ssize_t decoded_len = fmrb_link_cobs_decode(
                    rx->data + frame_start, frame_len, decoded);

                if (decoded_len <= 0) continue;

                // Check for EMPTY frame (type == 0)
                if (decoded_len >= 2 && decoded[0] == 0x94 && decoded[1] == 0x00) {
                    continue;  // EMPTY frame
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

                // Notify via callback if registered
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

        // Mark ACK as received (final status)
        ch->ack_received = true;
        ESP_LOGD(TAG, "ACK final: seq=%u status=0x%02x data_len=%u",
                 rx->ack_seq, rx->status, rx->data_len);
    }
}

/**
 * Poll for ACK by sending empty spi_frame_t (seq=0, data_len=0)
 * NOTE: Caller must hold spi_mutex (called from send_frame_and_wait_ack
 *       which is invoked under fmrb_hal_link_send's mutex lock)
 */
static fmrb_err_t poll_for_ack(fmrb_link_channel_t channel, uint32_t timeout_ms) {
    (void)timeout_ms;

    if (!link_initialized || !spi_handle) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Only poll when READY=HIGH (slave has queued slots)
    if (fmrb_hal_gpio_get_level(FMRB_PIN_GFX_SPI_INTR) != 1) {
        return FMRB_ERR_TIMEOUT;
    }

    // Build empty poll frame (seq=0 = polling-only)
    spi_frame_t tx, rx;
    build_command_frame(&tx, 0, NULL, 0);

    fmrb_err_t ret = spi_transfer_frame(&tx, &rx);

    if (ret == FMRB_OK) {
        ESP_LOGD(TAG, "POLL TX: seq=%u ack=%u st=0x%02x dlen=%u | RX: seq=%u ack=%u st=0x%02x dlen=%u",
                 tx.seq, tx.ack_seq, tx.status, tx.data_len,
                 rx.seq, rx.ack_seq, rx.status, rx.data_len);
        process_received_frame(channel, &rx);
    }

    return FMRB_OK;
}
