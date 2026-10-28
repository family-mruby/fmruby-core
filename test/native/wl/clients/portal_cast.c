/*
 * portal_cast.c - asks the desktop portal for a screen cast the way a
 * browser's meeting page does (doc/linux_native/ L14): CreateSession,
 * SelectSources (a monitor), Start, then OpenPipeWireRemote, on one
 * connection to the session bus. The portal hands the work to its backend
 * (xdg-desktop-portal-wlr), which takes the compositor's screen through
 * wlr-screencopy into a PipeWire stream.
 *
 *   portal_cast LOG
 *
 * The log: "session <path>", "sources ok", "stream <node id> <w>x<h>",
 * "remote fd <n>", or "error <step>: <why>" / "response <step> <code>".
 * It keeps the session open for a few seconds (the stream runs), then
 * closes it and ends.
 *
 * A test program, not part of the product: libc and sd-bus.
 */

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#define DEST "org.freedesktop.portal.Desktop"
#define PATH "/org/freedesktop/portal/desktop"
#define IFACE "org.freedesktop.portal.ScreenCast"

static FILE *g_log;
static sd_bus *g_bus;
static char g_sender[128];   /* the unique name, ':' dropped and '.' as '_' */

static int g_done;           /* the awaited Response came */
static uint32_t g_code;
static char g_session[256];
static uint32_t g_node;
static int g_w, g_h;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

