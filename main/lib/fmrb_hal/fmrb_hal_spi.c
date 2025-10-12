#include "fmrb_hal_spi.h"
#include "esp_log.h"

#ifdef FMRB_PLATFORM_LINUX
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    fmrb_spi_config_t config;
    bool initialized;
} linux_spi_handle_t;

#else
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    spi_device_handle_t device;
    fmrb_spi_config_t config;
} esp32_spi_handle_t;

#endif

static const char *TAG = "fmrb_hal_spi";

fmrb_err_t fmrb_hal_spi_init(const fmrb_spi_config_t *config, fmrb_spi_handle_t *handle) {
    if (!config || !handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

#ifdef FMRB_PLATFORM_LINUX
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
#else
    esp32_spi_handle_t *esp_handle = malloc(sizeof(esp32_spi_handle_t));
    if (!esp_handle) {
        return FMRB_ERR_NO_MEMORY;
    }

    // Configure SPI bus
    spi_bus_config_t bus_config = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        free(esp_handle);
        return FMRB_ERR_FAILED;
    }

    // Configure SPI device
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = config->frequency,
        .mode = 0,
        .spics_io_num = config->cs_pin,
        .queue_size = 1
    };

    ret = spi_bus_add_device(SPI2_HOST, &dev_config, &esp_handle->device);
    if (ret != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        free(esp_handle);
        return FMRB_ERR_FAILED;
    }

    esp_handle->config = *config;
    *handle = esp_handle;
    return FMRB_OK;
#endif
}

fmrb_err_t fmrb_hal_spi_deinit(fmrb_spi_handle_t handle) {
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

#ifdef FMRB_PLATFORM_LINUX
    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    linux_handle->initialized = false;
    free(linux_handle);
    ESP_LOGI(TAG, "Linux SPI deinitialized");
#else
    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;
    spi_bus_remove_device(esp_handle->device);
    spi_bus_free(SPI2_HOST);
    free(esp_handle);
#endif

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_transmit(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  size_t length,
                                  uint32_t timeout_ms) {
    if (!handle || !tx_data || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

#ifdef FMRB_PLATFORM_LINUX
    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    if (!linux_handle->initialized) {
        return FMRB_ERR_FAILED;
    }

    ESP_LOGI(TAG, "Linux SPI transmit %zu bytes", length);
    // Simulate transmission delay
    fmrb_hal_time_delay_ms(1);
    return FMRB_OK;
#else
    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;

    spi_transaction_t trans = {
        .length = length * 8,
        .tx_buffer = tx_data
    };

    esp_err_t ret = spi_device_transmit(esp_handle->device, &trans);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
#endif
}

fmrb_err_t fmrb_hal_spi_receive(fmrb_spi_handle_t handle,
                                 uint8_t *rx_data,
                                 size_t length,
                                 uint32_t timeout_ms) {
    if (!handle || !rx_data || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

#ifdef FMRB_PLATFORM_LINUX
    linux_spi_handle_t *linux_handle = (linux_spi_handle_t *)handle;
    if (!linux_handle->initialized) {
        return FMRB_ERR_FAILED;
    }

    // Simulate received data
    memset(rx_data, 0xAA, length);
    ESP_LOGI(TAG, "Linux SPI receive %zu bytes", length);
    fmrb_hal_time_delay_ms(1);
    return FMRB_OK;
#else
    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;

    spi_transaction_t trans = {
        .length = length * 8,
        .rx_buffer = rx_data
    };

    esp_err_t ret = spi_device_transmit(esp_handle->device, &trans);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
#endif
}

fmrb_err_t fmrb_hal_spi_transfer(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length,
                                  uint32_t timeout_ms) {
    if (!handle || (!tx_data && !rx_data) || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

#ifdef FMRB_PLATFORM_LINUX
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
#else
    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;

    spi_transaction_t trans = {
        .length = length * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    esp_err_t ret = spi_device_transmit(esp_handle->device, &trans);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
#endif
}