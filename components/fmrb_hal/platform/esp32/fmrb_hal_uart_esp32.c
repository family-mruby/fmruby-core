#include "fmrb_hal_uart.h"
#include "fmrb_err.h"
#include "fmrb_mem.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define UART_BUF_SIZE (1024)

// Internal UART context structure
typedef struct {
    uart_port_t uart_num;
    uint32_t timeout_ms;
} fmrb_uart_ctx_t;

static bool word_length_of(uint8_t data_bits, uart_word_length_t *out)
{
    switch (data_bits == 0 ? 8 : data_bits) {
    case 5: *out = UART_DATA_5_BITS; return true;
    case 6: *out = UART_DATA_6_BITS; return true;
    case 7: *out = UART_DATA_7_BITS; return true;
    case 8: *out = UART_DATA_8_BITS; return true;
    default: return false;
    }
}

static bool stop_bits_of(uint8_t stop_bits, uart_stop_bits_t *out)
{
    switch (stop_bits == 0 ? 1 : stop_bits) {
    case 1: *out = UART_STOP_BITS_1; return true;
    case 2: *out = UART_STOP_BITS_2; return true;
    default: return false;
    }
}

static bool parity_of(uint8_t parity, uart_parity_t *out)
{
    switch (parity) {
    case FMRB_UART_PARITY_NONE: *out = UART_PARITY_DISABLE; return true;
    case FMRB_UART_PARITY_EVEN: *out = UART_PARITY_EVEN; return true;
    case FMRB_UART_PARITY_ODD: *out = UART_PARITY_ODD; return true;
    default: return false;
    }
}

fmrb_err_t fmrb_hal_uart_open(const fmrb_uart_config_t *config, fmrb_uart_handle_t *handle)
{
    if (!config || !handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Validate UART port number
    if (config->uart_num < 0 || config->uart_num >= UART_NUM_MAX) {
        return FMRB_ERR_INVALID_PARAM;
    }

    uart_word_length_t word;
    uart_stop_bits_t stop;
    uart_parity_t parity;
    if (!word_length_of(config->data_bits, &word) || !stop_bits_of(config->stop_bits, &stop) ||
        !parity_of(config->parity, &parity)) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Allocate context
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)fmrb_sys_malloc(sizeof(fmrb_uart_ctx_t));
    if (!ctx) {
        return FMRB_ERR_NO_MEMORY;
    }

    ctx->uart_num = (uart_port_t)config->uart_num;
    ctx->timeout_ms = config->timeout_ms;

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = word,
        .parity = parity,
        .stop_bits = stop,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(ctx->uart_num, &uart_config);
    if (err != ESP_OK) {
        fmrb_sys_free(ctx);
        return FMRB_ERR_FAILED;
    }

    // Set UART pins
    err = uart_set_pin(ctx->uart_num, config->tx_pin, config->rx_pin,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        fmrb_sys_free(ctx);
        return FMRB_ERR_FAILED;
    }

    // Install UART driver. Its buffers are in internal RAM (the driver's
    // own allocation) and only while the port is open.
    int rx_size = config->rx_buffer_size ? (int)config->rx_buffer_size : UART_BUF_SIZE;
    err = uart_driver_install(ctx->uart_num, rx_size, UART_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        fmrb_sys_free(ctx);
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
    fmrb_sys_free(ctx);

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

fmrb_err_t fmrb_hal_uart_rx_available(fmrb_uart_handle_t handle, size_t *len)
{
    if (!handle || !len) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    size_t n = 0;
    if (uart_get_buffered_data_len(ctx->uart_num, &n) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    *len = n;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_flush_rx(fmrb_uart_handle_t handle)
{
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    return uart_flush_input(ctx->uart_num) == ESP_OK ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t fmrb_hal_uart_wait_tx(fmrb_uart_handle_t handle, uint32_t timeout_ms)
{
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    esp_err_t err = uart_wait_tx_done(ctx->uart_num, pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_ERR_TIMEOUT) {
        return FMRB_ERR_TIMEOUT;
    }
    return err == ESP_OK ? FMRB_OK : FMRB_ERR_FAILED;
}

// The driver's break (uart_write_bytes_with_break) counts bit times up to
// 255, too short for a break in ms at most rates: the idle-high TX line is
// inverted instead, which holds it low for exactly as long as asked.
fmrb_err_t fmrb_hal_uart_send_break(fmrb_uart_handle_t handle, uint32_t ms)
{
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    uart_wait_tx_done(ctx->uart_num, pdMS_TO_TICKS(1000));
    if (uart_set_line_inverse(ctx->uart_num, UART_SIGNAL_TXD_INV) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1);
    uart_set_line_inverse(ctx->uart_num, UART_SIGNAL_INV_DISABLE);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_set_baud(fmrb_uart_handle_t handle, uint32_t baud_rate, uint32_t *actual)
{
    if (!handle || baud_rate == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (uart_set_baudrate(ctx->uart_num, baud_rate) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    if (actual) {
        uint32_t got = baud_rate;
        if (uart_get_baudrate(ctx->uart_num, &got) != ESP_OK) {
            got = baud_rate;
        }
        *actual = got;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_set_format(fmrb_uart_handle_t handle, uint8_t data_bits, uint8_t stop_bits,
                                    uint8_t parity)
{
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    uart_word_length_t word;
    uart_stop_bits_t stop;
    uart_parity_t par;
    if (!word_length_of(data_bits, &word) || !stop_bits_of(stop_bits, &stop) || !parity_of(parity, &par)) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (uart_set_word_length(ctx->uart_num, word) != ESP_OK ||
        uart_set_stop_bits(ctx->uart_num, stop) != ESP_OK ||
        uart_set_parity(ctx->uart_num, par) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}
