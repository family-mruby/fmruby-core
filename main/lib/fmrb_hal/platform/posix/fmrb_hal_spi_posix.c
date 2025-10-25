#include "../../fmrb_hal_spi.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    fmrb_spi_config_t config;
    bool initialized;
} linux_spi_handle_t;

static const char *TAG = "fmrb_hal_spi";

fmrb_err_t fmrb_hal_spi_init(const fmrb_spi_config_t *config, fmrb_spi_handle_t *handle) {
    if (!config || !handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_spi_handle_t *linux_handle = malloc(sizeof(linux_spi_handle_t));
    if (!linux_handle) {
        return FMRB_ERR_NO_MEMORY;
    }

    linux_handle->config = *config;
    linux_handle->initialized = true;

    ESP_LOGI(TAG, "Linux SPI initialized: MOSI=%d, MISO=%d, SCLK=%d, CS=%d, freq=%d",
             config->mosi_pin, config->miso_pin, config->sclk_pin,
             config->cs_pin, config->frequency);

    *handle = linux_handle;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_deinit(fmrb_spi_handle_t handle) {
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    linux_handle->initialized = false;
    free(linux_handle);
    ESP_LOGI(TAG, "Linux SPI deinitialized");

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_transmit(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  size_t length,
                                  uint32_t timeout_ms) {
    if (!handle || !tx_data || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    if (!linux_handle->initialized) {
        return FMRB_ERR_FAILED;
    }

    ESP_LOGI(TAG, "Linux SPI transmit %zu bytes", length);
    // Simulate transmission delay
    fmrb_hal_time_delay_ms(1);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_receive(fmrb_spi_handle_t handle,
                                 uint8_t *rx_data,
                                 size_t length,
                                 uint32_t timeout_ms) {
    if (!handle || !rx_data || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    if (!linux_handle->initialized) {
        return FMRB_ERR_FAILED;
    }

    // Simulate received data
    memset(rx_data, 0xAA, length);
    ESP_LOGI(TAG, "Linux SPI receive %zu bytes", length);
    fmrb_hal_time_delay_ms(1);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_transfer(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length,
                                  uint32_t timeout_ms) {
    if (!handle || (!tx_data && !rx_data) || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    if (!linux_handle->initialized) {
        return FMRB_ERR_FAILED;
    }

    if (rx_data) {
        memset(rx_data, 0xBB, length);
    }
    ESP_LOGI(TAG, "Linux SPI transfer %zu bytes", length);
    fmrb_hal_time_delay_ms(1);
    return FMRB_OK;
}