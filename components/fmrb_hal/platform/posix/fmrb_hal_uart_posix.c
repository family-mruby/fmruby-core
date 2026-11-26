#include "fmrb_hal_uart.h"
#include "fmrb_err.h"
#include "fmrb_mem.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>

// UART context for POSIX. A device (device_path) reads and writes one fd; the
// simulation's stand-in for a numbered UART reads in_fd and writes out_fd: two
// FIFOs, or the two ends of a pipe (the loopback).
typedef struct {
    int in_fd;
    int out_fd;
    uint32_t timeout_ms;
    bool tty;
} fmrb_uart_ctx_t;

static bool is_fifo(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISFIFO(st.st_mode);
}

// The stand-in: uartN_in.fifo / uartN_out.fifo when both are there (opened
// read-write so neither end ever reads as closed, the way MIDI's FIFO is),
// otherwise a pipe whose write end feeds its read end.
static fmrb_err_t open_stand_in(int num, fmrb_uart_ctx_t *ctx)
{
    char in_path[64];
    char out_path[64];
    snprintf(in_path, sizeof(in_path), "%s/uart%d_in.fifo", FMRB_UART_SIM_DIR, num);
    snprintf(out_path, sizeof(out_path), "%s/uart%d_out.fifo", FMRB_UART_SIM_DIR, num);
    if (is_fifo(in_path) && is_fifo(out_path)) {
        ctx->in_fd = open(in_path, O_RDWR | O_NONBLOCK);
        ctx->out_fd = ctx->in_fd >= 0 ? open(out_path, O_RDWR | O_NONBLOCK) : -1;
        if (ctx->in_fd < 0 || ctx->out_fd < 0) {
            if (ctx->in_fd >= 0) {
                close(ctx->in_fd);
            }
            return FMRB_ERR_FAILED;
        }
        return FMRB_OK;
    }
    int fds[2];
    if (pipe(fds) != 0) {
        return FMRB_ERR_FAILED;
    }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    ctx->in_fd = fds[0];
    ctx->out_fd = fds[1];
    return FMRB_OK;
}

static bool speed_of(uint32_t baud_rate, speed_t *out)
{
    switch (baud_rate) {
    case 1200:   *out = B1200; return true;
    case 2400:   *out = B2400; return true;
    case 4800:   *out = B4800; return true;
    case 9600:   *out = B9600; return true;
    case 19200:  *out = B19200; return true;
    case 38400:  *out = B38400; return true;
    case 57600:  *out = B57600; return true;
    case 115200: *out = B115200; return true;
    case 230400: *out = B230400; return true;
    default:     return false;
    }
}

static bool format_ok(uint8_t data_bits, uint8_t stop_bits, uint8_t parity)
{
    uint8_t d = data_bits == 0 ? 8 : data_bits;
    uint8_t s = stop_bits == 0 ? 1 : stop_bits;
    return d >= 5 && d <= 8 && (s == 1 || s == 2) && parity <= FMRB_UART_PARITY_ODD;
}

static void apply_format(struct termios *tty, uint8_t data_bits, uint8_t stop_bits, uint8_t parity)
{
    static const tcflag_t sizes[] = { CS5, CS6, CS7, CS8 };
    uint8_t d = data_bits == 0 ? 8 : data_bits;
    tty->c_cflag &= ~CSIZE;
    tty->c_cflag |= sizes[d - 5];
    if (stop_bits == 2) {
        tty->c_cflag |= CSTOPB;
    } else {
        tty->c_cflag &= ~CSTOPB;
    }
    tty->c_cflag &= ~(PARENB | PARODD);
    if (parity == FMRB_UART_PARITY_EVEN) {
        tty->c_cflag |= PARENB;
    } else if (parity == FMRB_UART_PARITY_ODD) {
        tty->c_cflag |= PARENB | PARODD;
    }
}

static void close_ctx(fmrb_uart_ctx_t *ctx)
{
    if (ctx->out_fd >= 0 && ctx->out_fd != ctx->in_fd) {
        close(ctx->out_fd);
    }
    if (ctx->in_fd >= 0) {
        close(ctx->in_fd);
    }
    fmrb_sys_free(ctx);
}

