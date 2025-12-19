#include "fmrb_hal_uart.h"
#include "fmrb_err.h"
#include "fmrb_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>

// UART context for POSIX
typedef struct {
    int fd;
    uint32_t timeout_ms;
} fmrb_uart_ctx_t;

fmrb_err_t fmrb_hal_uart_open(const fmrb_uart_config_t *config, fmrb_uart_handle_t *handle)
{
    if (!config || !handle) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)fmrb_sys_malloc(sizeof(fmrb_uart_ctx_t));
    if (!ctx) {
        return FMRB_ERR_NO_MEMORY;
    }

    // Open device
    ctx->fd = open(config->device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ctx->fd < 0) {
        fmrb_sys_free(ctx);
        return FMRB_ERR_FAILED;
    }

    ctx->timeout_ms = config->timeout_ms;

    // Configure terminal settings
    struct termios tty;
    if (tcgetattr(ctx->fd, &tty) != 0) {
        close(ctx->fd);
        fmrb_sys_free(ctx);
        return FMRB_ERR_FAILED;
    }

    // Raw mode
    cfmakeraw(&tty);

    // Set baud rate
    speed_t speed;
    switch (config->baud_rate) {
        case 9600:   speed = B9600; break;
        case 19200:  speed = B19200; break;
        case 38400:  speed = B38400; break;
        case 57600:  speed = B57600; break;
        case 115200: speed = B115200; break;
        default:     speed = B115200; break;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;  // No parity
    tty.c_cflag &= ~CSTOPB;  // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;      // 8 data bits

    // No flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Non-blocking reads
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(ctx->fd, TCSANOW, &tty) != 0) {
        close(ctx->fd);
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
    close(ctx->fd);
    fmrb_sys_free(ctx);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_read(fmrb_uart_handle_t handle, void *buffer, size_t size, size_t *bytes_read)
{
    if (!handle || !buffer) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    // Use select for timeout
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(ctx->fd, &readfds);

    tv.tv_sec = ctx->timeout_ms / 1000;
    tv.tv_usec = (ctx->timeout_ms % 1000) * 1000;

    int ret = select(ctx->fd + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) {
        return FMRB_ERR_FAILED;
    } else if (ret == 0) {
        if (bytes_read) *bytes_read = 0;
        return FMRB_ERR_TIMEOUT;
    }

    ssize_t n = read(ctx->fd, buffer, size);
    if (n < 0) {
        if (bytes_read) *bytes_read = 0;
        return FMRB_ERR_FAILED;
    }

    if (bytes_read) {
        *bytes_read = (size_t)n;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_write(fmrb_uart_handle_t handle, const void *buffer, size_t size, size_t *bytes_written)
{
    if (!handle || !buffer) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    ssize_t n = write(ctx->fd, buffer, size);
    if (n < 0) {
        if (bytes_written) *bytes_written = 0;
        return FMRB_ERR_FAILED;
    }

    if (bytes_written) {
        *bytes_written = (size_t)n;
    }

    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_read_byte(fmrb_uart_handle_t handle, uint8_t *byte)
{
    if (!handle || !byte) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;

    // Non-blocking read
    ssize_t n = read(ctx->fd, byte, 1);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return FMRB_ERR_TIMEOUT;
        }
        return FMRB_ERR_FAILED;
    } else if (n == 0) {
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}
