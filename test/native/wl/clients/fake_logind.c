/*
 * fake_logind.c - logind's part the compositor uses (doc/linux_native/ L13),
 * standing in for it on a bus of the check's own (a dbus-daemon given as
 * DBUS_SYSTEM_BUS_ADDRESS): never the machine's logind.
 *
 *   fake_logind FIFO LOG
 *
 * It owns org.freedesktop.login1 and serves:
 *
 *   /org/freedesktop/login1  Manager.Inhibit(ssss) -> h (a pipe's end; its
 *                            release is logged), Manager.GetSessionByPID(u)
 *                            -> o, the signal PrepareForSleep(b)
 *   .../session/_31          the signal Lock
 *
 * The lines it takes: "lock" (Lock on session 1), "sleep" / "wake"
 * (PrepareForSleep true / false), "quit". The log: "ready", "Inhibit WHAT
 * MODE", "inhibitor released", "emitted Lock", "emitted PrepareForSleep 1|0".
 *
 * A test program, not part of the product: libc and sd-bus.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#define SESSION "/org/freedesktop/login1/session/_31"

static FILE *g_log;
static int g_inhibit_rd = -1;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static int m_inhibit(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    (void)e;
    const char *what = "", *who = "", *why = "", *mode = "";
    int r = sd_bus_message_read(m, "ssss", &what, &who, &why, &mode);
    if (r < 0) return r;
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return -errno;
    if (g_inhibit_rd >= 0) close(g_inhibit_rd);
    g_inhibit_rd = p[0];
    say("Inhibit %s %s", what, mode);
    r = sd_bus_reply_method_return(m, "h", p[1]);
    close(p[1]);
    return r;
}

static int m_session_by_pid(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    (void)e;
    return sd_bus_reply_method_return(m, "o", SESSION);
}

static const sd_bus_vtable manager_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Inhibit", "ssss", "h", m_inhibit, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetSessionByPID", "u", "o", m_session_by_pid, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("PrepareForSleep", "b", 0),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable session_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_SIGNAL("Lock", "", 0),
    SD_BUS_VTABLE_END,
};

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: fake_logind FIFO LOG\n");
        return 2;
    }
    g_log = fopen(argv[2], "a");
    if (g_log == NULL) return 1;
    int fifo = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fifo < 0) {
        say("error: %s: %s", argv[1], strerror(errno));
        return 1;
    }
    sd_bus *bus = NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        say("error: bus: %s", strerror(-r));
        return 1;
    }
    sd_bus_add_object_vtable(bus, NULL, "/org/freedesktop/login1", "org.freedesktop.login1.Manager", manager_vt,
                             NULL);
    sd_bus_add_object_vtable(bus, NULL, SESSION, "org.freedesktop.login1.Session", session_vt, NULL);
    r = sd_bus_request_name(bus, "org.freedesktop.login1", 0);
    if (r < 0) {
        say("error: name: %s", strerror(-r));
        return 1;
    }
    say("ready");
    char buf[256];
    size_t used = 0;
    bool quit = false;
    while (!quit) {
        while ((r = sd_bus_process(bus, NULL)) > 0) {
        }
        struct pollfd pf[3] = { { sd_bus_get_fd(bus), (short)sd_bus_get_events(bus), 0 },
                                { fifo, POLLIN, 0 },
                                { g_inhibit_rd, POLLIN, 0 } };
        if (poll(pf, g_inhibit_rd >= 0 ? 3 : 2, 200) < 0 && errno != EINTR) break;
        if (g_inhibit_rd >= 0 && (pf[2].revents & (POLLHUP | POLLIN))) {
            say("inhibitor released");
            close(g_inhibit_rd);
            g_inhibit_rd = -1;
        }
        if (pf[1].revents & POLLIN) {
            ssize_t n = read(fifo, buf + used, sizeof(buf) - 1 - used);
            if (n > 0) used += (size_t)n;
            char *nl;
            while ((nl = memchr(buf, '\n', used)) != NULL) {
                *nl = '\0';
                if (strcmp(buf, "lock") == 0) {
                    sd_bus_emit_signal(bus, SESSION, "org.freedesktop.login1.Session", "Lock", "");
                    say("emitted Lock");
                } else if (strcmp(buf, "sleep") == 0 || strcmp(buf, "wake") == 0) {
                    int going = strcmp(buf, "sleep") == 0;
                    sd_bus_emit_signal(bus, "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                                       "PrepareForSleep", "b", going);
                    say("emitted PrepareForSleep %d", going);
                } else if (strcmp(buf, "quit") == 0) {
                    quit = true;
                }
                size_t rest = used - (size_t)(nl + 1 - buf);
                memmove(buf, nl + 1, rest);
                used = rest;
            }
            if (used == sizeof(buf) - 1) used = 0;
        }
    }
    sd_bus_flush_close_unref(bus);
    return 0;
}
