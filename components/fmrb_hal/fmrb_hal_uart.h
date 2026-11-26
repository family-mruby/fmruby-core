#pragma once

#include "fmrb_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// UART handle type
typedef void* fmrb_uart_handle_t;

// Parity (the numbers are UART::PARITY_* of the Ruby class, sig/uart.rbs)
#define FMRB_UART_PARITY_NONE 0
#define FMRB_UART_PARITY_EVEN 1
#define FMRB_UART_PARITY_ODD  2

// UART configuration. A field left 0 takes its default (8N1, a 1 KB driver
// receive buffer), so a caller that names only the first six is unchanged.
typedef struct {
    const char *device_path;  // POSIX: device path (e.g., "/dev/pts/3"); NULL is the
                              // simulation's stand-in for uart_num (below). ESP32: ignored
    int uart_num;             // ESP32: UART port number (0, 1, 2 ...); POSIX: the stand-in's number
    int tx_pin;               // ESP32: TX pin number (-1 none), POSIX: ignored
    int rx_pin;               // ESP32: RX pin number (-1 none), POSIX: ignored
    uint32_t baud_rate;       // Both: baud rate (e.g., 115200)
    uint32_t timeout_ms;      // Both: read timeout in milliseconds (0 does not wait)
    uint8_t data_bits;        // 5-8 (0 = 8)
    uint8_t stop_bits;        // 1 or 2 (0 = 1)
    uint8_t parity;           // FMRB_UART_PARITY_*
    uint32_t rx_buffer_size;  // ESP32: the driver's receive buffer in bytes (0 = 1024)
} fmrb_uart_config_t;

/*
 * POSIX without a device_path (the simulation's stand-in for a UART, opened
 * with uart_num N): when both FIFOs FMRB_UART_SIM_DIR/uartN_in.fifo and
 * uartN_out.fifo exist, bytes are read from the first and written to the
 * second (a tool outside the container plays the other end); otherwise
 * what is written comes back to be read (a loopback).
 */
#define FMRB_UART_SIM_DIR "/project"

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

/**
 * @brief How many received bytes wait to be read (none waited for)
 *
 * @param handle UART handle
 * @param len Output: the count
 * @return fmrb_err_t FMRB_OK on success
 */
fmrb_err_t fmrb_hal_uart_rx_available(fmrb_uart_handle_t handle, size_t *len);

/**
 * @brief Drop every received byte not read yet
 */
fmrb_err_t fmrb_hal_uart_flush_rx(fmrb_uart_handle_t handle);

/**
 * @brief Wait until what was written has left the wire
 *
 * @param timeout_ms The longest wait
 * @return fmrb_err_t FMRB_OK when sent, FMRB_ERR_TIMEOUT when the wait ran out
 */
fmrb_err_t fmrb_hal_uart_wait_tx(fmrb_uart_handle_t handle, uint32_t timeout_ms);

/**
 * @brief Hold the TX line low (a break) for ms milliseconds, after what was
 *        written has been sent. Waits for the whole break.
 */
fmrb_err_t fmrb_hal_uart_send_break(fmrb_uart_handle_t handle, uint32_t ms);

/**
 * @brief Change the baud rate
 *
 * @param baud_rate The rate asked for
 * @param actual Output (can be NULL): the rate the hardware runs at
 */
fmrb_err_t fmrb_hal_uart_set_baud(fmrb_uart_handle_t handle, uint32_t baud_rate, uint32_t *actual);

/**
 * @brief Change the frame format: data_bits 5-8, stop_bits 1 or 2,
 *        parity FMRB_UART_PARITY_*
 *
 * @return fmrb_err_t FMRB_ERR_INVALID_PARAM for a value out of range
 */
fmrb_err_t fmrb_hal_uart_set_format(fmrb_uart_handle_t handle, uint8_t data_bits, uint8_t stop_bits,
                                    uint8_t parity);

#ifdef __cplusplus
}
#endif
