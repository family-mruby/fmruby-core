/*
 * linux_notify.c - see linux_notify.h.
 */

#include "linux_notify.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "linux_bus.h"

#define APP_CAP 48
#define SUMMARY_CAP 128
#define BODY_CAP 320
#define CLOSED_MAX 16

/* The service's answers. */
#define SERVER_NAME "Family mruby"
#define SERVER_VENDOR "Family mruby"
#define SERVER_VERSION "1"
#define SPEC_VERSION "1.2"
#define BUS_NAME "org.freedesktop.Notifications"
#define BUS_PATH "/org/freedesktop/Notifications"

/* Closing reasons (the specification's). */
#define REASON_EXPIRED 1
#define REASON_DISMISSED 2
#define REASON_CLOSED 3

typedef struct {
    uint32_t id;
    char kind[12];
    char app[APP_CAP];
    char summary[SUMMARY_CAP];
    char body[BODY_CAP];
    uint64_t until_ms;
    uint64_t seq;            /* the order they came in */
} notice_t;

static struct {
    pthread_mutex_t lock;
    notice_t n[LINUX_NOTIFY_MAX];
    int count;
    uint32_t next_id;
    uint32_t gen;
    uint64_t seq;
    struct {
        uint32_t id, reason;
    } closed[CLOSED_MAX];
    int nclosed;
} s_st = { .lock = PTHREAD_MUTEX_INITIALIZER, .next_id = 1 };

static struct {
    atomic_bool started;
    atomic_bool serving;
    pthread_t thread;
    int wake_fd;
} s_srv = { .wake_fd = -1 };

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

int linux_notify_duration(int timeout_ms)
{
    if (timeout_ms < 0) return 5000;     /* the server's choice */
    if (timeout_ms == 0) return 10000;   /* "never": there is no history to keep it in */
    if (timeout_ms < 2000) return 2000;
    if (timeout_ms > 10000) return 10000;
    return timeout_ms;
}

/* Copies text cut to cap at a whole UTF-8 character; tabs (and, unless
 * keep_lines, line breaks) become spaces, other control bytes go. */
