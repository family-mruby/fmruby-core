// TCP transport for the remote debugger (Linux build only).
//
// Single client. Frame = [u32 length BE][msgpack body]. Non-blocking sockets +
// select. A persistent receive buffer accumulates the byte stream across poll()
// calls and yields one complete body at a time.
#include "fmrb_debug_transport.h"
#include "fmrb_debug_proto.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

#include "fmrb_log.h"

static const char *TAG = "dbg_tcp";

static int  s_listen_fd = -1;
static int  s_client_fd = -1;

// Stream reassembly buffer: holds the 4-byte length prefix + up to one max body.
static uint8_t s_rx[4 + FMRB_DEBUG_MAX_FRAME];
static size_t  s_rx_fill;

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void drop_client(void) {
    if (s_client_fd >= 0) {
        close(s_client_fd);
        s_client_fd = -1;
    }
    s_rx_fill = 0;
}

static fmrb_err_t tcp_init(void) {
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        FMRB_LOGE(TAG, "socket() failed: %s", strerror(errno));
        return FMRB_ERR_FAILED;
    }
    int one = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(FMRB_DEBUG_TCP_PORT);
    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        FMRB_LOGE(TAG, "bind(:%d) failed: %s", FMRB_DEBUG_TCP_PORT, strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return FMRB_ERR_FAILED;
    }
    if (listen(s_listen_fd, 1) < 0) {
        FMRB_LOGE(TAG, "listen() failed: %s", strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return FMRB_ERR_FAILED;
    }
    set_nonblock(s_listen_fd);
    FMRB_LOGI(TAG, "listening on 0.0.0.0:%d", FMRB_DEBUG_TCP_PORT);
    return FMRB_OK;
}

// Extract one complete body from the reassembly buffer if present.
// Returns body length (>0) or 0 if incomplete.
static int try_extract(uint8_t *buf, size_t cap) {
    if (s_rx_fill < 4) return 0;
    uint32_t len = ((uint32_t)s_rx[0] << 24) | ((uint32_t)s_rx[1] << 16) |
                   ((uint32_t)s_rx[2] << 8)  | (uint32_t)s_rx[3];
    if (len > FMRB_DEBUG_MAX_FRAME) {
        FMRB_LOGE(TAG, "frame too large (%u), dropping client", (unsigned)len);
        drop_client();
        return 0;
    }
    if (s_rx_fill < 4 + len) return 0;   // body not fully arrived yet
    if (len > cap) {                     // caller buffer too small (shouldn't happen)
        FMRB_LOGE(TAG, "body %u > cap %zu", (unsigned)len, cap);
        drop_client();
        return 0;
    }
    memcpy(buf, s_rx + 4, len);
    // Shift any trailing bytes (next frame) down.
    size_t consumed = 4 + len;
    memmove(s_rx, s_rx + consumed, s_rx_fill - consumed);
    s_rx_fill -= consumed;
    return (int)len;
}

static int tcp_poll(uint8_t *buf, size_t cap, uint32_t timeout_ms) {
    // A previous read may have buffered more than one frame.
    int got = try_extract(buf, cap);
    if (got > 0) return got;

    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    if (s_listen_fd >= 0) { FD_SET(s_listen_fd, &rfds); maxfd = s_listen_fd; }
    if (s_client_fd >= 0) { FD_SET(s_client_fd, &rfds); if (s_client_fd > maxfd) maxfd = s_client_fd; }
    if (maxfd < 0) return 0;

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int n = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (n < 0) {
        if (errno == EINTR) return 0;
        FMRB_LOGE(TAG, "select() failed: %s", strerror(errno));
        return -1;
    }
    if (n == 0) return 0;   // timeout

    // New connection.
    if (s_listen_fd >= 0 && FD_ISSET(s_listen_fd, &rfds)) {
        int fd = accept(s_listen_fd, NULL, NULL);
        if (fd >= 0) {
            if (s_client_fd >= 0) {
                // One session only: reject the new comer.
                close(fd);
            } else {
                set_nonblock(fd);
                s_client_fd = fd;
                s_rx_fill = 0;
                FMRB_LOGI(TAG, "client connected");
            }
        }
    }

    // Client data.
    if (s_client_fd >= 0 && FD_ISSET(s_client_fd, &rfds)) {
        size_t room = sizeof(s_rx) - s_rx_fill;
        if (room == 0) {
            FMRB_LOGE(TAG, "rx buffer full without a complete frame; dropping");
            drop_client();
            return 0;
        }
        ssize_t r = recv(s_client_fd, s_rx + s_rx_fill, room, 0);
        if (r == 0) {
            FMRB_LOGI(TAG, "client disconnected");
            drop_client();
            return 0;
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            FMRB_LOGW(TAG, "recv() failed: %s", strerror(errno));
            drop_client();
            return 0;
        }
        s_rx_fill += (size_t)r;
        return try_extract(buf, cap);
    }
    return 0;
}

static fmrb_err_t tcp_send(const uint8_t *body, size_t len) {
    if (s_client_fd < 0) return FMRB_ERR_INVALID_STATE;
    uint8_t hdr[4] = {
        (uint8_t)(len >> 24), (uint8_t)(len >> 16),
        (uint8_t)(len >> 8),  (uint8_t)(len)
    };
    struct iovec_frame { const uint8_t *p; size_t n; } parts[2] = {
        { hdr, 4 }, { body, len }
    };
    for (int i = 0; i < 2; i++) {
        size_t off = 0;
        while (off < parts[i].n) {
            ssize_t w = send(s_client_fd, parts[i].p + off, parts[i].n - off, MSG_NOSIGNAL);
            if (w < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Socket buffer full (slow/stalled client). Block on
                    // writability instead of spinning, so a client that stops
                    // reading cannot busy-loop this task on the CPU.
                    fd_set wfds;
                    FD_ZERO(&wfds);
                    FD_SET(s_client_fd, &wfds);
                    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
                    (void)select(s_client_fd + 1, NULL, &wfds, NULL, &tv);
                    continue;  // retry the send
                }
                FMRB_LOGW(TAG, "send() failed: %s", strerror(errno));
                drop_client();
                return FMRB_ERR_FAILED;
            }
            off += (size_t)w;
        }
    }
    return FMRB_OK;
}

static bool tcp_connected(void) {
    return s_client_fd >= 0;
}

static void tcp_close_client(void) {
    drop_client();
}

const fmrb_debug_transport_ops_t fmrb_debug_transport_tcp = {
    .init         = tcp_init,
    .poll         = tcp_poll,
    .send         = tcp_send,
    .connected    = tcp_connected,
    .close_client = tcp_close_client,
};
