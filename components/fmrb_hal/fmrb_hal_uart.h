#pragma once

#include "fmrb_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// UART handle type
typedef void* fmrb_uart_handle_t;

// UART configuration
typedef struct {
    const char *device_path;  // POSIX: device path (e.g., "/dev/pts/3"), ESP32: ignored
    int uart_num;             // ESP32: UART port number (0, 1, 2), POSIX: ignored
    int tx_pin;               // ESP32: TX pin number, POSIX: ignored
    int rx_pin;               // ESP32: RX pin number, POSIX: ignored
    uint32_t baud_rate;       // Both: baud rate (e.g., 115200)
    uint32_t timeout_ms;      // Both: read timeout in milliseconds
} fmrb_uart_config_t;

/**
 * @brief Open UART device
 *
 * @param config UART configuration
 * @param handle Output handle
 * @return fmrb_err_t FMRB_OK on success
 */
fmrb_err_t fmrb_hal_uart_open(const fmrb_uart_config_t *config, fmrb_uart_handle_t *handle);

/**
 * @brief Close UART device
 *
 * @param handle UART handle
 * @return fmrb_err_t FMRB_OK on success
 */
fmrb_err_t fmrb_hal_uart_close(fmrb_uart_handle_t handle);

/**
 * @brief Read from UART
 *
 * @param handle UART handle
 * @param buffer Buffer to read into
 * @param size Maximum bytes to read
 * @param bytes_read Actual bytes read (can be NULL)
 * @return fmrb_err_t FMRB_OK on success, FMRB_ERR_TIMEOUT on timeout
 */
fmrb_err_t fmrb_hal_uart_read(fmrb_uart_handle_t handle, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Write to UART
 *
 * @param handle UART handle
 * @param buffer Data to write
 * @param size Bytes to write
 * @param bytes_written Actual bytes written (can be NULL)
 * @return fmrb_err_t FMRB_OK on success
 */
fmrb_err_t fmrb_hal_uart_write(fmrb_uart_handle_t handle, const void *buffer, size_t size, size_t *bytes_written);

/**
 * @brief Read one byte from UART (non-blocking)
 *
 * @param handle UART handle
 * @param byte Output byte
 * @return fmrb_err_t FMRB_OK on success, FMRB_ERR_TIMEOUT if no data available
 */
fmrb_err_t fmrb_hal_uart_read_byte(fmrb_uart_handle_t handle, uint8_t *byte);

#ifdef __cplusplus
}
#endif
