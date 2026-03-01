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
#include <string.h>

#define SPI_FRAME_SIZE 256  // Fixed frame size matching slave (increased for better throughput)
#define SPI_FREQUENCY (10 * 1000 * 1000)  // 10MHz
#define ACK_RECV_QUEUE_SIZE 8

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

static const char *TAG = "fmrb_hal_link";

// Forward declarations
static void process_received_ack(fmrb_link_channel_t channel, const uint8_t *rx_frame, size_t frame_size);
static fmrb_err_t poll_for_ack(fmrb_link_channel_t channel, uint32_t timeout_ms);

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

    // If ACK not received immediately, poll with timeout
    if (!ch->ack_received && timeout_ms > 0) {
        fmrb_time_t start_time = fmrb_hal_time_get_us();
        uint32_t poll_interval_ms = 1;  // Start with 1ms
        const uint32_t max_poll_interval_ms = 8;  // Max 8ms

        while (!ch->ack_received) {
            // Check timeout
            if (fmrb_hal_time_is_timeout(start_time, timeout_ms * 1000)) {
                ESP_LOGW(TAG, "ACK timeout after %u ms", timeout_ms);
                return FMRB_ERR_TIMEOUT;
            }

            // Poll for ACK
            fmrb_err_t poll_ret = poll_for_ack(channel, poll_interval_ms);
            if (poll_ret != FMRB_OK && poll_ret != FMRB_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "ACK polling failed: %d", poll_ret);
                return poll_ret;
            }

            // If ACK not received, wait and apply exponential backoff
            if (!ch->ack_received) {
                fmrb_hal_time_delay_ms(poll_interval_ms);

                // Exponential backoff: double interval up to max (1ms → 2ms → 4ms → 8ms)
                if (poll_interval_ms < max_poll_interval_ms) {
                    poll_interval_ms *= 2;
                }
            }
        }

        ESP_LOGI(TAG, "ACK received after polling (channel=%d)", channel);
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
            ESP_LOGI(TAG, "Dequeued ACK for transport: %zu bytes", entry.size);
            return FMRB_OK;
        }
    }

    // If handshake GPIO is LOW, slave has ACK data pending.
    // Skip SPI transfer to avoid consuming ACK meant for poll_for_ack().
    int32_t hs_level = fmrb_hal_gpio_get_level(FMRB_PIN_GFX_SPI_INTR);
    if (hs_level == 0) {
        return FMRB_ERR_TIMEOUT;
    }

    // Take SPI mutex
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    // Static receive buffer for COBS frames
    static uint8_t recv_buffer[SPI_FRAME_SIZE * 4];  // Buffer multiple frames
    static size_t recv_pos = 0;

    // Receive SPI frame
    uint8_t rx_frame[SPI_FRAME_SIZE];
    memset(rx_frame, 0, SPI_FRAME_SIZE);

    fmrb_err_t ret = fmrb_hal_spi_receive(spi_handle, rx_frame, SPI_FRAME_SIZE, timeout_ms);

    xSemaphoreGive(spi_mutex);

    if (ret != FMRB_OK) {
        if (ret == FMRB_ERR_TIMEOUT) {
            return FMRB_ERR_TIMEOUT;
        }
        ESP_LOGE(TAG, "SPI receive failed: %d", ret);
        return FMRB_ERR_FAILED;
    }

    // Append received data to buffer
    if (recv_pos + SPI_FRAME_SIZE > sizeof(recv_buffer)) {
        ESP_LOGW(TAG, "Receive buffer overflow, resetting");
        recv_pos = 0;
    }
    memcpy(recv_buffer + recv_pos, rx_frame, SPI_FRAME_SIZE);
    recv_pos += SPI_FRAME_SIZE;

    ESP_LOGD(TAG, "Received SPI frame, buffer pos=%zu", recv_pos);

    // Skip leading null bytes
    while (recv_pos > 0 && recv_buffer[0] == 0x00) {
        memmove(recv_buffer, recv_buffer + 1, recv_pos - 1);
        recv_pos--;
    }

    // Find frame terminator (0x00)
    size_t frame_end = 0;
    bool found_terminator = false;
    for (size_t i = 0; i < recv_pos; i++) {
        if (recv_buffer[i] == 0x00) {
            frame_end = i;
            found_terminator = true;
            break;
        }
    }

    if (!found_terminator) {
        // Incomplete frame, need more data
        return FMRB_ERR_TIMEOUT;
    }

    if (frame_end == 0) {
        // Empty frame
        memmove(recv_buffer, recv_buffer + 1, recv_pos - 1);
        recv_pos--;
        return FMRB_ERR_TIMEOUT;
    }

    // COBS decode
    uint8_t decoded[1024];
    ssize_t decoded_len = fmrb_link_cobs_decode(recv_buffer, frame_end, decoded);

    // Remove processed frame from buffer
    memmove(recv_buffer, recv_buffer + frame_end + 1, recv_pos - frame_end - 1);
    recv_pos -= (frame_end + 1);

    if (decoded_len <= 0) {
        ESP_LOGW(TAG, "COBS decode failed: frame_len=%zu, decoded_len=%zd", frame_end, decoded_len);
        return FMRB_ERR_FAILED;
    }

    if ((size_t)decoded_len < sizeof(uint32_t)) {
        ESP_LOGW(TAG, "Decoded frame too small: %zd bytes", decoded_len);
        return FMRB_ERR_FAILED;
    }

    // Verify CRC32
    size_t payload_len = decoded_len - sizeof(uint32_t);
    uint32_t received_crc;
    memcpy(&received_crc, decoded + payload_len, sizeof(uint32_t));
    uint32_t calculated_crc = fmrb_link_crc32_update(0, decoded, payload_len);

    if (received_crc != calculated_crc) {
        ESP_LOGE(TAG, "CRC mismatch: received=0x%08lx, calculated=0x%08lx",
                 received_crc, calculated_crc);
        return FMRB_ERR_FAILED;
    }

    // Allocate and copy payload
    msg->data = fmrb_sys_malloc(payload_len);
    if (!msg->data) {
        return FMRB_ERR_NO_MEMORY;
    }
    memcpy(msg->data, decoded, payload_len);
    msg->size = payload_len;

    ESP_LOGD(TAG, "Received %zu bytes from channel %d", payload_len, channel);

    return FMRB_OK;
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
    // Skip leading null bytes
    size_t rx_start = 0;
    while (rx_start < frame_size && rx_frame[rx_start] == 0x00) {
        rx_start++;
    }

    if (rx_start >= frame_size) {
        ESP_LOGD(TAG, "No ACK data received in this transaction (normal, will retry later)");
        return;
    }

    // Find COBS frame terminator
    size_t frame_end = rx_start;
    bool found_terminator = false;
    for (size_t i = rx_start; i < frame_size; i++) {
        if (rx_frame[i] == 0x00) {
            frame_end = i;
            found_terminator = true;
            break;
        }
    }

    if (!found_terminator || frame_end == rx_start) {
        ESP_LOGD(TAG, "No complete ACK frame in this transaction (normal, will retry later)");
        return;
    }

    // COBS decode received ACK
    uint8_t decoded[1024];
    size_t encoded_len_rx = frame_end - rx_start;
    ssize_t decoded_len = fmrb_link_cobs_decode(rx_frame + rx_start, encoded_len_rx, decoded);

    if (decoded_len <= (ssize_t)sizeof(uint32_t)) {
        ESP_LOGW(TAG, "Decoded ACK too small: %d bytes", (int)decoded_len);
        return;
    }

    // Verify CRC32
    size_t payload_len_rx = decoded_len - sizeof(uint32_t);
    uint32_t received_crc;
    memcpy(&received_crc, decoded + payload_len_rx, sizeof(uint32_t));
    uint32_t calculated_crc = fmrb_link_crc32_update(0, decoded, payload_len_rx);

    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "ACK CRC mismatch: received=0x%08lx, calculated=0x%08lx (decoded_len=%d)",
                 received_crc, calculated_crc, (int)decoded_len);
        ESP_LOG_BUFFER_HEXDUMP(TAG, rx_frame + rx_start, encoded_len_rx, ESP_LOG_WARN);
        return;
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

    esp32_link_channel_t *ch = &channels[channel];

    // Set ACK received flag for HAL-level polling loop
    ch->ack_received = true;

    // Also notify via callback if registered
    if (ch->callback) {
        // Allocate heap memory for ACK data (callback must free it)
        uint8_t *decoded_copy = (uint8_t*)fmrb_sys_malloc(payload_len_rx);
        if (decoded_copy) {
            memcpy(decoded_copy, decoded, payload_len_rx);
            fmrb_link_message_t ack_msg = {
                .data = decoded_copy,
                .size = payload_len_rx
            };
            ch->callback(channel, &ack_msg, ch->user_data);
            ESP_LOGI(TAG, "ACK received: %zu bytes", payload_len_rx);
        } else {
            ESP_LOGE(TAG, "Failed to allocate memory for ACK data");
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

    // Prepare empty TX frame (all zeros)
    uint8_t tx_frame[SPI_FRAME_SIZE];
    memset(tx_frame, 0, SPI_FRAME_SIZE);

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
