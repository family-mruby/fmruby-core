#include "fmrb_hal_link.h"
#include "fmrb_hal_spi.h"
#include "fmrb_hal_gpio.h"
#include "fmrb_link_cobs.h"
#include "fmrb_mem.h"
#include "fmrb_pin_assign.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <string.h>

#define SPI_FRAME_SIZE 256  // Fixed frame size matching slave (increased for better throughput)
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
static SemaphoreHandle_t ack_notify_sem = NULL;  // ACK ready notification from GPIO ISR

static const char *TAG = "fmrb_hal_link";

// Cached EMPTY frame for poll TX (encoded once at init)
static uint8_t s_empty_frame[SPI_FRAME_SIZE];
static size_t s_empty_frame_len = 0;

// Forward declarations
static void process_received_ack(fmrb_link_channel_t channel, const uint8_t *rx_frame, size_t frame_size);
static fmrb_err_t poll_for_ack(fmrb_link_channel_t channel, uint32_t timeout_ms);

// GPIO ISR: called on falling edge of handshake pin (slave signals ACK ready)
static void IRAM_ATTR ack_gpio_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(ack_notify_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
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

    // Initialize handshake GPIO as input (externally pulled up, active LOW)
    fmrb_hal_gpio_config(FMRB_PIN_GFX_SPI_INTR, FMRB_GPIO_MODE_INPUT, FMRB_GPIO_PULL_NONE);

    // Create ACK notification semaphore
    ack_notify_sem = xSemaphoreCreateBinary();
    if (!ack_notify_sem) {
        ESP_LOGE(TAG, "Failed to create ACK notify semaphore");
    } else {
        // Install GPIO ISR service (ignore if already installed)
        esp_err_t isr_ret = gpio_install_isr_service(0);
        if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %d", isr_ret);
        }

        // Configure falling-edge interrupt on handshake pin
        gpio_set_intr_type(FMRB_PIN_GFX_SPI_INTR, GPIO_INTR_NEGEDGE);
        gpio_isr_handler_add(FMRB_PIN_GFX_SPI_INTR, ack_gpio_isr_handler, NULL);
        ESP_LOGI(TAG, "ACK GPIO ISR registered on pin %d (falling edge)", FMRB_PIN_GFX_SPI_INTR);
    }

    // Encode EMPTY frame for poll TX: msgpack [EMPTY(0), seq(0), ACK(0xF0), nil]
    {
        const uint8_t msgpack_empty[] = {0x94, 0x00, 0x00, 0xCC, 0xF0, 0xC0};
        uint32_t crc = fmrb_link_crc32_update(0, msgpack_empty, sizeof(msgpack_empty));
        uint8_t msg_with_crc[sizeof(msgpack_empty) + sizeof(uint32_t)];
        memcpy(msg_with_crc, msgpack_empty, sizeof(msgpack_empty));
        memcpy(msg_with_crc + sizeof(msgpack_empty), &crc, sizeof(uint32_t));

        memset(s_empty_frame, 0, SPI_FRAME_SIZE);
        size_t cobs_len = fmrb_link_cobs_encode(msg_with_crc, sizeof(msg_with_crc), s_empty_frame);
        s_empty_frame[cobs_len] = 0x00;
        s_empty_frame_len = cobs_len + 1;
    }

    ESP_LOGI(TAG, "ESP32 SPI link communication initialized (MOSI=%d, MISO=%d, SCLK=%d, CS=%d, INTR=%d, %dMHz)",
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

    // Remove GPIO ISR and delete ACK notify semaphore
    if (ack_notify_sem) {
        gpio_isr_handler_remove(FMRB_PIN_GFX_SPI_INTR);
        vSemaphoreDelete(ack_notify_sem);
        ack_notify_sem = NULL;
    }

    // Flush and delete ACK receive queue
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

    ESP_LOGI(TAG, "ESP32 SPI link communication deinitialized");
    link_initialized = false;
}

fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                              const fmrb_link_message_t *msg,
                              uint32_t timeout_ms) {
    if (!link_initialized || channel >= FMRB_LINK_MAX_CHANNELS || !msg) {
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

    // Prepare buffer: [data | CRC32]
    size_t total_size = msg->size + sizeof(uint32_t);
    uint8_t *buffer = (uint8_t*)fmrb_sys_malloc(total_size);
    if (!buffer) {
        xSemaphoreGive(spi_mutex);
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
        xSemaphoreGive(spi_mutex);
        return FMRB_ERR_NO_MEMORY;
    }

    size_t encoded_len = fmrb_link_cobs_encode(buffer, total_size, encoded);
    fmrb_sys_free(buffer);

    // Pad to SPI_FRAME_SIZE
    if (encoded_len > SPI_FRAME_SIZE - 1) {  // -1 for 0x00 terminator
        ESP_LOGE(TAG, "Encoded message too large: %zu bytes (max %d)", encoded_len, SPI_FRAME_SIZE - 1);
        fmrb_sys_free(encoded);
        xSemaphoreGive(spi_mutex);
        return FMRB_ERR_INVALID_PARAM;
    }

    // Prepare SPI frame: [COBS encoded data | 0x00 | padding]
    uint8_t tx_frame[SPI_FRAME_SIZE];
    memset(tx_frame, 0, SPI_FRAME_SIZE);
    memcpy(tx_frame, encoded, encoded_len);
    tx_frame[encoded_len] = 0x00;  // COBS frame terminator

    fmrb_sys_free(encoded);

    // Prepare RX buffer for full-duplex transfer (to receive ACK simultaneously)
    uint8_t rx_frame[SPI_FRAME_SIZE];
    memset(rx_frame, 0, SPI_FRAME_SIZE);

    // Clear stale ACK notification before sending
    if (ack_notify_sem) {
        xSemaphoreTake(ack_notify_sem, 0);
    }

    // Send via SPI with full-duplex transfer (TX and RX simultaneously)
    fmrb_err_t ret = fmrb_hal_spi_transfer(spi_handle, tx_frame, rx_frame, SPI_FRAME_SIZE, timeout_ms);

    xSemaphoreGive(spi_mutex);

    if (ret != FMRB_OK) {
        ESP_LOGE(TAG, "SPI transfer failed: %d", ret);
        return FMRB_ERR_FAILED;
    }

    ESP_LOGD(TAG, "Sent %zu bytes (payload+crc: %zu, encoded: %zu, frame: %d) to channel %d",
             msg->size, total_size, encoded_len, SPI_FRAME_SIZE, channel);

    // Reset ACK received flag before processing
    esp32_link_channel_t *ch = &channels[channel];
    ch->ack_received = false;

    // Process received ACK from slave (if any)
    process_received_ack(channel, rx_frame, SPI_FRAME_SIZE);

    // If ACK not received immediately, wait for GPIO interrupt then poll.
    // GPIO LOW (falling edge) = slave has staged ACK. Due to SPI double-buffering,
    // master needs 2 polls after ISR: first triggers slave to copy ACK to TX buffer,
    // second reads the actual ACK data. GPIO goes HIGH when ACK is transmitted.
    if (!ch->ack_received && timeout_ms > 0) {
        fmrb_time_t start_time = fmrb_hal_time_get_us();

        while (!ch->ack_received) {
            // Check timeout
            if (fmrb_hal_time_is_timeout(start_time, timeout_ms * 1000)) {
                ESP_LOGW(TAG, "ACK timeout after %u ms", timeout_ms);
                return FMRB_ERR_TIMEOUT;
            }

            // Primary: wait for falling-edge ISR (slave staged ACK)
            // Failsafe: timeout triggers poll to keep SPI alive
            uint64_t elapsed_us = fmrb_hal_time_get_us() - start_time;
            uint32_t remaining_ms = (elapsed_us / 1000 < timeout_ms) ?
                                    (timeout_ms - (uint32_t)(elapsed_us / 1000)) : 1;
            if (ack_notify_sem) {
                xSemaphoreTake(ack_notify_sem, pdMS_TO_TICKS(remaining_ms));
            } else {
                fmrb_hal_time_delay_ms(remaining_ms < 5 ? remaining_ms : 5);
            }

            // After ISR wakeup: poll while GPIO is LOW (ACK pending)
            // Poll 1: triggers slave to copy ACK to TX buffer
            // Poll 2: reads actual ACK data (slave sets GPIO HIGH)
            while (fmrb_hal_gpio_get_level(FMRB_PIN_GFX_SPI_INTR) == 0
                   && !ch->ack_received) {
                poll_for_ack(channel, 10);
                if (!ch->ack_received) {
                    fmrb_hal_time_delay_us(500);
                }
            }
        }

        ESP_LOGD(TAG, "ACK received after waiting (channel=%d)", channel);
    }

    return ch->ack_received ? FMRB_OK : FMRB_ERR_TIMEOUT;
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

    // Check ACK queue first: ACK data consumed by fmrb_hal_link_send() polling
    // is queued here for the transport layer to process
    if (ack_recv_queue) {
        ack_queue_entry_t entry;
        if (xQueueReceive(ack_recv_queue, &entry, 0) == pdTRUE) {
            msg->data = entry.data;  // Transfer ownership (caller must free)
            msg->size = entry.size;
            ESP_LOGD(TAG, "Dequeued ACK for transport: %zu bytes", entry.size);
            return FMRB_OK;
        }
    }

    // In this protocol, the slave only sends ACKs in response to master commands.
    // ACKs are collected by poll_for_ack() during fmrb_hal_link_send() and queued
    // to ack_recv_queue (checked above). There is no unsolicited data from slave.
    // Performing SPI reads here would generate unnecessary transactions that
    // overwhelm the slave's SPI transaction queue, causing data corruption
    // and ACK delivery failures.
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
 * @brief Process received ACK data from RX frame
 * @param channel Channel number
 * @param rx_frame RX frame buffer
 * @param frame_size Frame size
 */
static void process_received_ack(fmrb_link_channel_t channel, const uint8_t *rx_frame, size_t frame_size) {
    esp32_link_channel_t *ch = &channels[channel];
    size_t pos = 0;

    while (pos < frame_size) {
        // Skip null bytes (inter-frame padding)
        while (pos < frame_size && rx_frame[pos] == 0x00) {
            pos++;
        }
        if (pos >= frame_size) {
            break;
        }

        // Find COBS frame terminator
        size_t frame_start = pos;
        while (pos < frame_size && rx_frame[pos] != 0x00) {
            pos++;
        }
        if (pos >= frame_size) {
            break;  // No terminator - incomplete frame
        }

        size_t encoded_len_rx = pos - frame_start;
        pos++;  // Skip 0x00 terminator

        // COBS decode
        uint8_t decoded[1024];
        ssize_t decoded_len = fmrb_link_cobs_decode(rx_frame + frame_start, encoded_len_rx, decoded);

        if (decoded_len <= (ssize_t)sizeof(uint32_t)) {
            ESP_LOGW(TAG, "Decoded ACK too small: %d bytes (encoded_len=%zu)",
                     (int)decoded_len, encoded_len_rx);
            continue;
        }

        // Verify CRC32
        size_t payload_len_rx = decoded_len - sizeof(uint32_t);
        uint32_t received_crc;
        memcpy(&received_crc, decoded + payload_len_rx, sizeof(uint32_t));
        uint32_t calculated_crc = fmrb_link_crc32_update(0, decoded, payload_len_rx);

        if (received_crc != calculated_crc) {
            ESP_LOGW(TAG, "ACK CRC mismatch: received=0x%08lx, calculated=0x%08lx",
                     received_crc, calculated_crc);
            continue;
        }

        // Check for EMPTY frame (type == FMRB_LINK_TYPE_EMPTY == 0)
        // msgpack fixarray(4) = 0x94, followed by type byte
        if (payload_len_rx >= 2 && decoded[0] == 0x94 && decoded[1] == 0x00) {
            ESP_LOGD(TAG, "EMPTY frame received, skipping");
            continue;
        }

        // Valid ACK received - queue for transport layer
        if (ack_recv_queue) {
            uint8_t *queued_data = (uint8_t*)fmrb_sys_malloc(payload_len_rx);
            if (queued_data) {
                memcpy(queued_data, decoded, payload_len_rx);
                ack_queue_entry_t entry = { .data = queued_data, .size = payload_len_rx };
                if (xQueueSend(ack_recv_queue, &entry, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "ACK queue full, dropping data");
                    fmrb_sys_free(queued_data);
                }
            }
        }

        // Set ACK received flag for HAL-level polling loop
        ch->ack_received = true;

        // Notify via callback if registered
        if (ch->callback) {
            uint8_t *decoded_copy = (uint8_t*)fmrb_sys_malloc(payload_len_rx);
            if (decoded_copy) {
                memcpy(decoded_copy, decoded, payload_len_rx);
                fmrb_link_message_t ack_msg = {
                    .data = decoded_copy,
                    .size = payload_len_rx
                };
                ch->callback(channel, &ack_msg, ch->user_data);
                ESP_LOGD(TAG, "ACK received: %zu bytes", payload_len_rx);
            }
        }
    }
}

/**
 * @brief Poll for ACK by sending empty frame
 * @param channel Channel number
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
static fmrb_err_t poll_for_ack(fmrb_link_channel_t channel, uint32_t timeout_ms) {
    if (!link_initialized || !spi_handle) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Check handshake GPIO: active LOW means slave has ACK ready
    int32_t hs_level = fmrb_hal_gpio_get_level(FMRB_PIN_GFX_SPI_INTR);
    if (hs_level != 0) {
        // Slave not ready yet, skip SPI transaction
        return FMRB_ERR_TIMEOUT;
    }

    // Take SPI mutex
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    // Prepare TX frame with EMPTY message
    uint8_t tx_frame[SPI_FRAME_SIZE];
    memset(tx_frame, 0, SPI_FRAME_SIZE);
    memcpy(tx_frame, s_empty_frame, s_empty_frame_len);

    // Prepare RX buffer
    uint8_t rx_frame[SPI_FRAME_SIZE];
    memset(rx_frame, 0, SPI_FRAME_SIZE);

    // Send empty frame to poll for ACK (slave signaled ready via handshake)
    fmrb_err_t ret = fmrb_hal_spi_transfer(spi_handle, tx_frame, rx_frame, SPI_FRAME_SIZE, timeout_ms);

    xSemaphoreGive(spi_mutex);

    if (ret != FMRB_OK) {
        ESP_LOGD(TAG, "ACK poll transfer failed: %d", ret);
        return ret;
    }

    // Process any received ACK
    process_received_ack(channel, rx_frame, SPI_FRAME_SIZE);

    return FMRB_OK;
}
