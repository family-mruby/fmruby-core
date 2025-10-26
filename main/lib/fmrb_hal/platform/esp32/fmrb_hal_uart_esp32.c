#include "fmrb_hal_uart.h"
#include "fmrb_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdlib.h>

#define UART_BUF_SIZE (1024)

// Internal UART context structure
typedef struct {
    uart_port_t uart_num;
    uint32_t timeout_ms;
} fmrb_uart_ctx_t;

fmrb_err_t fmrb_hal_uart_open(const fmrb_uart_config_t *config, fmrb_uart_handle_t *handle)
{
    if (!config || !handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Validate UART port number
    if (config->uart_num < 0 || config->uart_num >= UART_NUM_MAX) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Allocate context
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)malloc(sizeof(fmrb_uart_ctx_t));
    if (!ctx) {
        return FMRB_ERR_NO_MEMORY;
    }

    ctx->uart_num = (uart_port_t)config->uart_num;
    ctx->timeout_ms = config->timeout_ms;

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(ctx->uart_num, &uart_config);
    if (err != ESP_OK) {
        free(ctx);
        return FMRB_ERR_FAILED;
    }

    // Set UART pins
    err = uart_set_pin(ctx->uart_num, config->tx_pin, config->rx_pin,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        free(ctx);
        return FMRB_ERR_FAILED;
    }

    // Install UART driver
    err = uart_driver_install(ctx->uart_num, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        free(ctx);
        return FMRB_ERR_FAILED;
    }

    *handle = (fmrb_uart_handle_t)ctx;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_close(fmrb_uart_handle_t handle)
{
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    uart_driver_delete(ctx->uart_num);
    free(ctx);

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_read(fmrb_uart_handle_t handle, void *buffer, size_t size, size_t *bytes_read)
{
    if (!handle || !buffer) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    // Convert timeout to ticks
    TickType_t timeout_ticks = (ctx->timeout_ms == 0) ? 0 : pdMS_TO_TICKS(ctx->timeout_ms);

    int len = uart_read_bytes(ctx->uart_num, buffer, size, timeout_ticks);

    if (len < 0) {
        return FMRB_ERR_FAILED;
    }

    if (bytes_read) {
        *bytes_read = (size_t)len;
    }

    if (len == 0) {
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_write(fmrb_uart_handle_t handle, const void *buffer, size_t size, size_t *bytes_written)
{
    if (!handle || !buffer) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    int len = uart_write_bytes(ctx->uart_num, buffer, size);

    if (len < 0) {
        return FMRB_ERR_FAILED;
    }

    if (bytes_written) {
        *bytes_written = (size_t)len;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_read_byte(fmrb_uart_handle_t handle, uint8_t *byte)
{
    if (!handle || !byte) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    // Non-blocking read (timeout = 0)
    int len = uart_read_bytes(ctx->uart_num, byte, 1, 0);

    if (len < 0) {
        return FMRB_ERR_FAILED;
    }

    if (len == 0) {
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}
