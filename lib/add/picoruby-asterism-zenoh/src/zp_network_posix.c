/*
 * Socket helpers for zenoh-pico on POSIX, used by picoruby-asterism-zenoh in place of
 * zenoh-pico's src/system/unix/network.c (the gem compiles this file and
 * leaves that one out; the zenoh-pico checkout itself is not edited).
 * Based on that file (Copyright (c) 2022 ZettaScale Technology,
 * EPL-2.0 OR Apache-2.0) with one change: _z_socket_wait_readable retries
 * select() on EINTR.
 *
 * The peer read path waits on all peer sockets with this function. Upstream
 * reports EINTR as an error, and the read task then stops for good (the
 * session stays open but never reads again). A host that drives its own
 * scheduler with signals (a FreeRTOS POSIX simulator ticks with SIGALRM)
 * interrupts system calls all the time. Retrying keeps the total wait within
 * the requested time. The gem also compiles zenoh-pico's unicast read path
 * with a zero wait (see mrbgem.rake), so this never blocks a poll.
 */
#if defined(ZENOH_LINUX) || defined(ZENOH_MACOS) || defined(ZENOH_BSD)

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/transport/socket.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/logging.h"

z_result_t _z_socket_set_blocking(const _z_sys_net_socket_t *sock, bool blocking) {
    int flags = fcntl(sock->_fd, F_GETFL, 0);
    if (flags == -1) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    if (fcntl(sock->_fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == -1) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    return _Z_RES_OK;
}

void _z_socket_close(_z_sys_net_socket_t *sock) {
    if (sock->_fd >= 0) {
        shutdown(sock->_fd, SHUT_RDWR);
        close(sock->_fd);
        sock->_fd = -1;
    }
}

static long zp_net_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

z_result_t _z_socket_wait_readable(_z_socket_wait_iter_t *iter, uint32_t timeout_ms) {
    long deadline = zp_net_mono_ms() + (long)timeout_ms;
    fd_set read_fds;
    int result;
    for (;;) {
        int max_fd = 0;
        bool has_sockets = false;
        FD_ZERO(&read_fds);
        _z_socket_wait_iter_reset(iter);
        while (_z_socket_wait_iter_next(iter)) {
            const _z_sys_net_socket_t *sock = _z_socket_wait_iter_get_socket(iter);
            _z_socket_wait_iter_set_ready(iter, false);
            FD_SET(sock->_fd, &read_fds);
            if (sock->_fd > max_fd) {
                max_fd = sock->_fd;
            }
            has_sockets = true;
        }
        if (!has_sockets) {
            return _Z_RES_OK;
        }

        long left = deadline - zp_net_mono_ms();
        if (left < 0) {
            left = 0;
        }
        struct timeval timeout = {
            .tv_sec = (time_t)(left / 1000L),
            .tv_usec = (suseconds_t)((left % 1000L) * 1000L),
        };
        result = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (result >= 0) {
            break;
        }
        if (errno != EINTR) {
            _Z_DEBUG("Errno: %d\n", errno);
            _Z_ERROR_RETURN(_Z_ERR_GENERIC);
        }
    }

    bool has_data = false;
    _z_socket_wait_iter_reset(iter);
    while (_z_socket_wait_iter_next(iter)) {
        const _z_sys_net_socket_t *sock = _z_socket_wait_iter_get_socket(iter);
        bool is_ready = FD_ISSET(sock->_fd, &read_fds);
        _z_socket_wait_iter_set_ready(iter, is_ready);
        has_data |= is_ready;
    }
    return has_data ? _Z_RES_OK : _Z_NO_DATA_PROCESSED;
}

#endif /* POSIX */