fmrb_err_t fmrb_hal_uart_open(const fmrb_uart_config_t *config, fmrb_uart_handle_t *handle)
{
    if (!config || !handle || !format_ok(config->data_bits, config->stop_bits, config->parity)) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)fmrb_sys_malloc(sizeof(fmrb_uart_ctx_t));
    if (!ctx) {
        return FMRB_ERR_NO_MEMORY;
    }
    ctx->in_fd = -1;
    ctx->out_fd = -1;
    ctx->timeout_ms = config->timeout_ms;
    ctx->tty = false;

    if (config->device_path == NULL) {
        if (config->uart_num < 0 || open_stand_in(config->uart_num, ctx) != FMRB_OK) {
            fmrb_sys_free(ctx);
            return FMRB_ERR_FAILED;
        }
        *handle = (fmrb_uart_handle_t)ctx;
        return FMRB_OK;
    }

    // Open device
    int fd = open(config->device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fmrb_sys_free(ctx);
        return FMRB_ERR_FAILED;
    }
    ctx->in_fd = fd;
    ctx->out_fd = fd;

    // A device path in the simulation is not necessarily a terminal: MIDI
    // output is pointed at a FIFO so a tool outside the container can read
    // the bytes. termios settings (baud, 8N1, flow control) mean nothing
    // there, so configure them only for a real tty and treat anything else
    // as an already-raw byte stream.
    if (!isatty(fd)) {
        *handle = (fmrb_uart_handle_t)ctx;
        return FMRB_OK;
    }
    ctx->tty = true;

    // Configure terminal settings
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        close_ctx(ctx);
        return FMRB_ERR_FAILED;
    }

    // Raw mode
    cfmakeraw(&tty);

    // Set baud rate
    speed_t speed;
    if (!speed_of(config->baud_rate, &speed)) {
        speed = B115200;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    apply_format(&tty, config->data_bits, config->stop_bits, config->parity);

    // No flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Non-blocking reads
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close_ctx(ctx);
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

    close_ctx((fmrb_uart_ctx_t *)handle);
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
    FD_SET(ctx->in_fd, &readfds);

    tv.tv_sec = ctx->timeout_ms / 1000;
    tv.tv_usec = (ctx->timeout_ms % 1000) * 1000;

    int ret = select(ctx->in_fd + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) {
        return FMRB_ERR_FAILED;
    } else if (ret == 0) {
        if (bytes_read) *bytes_read = 0;
        return FMRB_ERR_TIMEOUT;
    }

    ssize_t n = read(ctx->in_fd, buffer, size);
    if (n < 0) {
        if (bytes_read) *bytes_read = 0;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? FMRB_ERR_TIMEOUT : FMRB_ERR_FAILED;
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

    ssize_t n = write(ctx->out_fd, buffer, size);
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
    ssize_t n = read(ctx->in_fd, byte, 1);
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

fmrb_err_t fmrb_hal_uart_rx_available(fmrb_uart_handle_t handle, size_t *len)
{
    if (!handle || !len) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    int n = 0;
    if (ioctl(ctx->in_fd, FIONREAD, &n) != 0 || n < 0) {
        n = 0;
    }
    *len = (size_t)n;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_flush_rx(fmrb_uart_handle_t handle)
{
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (ctx->tty) {
        return tcflush(ctx->in_fd, TCIFLUSH) == 0 ? FMRB_OK : FMRB_ERR_FAILED;
    }
    uint8_t buf[256];
    while (read(ctx->in_fd, buf, sizeof(buf)) > 0) {
    }
    return FMRB_OK;
}

// A FIFO or a pipe has nothing on a wire to wait for.
fmrb_err_t fmrb_hal_uart_wait_tx(fmrb_uart_handle_t handle, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (ctx->tty && tcdrain(ctx->out_fd) != 0) {
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_send_break(fmrb_uart_handle_t handle, uint32_t ms)
{
    (void)ms;
    if (!handle) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (ctx->tty && tcsendbreak(ctx->out_fd, 0) != 0) {
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

// Only a tty has a rate; the stand-in answers the rate asked for.
fmrb_err_t fmrb_hal_uart_set_baud(fmrb_uart_handle_t handle, uint32_t baud_rate, uint32_t *actual)
{
    if (!handle || baud_rate == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (ctx->tty) {
        speed_t speed;
        struct termios tty;
        if (!speed_of(baud_rate, &speed) || tcgetattr(ctx->in_fd, &tty) != 0) {
            return FMRB_ERR_FAILED;
        }
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);
        if (tcsetattr(ctx->in_fd, TCSANOW, &tty) != 0) {
            return FMRB_ERR_FAILED;
        }
    }
    if (actual) {
        *actual = baud_rate;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_uart_set_format(fmrb_uart_handle_t handle, uint8_t data_bits, uint8_t stop_bits,
                                    uint8_t parity)
{
    if (!handle || !format_ok(data_bits, stop_bits, parity)) {
        return FMRB_ERR_INVALID_PARAM;
    }
    fmrb_uart_ctx_t *ctx = (fmrb_uart_ctx_t *)handle;
    if (ctx->tty) {
        struct termios tty;
        if (tcgetattr(ctx->in_fd, &tty) != 0) {
            return FMRB_ERR_FAILED;
        }
        apply_format(&tty, data_bits, stop_bits, parity);
        if (tcsetattr(ctx->in_fd, TCSANOW, &tty) != 0) {
            return FMRB_ERR_FAILED;
        }
    }
    return FMRB_OK;
}
