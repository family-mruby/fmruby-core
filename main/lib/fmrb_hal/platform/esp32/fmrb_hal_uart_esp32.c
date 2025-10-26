#include "fmrb_hal_uart.h"
#include "fmrb_err.h"

// TODO: Implement ESP32 UART driver using ESP-IDF UART API

fmrb_err_t fmrb_hal_uart_open(const fmrb_uart_config_t *config, fmrb_uart_handle_t *handle)
{
    // TODO: Implement ESP32 UART open
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t fmrb_hal_uart_close(fmrb_uart_handle_t handle)
{
    // TODO: Implement ESP32 UART close
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t fmrb_hal_uart_read(fmrb_uart_handle_t handle, void *buffer, size_t size, size_t *bytes_read)
{
    // TODO: Implement ESP32 UART read
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t fmrb_hal_uart_write(fmrb_uart_handle_t handle, const void *buffer, size_t size, size_t *bytes_written)
{
    // TODO: Implement ESP32 UART write
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t fmrb_hal_uart_read_byte(fmrb_uart_handle_t handle, uint8_t *byte)
{
    // TODO: Implement ESP32 UART read byte
    return FMRB_ERR_NOT_SUPPORTED;
}
