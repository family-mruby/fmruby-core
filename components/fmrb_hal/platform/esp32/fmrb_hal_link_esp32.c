#include "fmrb_hal_link.h"
#include "fmrb_hal_spi.h"
#include "fmrb_link_cobs.h"
#include "fmrb_mem.h"
#include "fmrb_pin_assign.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define SPI_FRAME_SIZE 64  // Fixed frame size matching slave
#define SPI_FREQUENCY (10 * 1000 * 1000)  // 10MHz

typedef struct {
    fmrb_link_callback_t callback;
    void *user_data;
} esp32_link_channel_t;

static esp32_link_channel_t channels[FMRB_LINK_MAX_CHANNELS];
static bool link_initialized = false;
static fmrb_spi_handle_t spi_handle = NULL;
static SemaphoreHandle_t spi_mutex = NULL;

static const char *TAG = "fmrb_hal_link";

fmrb_err_t fmrb_hal_link_init(void) {
    if (link_initialized) {
        return FMRB_OK;
    }

    // Initialize channels
    for (int i = 0; i < FMRB_LINK_MAX_CHANNELS; i++) {
        channels[i].callback = NULL;
        channels[i].user_data = NULL;
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

    ESP_LOGI(TAG, "ESP32 SPI link communication initialized (MOSI=%d, MISO=%d, SCLK=%d, CS=%d, %dMHz)",
             spi_config.mosi_pin, spi_config.miso_pin, spi_config.sclk_pin,
             spi_config.cs_pin, spi_config.frequency / 1000000);

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

    // Send via SPI
    fmrb_err_t ret = fmrb_hal_spi_transmit(spi_handle, tx_frame, SPI_FRAME_SIZE, timeout_ms);

    xSemaphoreGive(spi_mutex);

    if (ret != FMRB_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %d", ret);
        return FMRB_ERR_FAILED;
    }

    ESP_LOGD(TAG, "Sent %zu bytes (payload+crc: %zu, encoded: %zu, frame: %d) to channel %d",
             msg->size, total_size, encoded_len, SPI_FRAME_SIZE, channel);

    return FMRB_OK;
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
    size_t decoded_len = fmrb_link_cobs_decode(recv_buffer, frame_end, decoded);

    // Remove processed frame from buffer
    memmove(recv_buffer, recv_buffer + frame_end + 1, recv_pos - frame_end - 1);
    recv_pos -= (frame_end + 1);

    if (decoded_len < sizeof(uint32_t)) {
        ESP_LOGW(TAG, "Decoded frame too small: %zu bytes", decoded_len);
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