/* Reads the results a{sv} of a Response for what we need. */
static void read_results(sd_bus_message *m)
{
    if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return;
    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char *key = NULL;
        sd_bus_message_read(m, "s", &key);
        if (key && strcmp(key, "session_handle") == 0) {
            const char *s = NULL;
            if (sd_bus_message_enter_container(m, 'v', "s") > 0) {
                sd_bus_message_read(m, "s", &s);
                if (s) snprintf(g_session, sizeof(g_session), "%s", s);
                sd_bus_message_exit_container(m);
            } else if (sd_bus_message_enter_container(m, 'v', "o") > 0) {
                sd_bus_message_read(m, "o", &s);
                if (s) snprintf(g_session, sizeof(g_session), "%s", s);
                sd_bus_message_exit_container(m);
            } else {
                sd_bus_message_skip(m, "v");
            }
        } else if (key && strcmp(key, "streams") == 0) {
            /* a(ua{sv}): the node id and its properties (size) */
            if (sd_bus_message_enter_container(m, 'v', "a(ua{sv})") > 0) {
                if (sd_bus_message_enter_container(m, 'a', "(ua{sv})") > 0) {
                    if (sd_bus_message_enter_container(m, 'r', "ua{sv}") > 0) {
                        sd_bus_message_read(m, "u", &g_node);
                        if (sd_bus_message_enter_container(m, 'a', "{sv}") > 0) {
                            while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
                                const char *k = NULL;
                                sd_bus_message_read(m, "s", &k);
                                if (k && strcmp(k, "size") == 0 &&
                                    sd_bus_message_enter_container(m, 'v', "(ii)") > 0) {
                                    sd_bus_message_read(m, "(ii)", &g_w, &g_h);
                                    sd_bus_message_exit_container(m);
                                } else {
                                    sd_bus_message_skip(m, "v");
                                }
                                sd_bus_message_exit_container(m);
                            }
                            sd_bus_message_exit_container(m);
                        }
                        sd_bus_message_exit_container(m);
                    }
                    sd_bus_message_exit_container(m);
                }
                sd_bus_message_exit_container(m);
            } else {
                sd_bus_message_skip(m, "v");
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

static int on_response(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    (void)e;
    if (sd_bus_message_read(m, "u", &g_code) < 0) return 0;
    read_results(m);
    g_done = 1;
    return 0;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Waits for the Response of the request named token. */
static bool wait_response(const char *step, double timeout)
{
    double end = now() + timeout;
    while (!g_done && now() < end) {
        while (sd_bus_process(g_bus, NULL) > 0) {
        }
        if (g_done) break;
        sd_bus_wait(g_bus, 200000);
    }
    if (!g_done) {
        say("error %s: no response in %.0fs", step, timeout);
        return false;
    }
    if (g_code != 0) {
        say("response %s %u", step, g_code);
        return false;
    }
    return true;
}

static bool watch(const char *token, sd_bus_slot **slot)
{
    char path[256];
    snprintf(path, sizeof(path), "/org/freedesktop/portal/desktop/request/%s/%s", g_sender, token);
    g_done = 0;
    return sd_bus_match_signal(g_bus, slot, DEST, path, "org.freedesktop.portal.Request", "Response", on_response,
                               NULL) >= 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: portal_cast LOG\n");
        return 2;
    }
    g_log = fopen(argv[1], "a");
    if (!g_log) return 1;
    if (sd_bus_open_user(&g_bus) < 0 && sd_bus_default_user(&g_bus) < 0) {
        say("error bus: no session bus");
        return 1;
    }
    const char *u = NULL;
    sd_bus_get_unique_name(g_bus, &u);
    size_t j = 0;
    for (size_t i = 1; u && u[i] && j + 1 < sizeof(g_sender); i++) g_sender[j++] = u[i] == '.' ? '_' : u[i];
    g_sender[j] = '\0';

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    sd_bus_slot *slot = NULL;

    /* 1. CreateSession */
    watch("c1", &slot);
    int r = sd_bus_call_method(g_bus, DEST, PATH, IFACE, "CreateSession", &err, &reply, "a{sv}", 2, "handle_token",
                               "s", "c1", "session_handle_token", "s", "s1");
    if (r < 0) {
        say("error CreateSession: %s", err.message ? err.message : strerror(-r));
        return 1;
    }
    sd_bus_message_unref(reply);
    reply = NULL;
    if (!wait_response("CreateSession", 20)) return 1;
    say("session %s", g_session);
    sd_bus_slot_unref(slot);

    /* 2. SelectSources: a monitor (types 1), one */
    watch("c2", &slot);
    r = sd_bus_call_method(g_bus, DEST, PATH, IFACE, "SelectSources", &err, &reply, "oa{sv}", g_session, 3,
                           "handle_token", "s", "c2", "types", "u", (uint32_t)1, "multiple", "b", 0);
    if (r < 0) {
        say("error SelectSources: %s", err.message ? err.message : strerror(-r));
        return 1;
    }
    sd_bus_message_unref(reply);
    reply = NULL;
    if (!wait_response("SelectSources", 20)) return 1;
    say("sources ok");
    sd_bus_slot_unref(slot);

    /* 3. Start */
    watch("c3", &slot);
    r = sd_bus_call_method(g_bus, DEST, PATH, IFACE, "Start", &err, &reply, "osa{sv}", g_session, "", 1,
                           "handle_token", "s", "c3");
    if (r < 0) {
        say("error Start: %s", err.message ? err.message : strerror(-r));
        return 1;
    }
    sd_bus_message_unref(reply);
    reply = NULL;
    if (!wait_response("Start", 30)) return 1;
    say("stream %u %dx%d", g_node, g_w, g_h);
    sd_bus_slot_unref(slot);

    /* 4. OpenPipeWireRemote: the fd a client reads the stream from */
    r = sd_bus_call_method(g_bus, DEST, PATH, IFACE, "OpenPipeWireRemote", &err, &reply, "oa{sv}", g_session, 0);
    if (r < 0) {
        say("error OpenPipeWireRemote: %s", err.message ? err.message : strerror(-r));
    } else {
        int fd = -1;
        sd_bus_message_read(reply, "h", &fd);
        say("remote fd %d", fd);
        sd_bus_message_unref(reply);
        reply = NULL;
    }

    /* Keep the stream a moment (the backend copies frames meanwhile). */
    double end = now() + 4.0;
    while (now() < end) {
        while (sd_bus_process(g_bus, NULL) > 0) {
        }
        sd_bus_wait(g_bus, 200000);
    }
    sd_bus_call_method(g_bus, DEST, g_session, "org.freedesktop.portal.Session", "Close", &err, NULL, "");
    say("closed");
    sd_bus_error_free(&err);
    sd_bus_flush_close_unref(g_bus);
    return 0;
}