static void copy_text(char *out, size_t cap, const char *in, bool keep_lines)
{
    size_t j = 0;
    if (in == NULL) in = "";
    for (size_t i = 0; in[i] != '\0' && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\t' || (c == '\n' && !keep_lines)) c = ' ';
        else if (c == '\r') continue;
        else if (c < 0x20 && c != '\n') continue;
        out[j++] = (char)c;
    }
    /* Do not end inside a character. */
    size_t k = j;
    while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
    if (k > 0 && ((unsigned char)out[k - 1] & 0x80)) {
        unsigned char lead = (unsigned char)out[k - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (j - (k - 1) < need) j = k - 1;
    }
    out[j] = '\0';
}

static void closed_push_locked(uint32_t id, uint32_t reason, bool from_bus)
{
    if (!from_bus) return;
    if (s_st.nclosed >= CLOSED_MAX) {
        memmove(&s_st.closed[0], &s_st.closed[1], sizeof(s_st.closed[0]) * (CLOSED_MAX - 1));
        s_st.nclosed--;
    }
    s_st.closed[s_st.nclosed].id = id;
    s_st.closed[s_st.nclosed].reason = reason;
    s_st.nclosed++;
}

static void remove_locked(int i, uint32_t reason)
{
    closed_push_locked(s_st.n[i].id, reason, strcmp(s_st.n[i].kind, "linux") == 0);
    memmove(&s_st.n[i], &s_st.n[i + 1], sizeof(notice_t) * (size_t)(s_st.count - i - 1));
    s_st.count--;
    s_st.gen++;
}

static bool expire_locked(uint64_t now)
{
    bool any = false;
    for (int i = s_st.count - 1; i >= 0; i--) {
        if (now >= s_st.n[i].until_ms) {
            remove_locked(i, REASON_EXPIRED);
            any = true;
        }
    }
    return any;
}

uint32_t linux_notify_post_at(const char *kind, const char *app, const char *summary, const char *body,
                              int timeout_ms, uint32_t replaces, uint64_t now)
{
    pthread_mutex_lock(&s_st.lock);
    bool woke = expire_locked(now);
    notice_t *slot = NULL;
    if (replaces != 0) {
        for (int i = 0; i < s_st.count; i++) {
            if (s_st.n[i].id == replaces) slot = &s_st.n[i];
        }
    }
    if (slot == NULL) {
        if (s_st.count >= LINUX_NOTIFY_MAX) {
            /* The oldest makes room. */
            int oldest = 0;
            for (int i = 1; i < s_st.count; i++) {
                if (s_st.n[i].seq < s_st.n[oldest].seq) oldest = i;
            }
            remove_locked(oldest, REASON_EXPIRED);
            woke = true;
        }
        slot = &s_st.n[s_st.count++];
        memset(slot, 0, sizeof(*slot));
        slot->id = replaces != 0 ? replaces : s_st.next_id++;
        if (s_st.next_id == 0) s_st.next_id = 1;
    }
    copy_text(slot->kind, sizeof(slot->kind), kind, false);
    copy_text(slot->app, sizeof(slot->app), app, false);
    copy_text(slot->summary, sizeof(slot->summary), summary, false);
    copy_text(slot->body, sizeof(slot->body), body, true);
    slot->until_ms = now + (uint64_t)linux_notify_duration(timeout_ms);
    slot->seq = ++s_st.seq;
    s_st.gen++;
    uint32_t id = slot->id;
    pthread_mutex_unlock(&s_st.lock);
    if (woke) lbus_wake(s_srv.wake_fd);
    return id;
}

uint32_t linux_notify_post(const char *kind, const char *app, const char *summary, const char *body,
                           int timeout_ms, uint32_t replaces)
{
    return linux_notify_post_at(kind, app, summary, body, timeout_ms, replaces, now_ms());
}

uint32_t linux_notify_gen_at(uint64_t now)
{
    pthread_mutex_lock(&s_st.lock);
    bool woke = expire_locked(now);
    uint32_t g = s_st.gen;
    pthread_mutex_unlock(&s_st.lock);
    if (woke) lbus_wake(s_srv.wake_fd);
    return g;
}

uint32_t linux_notify_gen(void)
{
    return linux_notify_gen_at(now_ms());
}

int32_t linux_notify_list(char *buf, size_t cap)
{
    pthread_mutex_lock(&s_st.lock);
    /* newest first */
    int order[LINUX_NOTIFY_MAX];
    int n = s_st.count;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (s_st.n[order[j]].seq > s_st.n[order[i]].seq) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    size_t len = 0;
    for (int k = 0; k < n; k++) {
        const notice_t *e = &s_st.n[order[k]];
        len += (size_t)snprintf(NULL, 0, "%u\t%s\t%s\t%s\t%s\n", e->id, e->kind, e->app, e->summary, e->body);
    }
    if (buf != NULL && len < cap) {
        size_t at = 0;
        for (int k = 0; k < n; k++) {
            const notice_t *e = &s_st.n[order[k]];
            at += (size_t)snprintf(buf + at, cap - at, "%u\t%s\t%s\t%s\t", e->id, e->kind, e->app, e->summary);
            for (const char *p = e->body; *p; p++) buf[at++] = *p == '\n' ? '\x1f' : *p;
            buf[at++] = '\n';
        }
        buf[at] = '\0';
    }
    pthread_mutex_unlock(&s_st.lock);
    return (int32_t)len;
}

static bool remove_id(uint32_t id, uint32_t reason)
{
    pthread_mutex_lock(&s_st.lock);
    bool found = false;
    for (int i = 0; i < s_st.count; i++) {
        if (s_st.n[i].id == id) {
            remove_locked(i, reason);
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_st.lock);
    if (found) lbus_wake(s_srv.wake_fd);
    return found;
}

bool linux_notify_dismiss(uint32_t id)
{
    return remove_id(id, REASON_DISMISSED);
}

bool linux_notify_close(uint32_t id)
{
    return remove_id(id, REASON_CLOSED);
}

bool linux_notify_take_closed(uint32_t *id, uint32_t *reason)
{
    pthread_mutex_lock(&s_st.lock);
    bool any = s_st.nclosed > 0;
    if (any) {
        *id = s_st.closed[0].id;
        *reason = s_st.closed[0].reason;
        memmove(&s_st.closed[0], &s_st.closed[1], sizeof(s_st.closed[0]) * (size_t)(s_st.nclosed - 1));
        s_st.nclosed--;
    }
    pthread_mutex_unlock(&s_st.lock);
    return any;
}

void linux_notify_reset(void)
{
    pthread_mutex_lock(&s_st.lock);
    s_st.count = 0;
    s_st.nclosed = 0;
    s_st.next_id = 1;
    s_st.gen++;
    pthread_mutex_unlock(&s_st.lock);
}

/* ---- the service (session bus) ---- */

static const char INTROSPECT_XML[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    " \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    " <interface name=\"org.freedesktop.Notifications\">\n"
    "  <method name=\"GetCapabilities\"><arg type=\"as\" direction=\"out\"/></method>\n"
    "  <method name=\"Notify\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/>"
    "<arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"as\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"i\" direction=\"in\"/>"
    "<arg type=\"u\" direction=\"out\"/></method>\n"
    "  <method name=\"CloseNotification\"><arg type=\"u\" direction=\"in\"/></method>\n"
    "  <method name=\"GetServerInformation\"><arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/>"
    "<arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/></method>\n"
    "  <signal name=\"NotificationClosed\"><arg type=\"u\"/><arg type=\"u\"/></signal>\n"
    "  <signal name=\"ActionInvoked\"><arg type=\"u\"/><arg type=\"s\"/></signal>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "  <method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method>\n"
    " </interface>\n"
    "</node>\n";

static const lbus_t *s_sd;

static int on_call(void *m, void *userdata, lbus_error_t *ret_error)
{
    (void)userdata;
    (void)ret_error;
    const lbus_t *sd = s_sd;
    if (sd->message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect") > 0) {
        return sd->reply_method_return(m, "s", INTROSPECT_XML);
    }
    const char *iface = "org.freedesktop.Notifications";
    if (sd->message_is_method_call(m, iface, "GetCapabilities") > 0) {
        return sd->reply_method_return(m, "as", 1, "body");
    }
    if (sd->message_is_method_call(m, iface, "GetServerInformation") > 0) {
        return sd->reply_method_return(m, "ssss", SERVER_NAME, SERVER_VENDOR, SERVER_VERSION, SPEC_VERSION);
    }
    if (sd->message_is_method_call(m, iface, "Notify") > 0) {
        const char *app = "", *icon = "", *summary = "", *body = "";
        uint32_t replaces = 0;
        int32_t timeout = -1;
        int r = sd->message_read(m, "susss", &app, &replaces, &icon, &summary, &body);
        if (r < 0) return r;
        /* The actions and hints are read past (no buttons, no urgency). */
        sd->message_skip(m, "as");
        sd->message_skip(m, "a{sv}");
        sd->message_read(m, "i", &timeout);
        uint32_t id = linux_notify_post("linux", app, summary, body, timeout, replaces);
        fprintf(stderr, "native: notifications: #%u from \"%s\": %s\n", id, app, summary);
        return sd->reply_method_return(m, "u", id);
    }
    if (sd->message_is_method_call(m, iface, "CloseNotification") > 0) {
        uint32_t id = 0;
        sd->message_read(m, "u", &id);
        linux_notify_close(id);
        return sd->reply_method_return(m, "");
    }
    return 0;   /* sd-bus answers the rest (unknown method) */
}

static void *serve(void *arg)
{
    void *bus = arg;
    const lbus_t *sd = s_sd;
    for (;;) {
        lbus_drain(sd, bus);
        uint32_t id, reason;
        while (linux_notify_take_closed(&id, &reason)) {
            sd->emit_signal(bus, BUS_PATH, "org.freedesktop.Notifications", "NotificationClosed", "uu", id, reason);
        }
        sd->flush(bus);
        lbus_wait(sd, bus, s_srv.wake_fd, -1);
    }
    return NULL;
}

bool linux_notify_serve(void)
{
    if (atomic_exchange(&s_srv.started, true)) return atomic_load(&s_srv.serving);
    char why[200];
    s_sd = lbus(why, sizeof(why));
    if (s_sd == NULL) {
        fprintf(stderr, "native: notifications: not served (%s)\n", why);
        return false;
    }
    void *bus = NULL;
    int rc = s_sd->open_user(&bus);
    if (rc < 0) {
        fprintf(stderr, "native: notifications: not served (no session bus: %s)\n", strerror(-rc));
        return false;
    }
    rc = s_sd->request_name(bus, BUS_NAME, 0);
    if (rc < 0) {
        fprintf(stderr, "native: notifications: not served (%s is %s); the apps' notices still show\n", BUS_NAME,
                rc == -EEXIST ? "another program's" : strerror(-rc));
        s_sd->flush_close_unref(bus);
        return false;
    }
    if (s_sd->add_object(bus, NULL, BUS_PATH, on_call, NULL) < 0) {
        fprintf(stderr, "native: notifications: not served (cannot add %s)\n", BUS_PATH);
        s_sd->flush_close_unref(bus);
        return false;
    }
    s_srv.wake_fd = lbus_wake_fd();
    if (pthread_create(&s_srv.thread, NULL, serve, bus) != 0) {
        s_sd->flush_close_unref(bus);
        return false;
    }
    pthread_detach(s_srv.thread);
    atomic_store(&s_srv.serving, true);
    fprintf(stderr, "native: notifications: serving %s on the session bus\n", BUS_NAME);
    return true;
}

bool linux_notify_serving(void)
{
    return atomic_load(&s_srv.serving);
}
