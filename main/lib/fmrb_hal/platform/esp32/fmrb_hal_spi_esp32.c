#include "../../fmrb_hal_spi.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    spi_device_handle_t device;
    fmrb_spi_config_t config;
} esp32_spi_handle_t;

static const char *TAG = "fmrb_hal_spi";

fmrb_err_t fmrb_hal_spi_init(const fmrb_spi_config_t *config, fmrb_spi_handle_t *handle) {
    if (!config || !handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_spi_handle_t *esp_handle = fmrb_sys_malloc(sizeof(esp32_spi_handle_t));
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
        fmrb_sys_free(esp_handle);
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
        fmrb_sys_free(esp_handle);
        return FMRB_ERR_FAILED;
    }

    esp_handle->config = *config;
    *handle = esp_handle;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_deinit(fmrb_spi_handle_t handle) {
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;
    spi_bus_remove_device(esp_handle->device);
    spi_bus_free(SPI2_HOST);
    fmrb_sys_free(esp_handle);

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_spi_transmit(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  size_t length,
                                  uint32_t timeout_ms) {
    if (!handle || !tx_data || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;

    spi_transaction_t trans = {
        .length = length * 8,
        .tx_buffer = tx_data
    };

    esp_err_t ret = spi_device_transmit(esp_handle->device, &trans);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t fmrb_hal_spi_receive(fmrb_spi_handle_t handle,
                                 uint8_t *rx_data,
                                 size_t length,
                                 uint32_t timeout_ms) {
    if (!handle || !rx_data || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;

    spi_transaction_t trans = {
        .length = length * 8,
        .rx_buffer = rx_data
    };

    esp_err_t ret = spi_device_transmit(esp_handle->device, &trans);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t fmrb_hal_spi_transfer(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length,
                                  uint32_t timeout_ms) {
    if (!handle || (!tx_data && !rx_data) || length == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp32_spi_handle_t *esp_handle = (esp32_spi_handle_t *)handle;

    spi_transaction_t trans = {
        .length = length * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data
    };

    esp_err_t ret = spi_device_transmit(esp_handle->device, &trans);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
}