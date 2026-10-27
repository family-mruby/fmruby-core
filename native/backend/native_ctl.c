/*
 * native_ctl.c - see native_ctl.h. Main thread only.
 */

#include "native_ctl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "fmrb_gate.h"
#include "port_hosted.h"
#include "native_config.h"
#include "native_frame.h"
#include "png_write.h"

static int s_listen_fd = -1;
static char s_path[108];
static native_ctl_output_fn s_output_fn = NULL;

void native_ctl_set_output_handler(native_ctl_output_fn fn)
{
    s_output_fn = fn;
}

bool native_ctl_open(const char *path)
{
    snprintf(s_path, sizeof(s_path), "%s", path);
    s_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_listen_fd < 0) return false;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", s_path);
    unlink(s_path);
    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(s_listen_fd, 4) != 0) {
        fprintf(stderr, "native: control socket %s: %s\n", s_path, strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }
    fcntl(s_listen_fd, F_SETFL, fcntl(s_listen_fd, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

static void send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = fd, .events = POLLOUT };
                if (poll(&pfd, 1, 2000) <= 0) return;
                continue;
            }
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void reply(int fd, const char *line)
{
    send_all(fd, line, strlen(line));
}

/* Read one request line (the client sends it right after connecting). */
static int read_line(int fd, char *buf, size_t cap)
{
    size_t got = 0;
    while (got + 1 < cap) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 1000) <= 0) break;
        ssize_t n = recv(fd, buf + got, 1, 0);
        if (n <= 0) break;
        if (buf[got] == '\n') break;
        got++;
    }
    buf[got] = '\0';
    return (int)got;
}

static void serve_shot(int fd)
{
    static uint16_t *px = NULL;
    const int cap = 1920 * 1080;
    if (px == NULL) px = (uint16_t *)malloc((size_t)cap * sizeof(uint16_t));
    int w = 0, h = 0;
    uint32_t seq = 0;
    if (px == NULL || !native_frame_copy(px, cap, &w, &h, &seq)) {
        reply(fd, "ERR no frame yet\n");
        return;
    }
    size_t len = 0;
    uint8_t *png = png_write_rgb565(px, w, h, &len);
    if (png == NULL) {
        reply(fd, "ERR out of memory\n");
        return;
    }
    char head[64];
    snprintf(head, sizeof(head), "OK %zu %d %d\n", len, w, h);
    reply(fd, head);
    send_all(fd, png, len);
    free(png);
}

bool native_ctl_poll(void)
{
    bool quit = false;
    if (s_listen_fd < 0) return false;
    for (;;) {
        int fd = accept(s_listen_fd, NULL, NULL);
        if (fd < 0) break;
        char line[160];
        char own[1024];
        read_line(fd, line, sizeof(line));
        if (s_output_fn != NULL && s_output_fn(line, own, sizeof(own))) {
            reply(fd, own);
        } else if (strcmp(line, "shot") == 0) {
            serve_shot(fd);
        } else if (strcmp(line, "info") == 0) {
            int fw = 0, fh = 0;
            native_frame_size(&fw, &fh);
            char out[160];
            snprintf(out, sizeof(out), "OK %d %d %u %d %s\n", fw, fh,
                     (unsigned)native_frame_seq(), (int)getpid(),
                     g_native_config.machine.name[0] ? g_native_config.machine.name : "?");
            reply(fd, out);
        } else if (strcmp(line, "stats") == 0 || strcmp(line, "stats reset") == 0) {
            fmrb_gate_stats_t g;
            fmrb_gate_get_stats(&g);
            char out[224];
            snprintf(out, sizeof(out),
                     "OK gate calls=%u busy=%u orphans=%u helpers=%u idle=%u cancels=%u "
                     "deleted=%u burst_ms=%u\n",
                     g.calls, g.busy, g.orphans, g.helpers, g.helpers_idle, g.cancels,
                     g.deleted, (unsigned)ulPortGetMaxTickBurst());
            if (strcmp(line, "stats reset") == 0) {
                vPortResetMaxTickBurst();
            }
            reply(fd, out);
        } else if (strcmp(line, "quit") == 0) {
            reply(fd, "OK\n");
            quit = true;
        } else {
            reply(fd, "ERR unknown request\n");
        }
        close(fd);
    }
    return quit;
}

void native_ctl_close(void)
{
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
        unlink(s_path);
    }
}
