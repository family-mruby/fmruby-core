/*
 * TCP link for zenoh-pico on POSIX, used by picoruby-zenoh in place of
 * zenoh-pico's src/link/transport/tcp/tcp_posix.c (the gem compiles this file
 * and leaves that one out; the zenoh-pico checkout itself is not edited).
 * Based on that file (Copyright (c) 2026 ZettaScale Technology,
 * EPL-2.0 OR Apache-2.0) with three changes for a polled, single-threaded
 * session living inside an interpreter loop:
 *
 * 1. The plain read (_z_tcp_read) never blocks. It is what the session's read
 *    task calls on every zp_spin_once(); upstream waits up to the socket
 *    timeout (SO_RCVTIMEO) there, which would stall the caller's loop for that
 *    long on every poll with no traffic. Here "no data yet" returns SIZE_MAX
 *    at once, which the read task already treats as "nothing to read".
 * 2. Every blocking call is retried on EINTR. A host that drives its own
 *    scheduler with signals (a FreeRTOS POSIX simulator ticks with SIGALRM,
 *    installed without SA_RESTART) interrupts recv/send/connect/poll all the
 *    time; upstream reports those as link errors and drops the session.
 * 3. connect() has a time limit (PICORUBY_ZENOH_CONNECT_TIMEOUT_MS), so an
 *    unreachable router fails the open in seconds instead of the kernel's
 *    minutes.
 *
 * _z_tcp_read_exact (session handshake) still waits, up to the socket timeout
 * per call, and keeps waiting once a message has started arriving so a
 * partially received message is never cut in half.
 */
#include "zenoh-pico/link/transport/tcp.h"

#if defined(ZP_PLATFORM_SOCKET_POSIX)

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/pointers.h"

#ifndef PICORUBY_ZENOH_CONNECT_TIMEOUT_MS
#define PICORUBY_ZENOH_CONNECT_TIMEOUT_MS 3000
#endif

/* A message that has started arriving is given this long to complete. */
#ifndef PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS
#define PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS 5000
#endif

static long zp_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

/* poll() one fd, retrying EINTR while keeping the total wait bounded.
 * Returns >0 when ready, 0 on timeout, <0 on error. */
static int zp_poll_one(int fd, short events, int timeout_ms) {
    long deadline = zp_mono_ms() + timeout_ms;
    for (;;) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = events;
        pfd.revents = 0;
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret >= 0) {
            return ret;
        }
        if (errno != EINTR) {
            return -1;
        }
        long left = deadline - zp_mono_ms();
        if (left <= 0) {
            return 0;
        }
        timeout_ms = (int)left;
    }
}

static z_result_t zp_set_sockopts(int fd) {
    int flags = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags)) < 0) {
        return _Z_ERR_GENERIC;
    }
#if Z_FEATURE_TCP_NODELAY == 1
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) < 0) {
        return _Z_ERR_GENERIC;
    }
#endif
    return _Z_RES_OK;
}

static z_result_t zp_set_nonblocking(int fd, bool on) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl == -1) {
        return _Z_ERR_GENERIC;
    }
    fl = on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK);
    return (fcntl(fd, F_SETFL, fl) == -1) ? _Z_ERR_GENERIC : _Z_RES_OK;
}

/* connect() with a time limit; EINTR does not abort the attempt (the kernel
 * keeps connecting in the background, so wait for it like EINPROGRESS). */
static bool zp_connect_timed(int fd, const struct sockaddr *addr, socklen_t len, int timeout_ms) {
    if (zp_set_nonblocking(fd, true) != _Z_RES_OK) {
        return false;
    }
    bool ok = false;
    int ret = connect(fd, addr, len);
    if (ret == 0) {
        ok = true;
    } else if (errno == EINPROGRESS || errno == EINTR) {
        if (zp_poll_one(fd, POLLOUT, timeout_ms) > 0) {
            int err = 0;
            socklen_t elen = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) {
                ok = true;
            }
        }
    }
    if (zp_set_nonblocking(fd, false) != _Z_RES_OK) {
        ok = false;
    }
    return ok;
}

z_result_t _z_tcp_endpoint_init(_z_sys_net_endpoint_t *ep, const char *s_address, const char *s_port) {
    struct addrinfo hints;
    ep->_iptcp = NULL;
    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_TCP;

    int ret;
    int tries = 0;
    do {
        ret = getaddrinfo(s_address, s_port, &hints, &ep->_iptcp);
        /* A signal can surface as EAI_SYSTEM/EINTR or EAI_AGAIN; retry a few times. */
    } while (ret != 0 && ((ret == EAI_SYSTEM && errno == EINTR) || ret == EAI_AGAIN) && ++tries < 3);
    if (ret != 0) {
        ep->_iptcp = NULL;
        _Z_ERROR_LOG(_Z_ERR_GENERIC);
        return _Z_ERR_GENERIC;
    }
    return _Z_RES_OK;
}

