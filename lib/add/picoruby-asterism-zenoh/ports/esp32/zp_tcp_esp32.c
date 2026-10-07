/*
 * TCP link for zenoh-pico on ESP-IDF (lwIP sockets), used by picoruby-asterism-zenoh
 * in place of zenoh-pico's src/link/transport/tcp/tcp_esp32.c (that file is
 * not compiled; the zenoh-pico checkout itself is not edited).
 * Based on it (Copyright (c) 2026 ZettaScale Technology,
 * EPL-2.0 OR Apache-2.0), with the same changes as this gem's POSIX link
 * (src/zp_tcp_posix.c) for a polled, single-threaded session living inside
 * an interpreter loop:
 *
 * 1. The plain read (_z_tcp_read) never blocks. The session's read task calls
 *    it on every zp_spin_once(). Upstream sets no receive timeout at all on
 *    ESP-IDF, so a poll with no traffic would block the caller until the
 *    router sends something (keep-alives come every few seconds). Here "no
 *    data yet" returns SIZE_MAX at once, which the read task already treats
 *    as "nothing to read".
 * 2. connect() has a time limit (PICORUBY_ZENOH_CONNECT_TIMEOUT_MS). Upstream
 *    blocks until lwIP gives up, which for a silent host is lwIP's SYN
 *    retransmission schedule.
 * 3. The handshake read (_z_tcp_read_exact) waits at most one socket timeout
 *    for the first byte (the caller loops until its own deadline), then keeps
 *    waiting, up to PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS, so a message that
 *    has started arriving is not cut in half. Upstream blocks with no limit.
 * 4. A send has a time limit (PICORUBY_ZENOH_SEND_TIMEOUT_MS, for the whole
 *    message). Upstream blocks until lwIP gives up retransmitting, which can
 *    take minutes when the WiFi is gone. When the limit runs out the socket
 *    is shut down, and zp_tcp_socket_closed() reports it, like a peer that
 *    closed the connection (see include/picoruby_zenoh_link.h).
 * 5. No SO_LINGER: closing never waits for unsent data (upstream lingers up to
 *    the lease time when lwIP has SO_LINGER). The session sends its close
 *    message before the socket is closed either way.
 * 6. Peer mode: listen/accept for a non-blocking listening socket polled by
 *    zenoh-pico's runtime (no accept task of its own).
 *
 * EINTR does not happen on ESP-IDF, but retrying it keeps the code identical
 * in shape to the POSIX link.
 * Compiled by the ESP-IDF component (lwIP headers).
 */
#include "zenoh-pico/link/transport/tcp.h"

#if defined(ZP_PLATFORM_SOCKET_ESP32)

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lwip/sockets.h"
#include "picoruby_zenoh_link.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/pointers.h"

static long zp_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

/* Wait until one fd is readable (for_write false) or writable, at most
 * timeout_ms. Returns >0 when ready, 0 on timeout, <0 on error. */
static int zp_wait_one(int fd, bool for_write, int timeout_ms) {
    long deadline = zp_mono_ms() + timeout_ms;
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        int ret = for_write ? select(fd + 1, NULL, &fds, NULL, &tv) : select(fd + 1, &fds, NULL, NULL, &tv);
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

/* connect() with a time limit. The socket is left blocking afterwards. */
static bool zp_connect_timed(int fd, const struct sockaddr *addr, socklen_t len, int timeout_ms) {
    if (zp_set_nonblocking(fd, true) != _Z_RES_OK) {
        return false;
    }
    bool ok = false;
    int ret = connect(fd, addr, len);
    if (ret == 0) {
        ok = true;
    } else if (errno == EINPROGRESS || errno == EINTR) {
        if (zp_wait_one(fd, true, timeout_ms) > 0) {
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

    if (getaddrinfo(s_address, s_port, &hints, &ep->_iptcp) != 0) {
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

/* Listening socket for a peer session. zenoh-pico makes it non-blocking and
 * polls _z_tcp_accept from its runtime (about once a second while idle). */
z_result_t _z_tcp_listen(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint) {
    sock->_fd = -1;
    for (struct addrinfo *it = endpoint._iptcp; it != NULL; it = it->ai_next) {
        int fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == -1) {
            continue;
        }
        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(one)) == 0 &&
            bind(fd, it->ai_addr, it->ai_addrlen) == 0 && listen(fd, Z_LISTEN_MAX_CONNECTION_NB) == 0) {
            sock->_fd = fd;
            return _Z_RES_OK;
        }
        close(fd);
    }
    _Z_ERROR_LOG(_Z_ERR_GENERIC);
    return _Z_ERR_GENERIC;
}

/* Called by zenoh-pico's accept task on every turn. "Nobody is waiting" is
 * the normal case and is not logged (upstream logs an error each time).
 * _Z_ERR_INVALID tells the task that the listening socket is gone. */
z_result_t _z_tcp_accept(const _z_sys_net_socket_t *sock_in, _z_sys_net_socket_t *sock_out) {
    sock_out->_fd = -1;
    int fd;
    do {
        fd = accept(sock_in->_fd, NULL, NULL);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return (errno == EBADF) ? _Z_ERR_INVALID : _Z_ERR_GENERIC;
    }
    if (zp_set_sockopts(fd) != _Z_RES_OK) {
        close(fd);
        return _Z_ERR_GENERIC;
    }
    sock_out->_fd = fd;
    return _Z_RES_OK;
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
        int wr = zp_wait_one(sock._fd, false, wait_ms);
        if (wr < 0) {
            return SIZE_MAX;
        }
        if (wr == 0) {
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

/* Sends the whole buffer or nothing usable: waits for room in the socket's
 * send buffer up to PICORUBY_ZENOH_SEND_TIMEOUT_MS in total, then shuts the
 * socket down (part of a message may already be on the wire). */
size_t _z_tcp_write(_z_sys_net_socket_t sock, const uint8_t *ptr, size_t len) {
    long deadline = zp_mono_ms() + PICORUBY_ZENOH_SEND_TIMEOUT_MS;
    size_t n = 0;
    while (n < len) {
        ssize_t sb = send(sock._fd, ptr + n, len - n, MSG_DONTWAIT);
        if (sb >= 0) {
            n += (size_t)sb;
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            long left = deadline - zp_mono_ms();
            if (left > 0 && zp_wait_one(sock._fd, true, (int)left) >= 0) {
                continue;
            }
            shutdown(sock._fd, SHUT_RDWR);
        }
        return SIZE_MAX;
    }
    return n;
}

bool zp_tcp_socket_closed(const _z_sys_net_socket_t *sock) {
    if (sock->_fd < 0) {
        return true;
    }
    uint8_t b;
    for (;;) {
        ssize_t rb = recv(sock->_fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
        if (rb > 0) {
            return false;
        }
        if (rb == 0) {
            return true; /* peer closed, or shut down after a send timeout */
        }
        if (errno == EINTR) {
            continue;
        }
        return !(errno == EAGAIN || errno == EWOULDBLOCK);
    }
}

#endif /* defined(ZP_PLATFORM_SOCKET_ESP32) */
