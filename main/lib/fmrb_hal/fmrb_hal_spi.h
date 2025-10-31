#pragma once

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// SPI types
typedef void* fmrb_spi_handle_t;
typedef struct {
    int mosi_pin;
    int miso_pin;
    int sclk_pin;
    int cs_pin;
    int frequency;
} fmrb_spi_config_t;


/**
 * @brief Initialize SPI interface
 * @param config SPI configuration
 * @param handle Pointer to store SPI handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_spi_init(const fmrb_spi_config_t *config, fmrb_spi_handle_t *handle);

/**
 * @brief Deinitialize SPI interface
 * @param handle SPI handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_spi_deinit(fmrb_spi_handle_t handle);

/**
 * @brief Transmit data over SPI
 * @param handle SPI handle
 * @param tx_data Data to transmit
 * @param length Data length in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_spi_transmit(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  size_t length,
                                  uint32_t timeout_ms);

/**
 * @brief Receive data over SPI
 * @param handle SPI handle
 * @param rx_data Buffer to store received data
 * @param length Data length in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_spi_receive(fmrb_spi_handle_t handle,
                                 uint8_t *rx_data,
                                 size_t length,
                                 uint32_t timeout_ms);

/**
 * @brief Transmit and receive data over SPI (full duplex)
 * @param handle SPI handle
 * @param tx_data Data to transmit
 * @param rx_data Buffer to store received data
 * @param length Data length in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_spi_transfer(fmrb_spi_handle_t handle,
                                  const uint8_t *tx_data,
                                  uint8_t *rx_data,
                                  size_t length,
                                  uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