void _z_tcp_endpoint_clear(_z_sys_net_endpoint_t *ep) {
    if ((ep == NULL) || (ep->_iptcp == NULL)) {
        return;
    }
    freeaddrinfo(ep->_iptcp);
    ep->_iptcp = NULL;
}

z_result_t _z_tcp_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint, uint32_t tout) {
    (void)tout;
    sock->_fd = -1;
    for (struct addrinfo *it = endpoint._iptcp; it != NULL; it = it->ai_next) {
        int fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == -1) {
            continue;
        }
        if (zp_set_sockopts(fd) == _Z_RES_OK &&
            zp_connect_timed(fd, it->ai_addr, it->ai_addrlen, PICORUBY_ZENOH_CONNECT_TIMEOUT_MS)) {
            sock->_fd = fd;
            return _Z_RES_OK;
        }
        close(fd);
    }
    _Z_ERROR_LOG(_Z_ERR_GENERIC);
    return _Z_ERR_GENERIC;
}

z_result_t _z_tcp_listen(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint) {
    /* Client mode only (Z_FEATURE_UNICAST_PEER=0): nothing listens. */
    (void)endpoint;
    sock->_fd = -1;
    _Z_ERROR_RETURN(_Z_ERR_GENERIC);
}

z_result_t _z_tcp_accept(const _z_sys_net_socket_t *sock_in, _z_sys_net_socket_t *sock_out) {
    (void)sock_in;
    sock_out->_fd = -1;
    _Z_ERROR_RETURN(_Z_ERR_GENERIC);
}

void _z_tcp_close(_z_sys_net_socket_t *sock) {
    if (sock->_fd >= 0) {
        shutdown(sock->_fd, SHUT_RDWR);
        close(sock->_fd);
        sock->_fd = -1;
    }
}

/* One recv() that does not wait: SIZE_MAX when nothing is there (or on a
 * transient error), 0 when the peer closed, otherwise the byte count. */
static size_t zp_recv_now(int fd, uint8_t *ptr, size_t len) {
    for (;;) {
        ssize_t rb = recv(fd, ptr, len, MSG_DONTWAIT);
        if (rb >= 0) {
            return (size_t)rb;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            _Z_DEBUG("Errno: %d\n", errno);
        }
        return SIZE_MAX;
    }
}

size_t _z_tcp_read(_z_sys_net_socket_t sock, uint8_t *ptr, size_t len) { return zp_recv_now(sock._fd, ptr, len); }

size_t _z_tcp_read_exact(_z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    size_t n = 0;
    uint8_t *pos = &ptr[0];
    long started = 0;

    while (n != len) {
        /* Before the first byte: wait one socket timeout and report a timeout
         * (the caller loops until its own deadline). After it: keep waiting
         * so the message is not cut in half. */
        int wait_ms = Z_CONFIG_SOCKET_TIMEOUT;
        if (n > 0) {
            long left = PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS - (zp_mono_ms() - started);
            if (left <= 0) {
                return SIZE_MAX;
            }
            wait_ms = (int)left;
        }
        int pr = zp_poll_one(sock._fd, POLLIN, wait_ms);
        if (pr < 0) {
            return SIZE_MAX;
        }
        if (pr == 0) {
            if (n == 0) {
                return SIZE_MAX;
            }
            continue;
        }
        size_t rb = zp_recv_now(sock._fd, pos, len - n);
        if (rb == 0) {
            return 0;
        }
        if (rb == SIZE_MAX) {
            continue;
        }
        if (n == 0) {
            started = zp_mono_ms();
        }
        n += rb;
        pos = _z_ptr_u8_offset(pos, (ptrdiff_t)rb);
    }
    return n;
}

size_t _z_tcp_write(_z_sys_net_socket_t sock, const uint8_t *ptr, size_t len) {
    for (;;) {
        ssize_t sb = send(sock._fd, ptr, len, MSG_NOSIGNAL);
        if (sb >= 0) {
            return (size_t)sb;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Socket buffer full: wait for room rather than fail the link. */
            if (zp_poll_one(sock._fd, POLLOUT, Z_CONFIG_SOCKET_TIMEOUT) > 0) {
                continue;
            }
        }
        return SIZE_MAX;
    }
}

#endif /* defined(ZP_PLATFORM_SOCKET_POSIX) */
