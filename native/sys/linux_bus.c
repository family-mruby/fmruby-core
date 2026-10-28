/*
 * linux_bus.c - see linux_bus.h.
 */

#include "linux_bus.h"

#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static lbus_t s_sd;
static bool s_ok;
static char s_why[160];

#define SYM(field, name)                                                  \
    do {                                                                  \
        *(void **)&s_sd.field = dlsym(lib, name);                         \
        if (s_sd.field == NULL) {                                         \
            snprintf(s_why, sizeof(s_why), "libsystemd.so.0 lacks %s", name); \
            return;                                                       \
        }                                                                 \
    } while (0)

static void load(void)
{
    void *lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        snprintf(s_why, sizeof(s_why), "libsystemd.so.0: %s", dlerror());
        return;
    }
    SYM(open_system, "sd_bus_open_system");
    SYM(open_user, "sd_bus_open_user");
    SYM(flush_close_unref, "sd_bus_flush_close_unref");
    SYM(get_fd, "sd_bus_get_fd");
    SYM(get_events, "sd_bus_get_events");
    SYM(get_timeout, "sd_bus_get_timeout");
    SYM(process, "sd_bus_process");
    SYM(flush, "sd_bus_flush");
    SYM(request_name, "sd_bus_request_name");
    SYM(add_object, "sd_bus_add_object");
    SYM(add_fallback, "sd_bus_add_fallback");
    SYM(match_signal, "sd_bus_match_signal");
    SYM(call_method, "sd_bus_call_method");
    SYM(call, "sd_bus_call");
    SYM(send, "sd_bus_send");
    SYM(get_property, "sd_bus_get_property");
    SYM(set_property, "sd_bus_set_property");
    SYM(emit_signal, "sd_bus_emit_signal");
    SYM(new_method_call, "sd_bus_message_new_method_call");
    SYM(new_method_return, "sd_bus_message_new_method_return");
    SYM(reply_method_return, "sd_bus_reply_method_return");
    SYM(reply_method_errorf, "sd_bus_reply_method_errorf");
    SYM(message_append, "sd_bus_message_append");
    SYM(message_append_basic, "sd_bus_message_append_basic");
    SYM(message_open_container, "sd_bus_message_open_container");
    SYM(message_close_container, "sd_bus_message_close_container");
    SYM(message_read, "sd_bus_message_read");
    SYM(message_read_basic, "sd_bus_message_read_basic");
    SYM(message_read_array, "sd_bus_message_read_array");
    SYM(message_enter_container, "sd_bus_message_enter_container");
    SYM(message_exit_container, "sd_bus_message_exit_container");
    SYM(message_skip, "sd_bus_message_skip");
    SYM(message_peek_type, "sd_bus_message_peek_type");
    SYM(message_rewind, "sd_bus_message_rewind");
    SYM(message_ref, "sd_bus_message_ref");
    SYM(message_unref, "sd_bus_message_unref");
    SYM(message_get_member, "sd_bus_message_get_member");
    SYM(message_get_interface, "sd_bus_message_get_interface");
    SYM(message_get_path, "sd_bus_message_get_path");
    SYM(message_is_method_call, "sd_bus_message_is_method_call");
    SYM(error_free, "sd_bus_error_free");
    SYM(slot_unref, "sd_bus_slot_unref");
    s_ok = true;   /* the library stays loaded */
}

const lbus_t *lbus(char *why, size_t cap)
{
    pthread_once(&s_once, load);
    if (!s_ok) {
        if (why != NULL && cap > 0) snprintf(why, cap, "%s", s_why);
        return NULL;
    }
    return &s_sd;
}

void lbus_wait(const lbus_t *sd, void *bus, int wake_fd, int timeout_ms)
{
    struct pollfd p[2];
    int n = 0;
    int bus_fd = sd->get_fd(bus);
    if (bus_fd >= 0) {
        int ev = sd->get_events(bus);
        p[n].fd = bus_fd;
        p[n].events = (short)(ev > 0 ? ev : POLLIN);
        p[n].revents = 0;
        n++;
    }
    if (wake_fd >= 0) {
        p[n].fd = wake_fd;
        p[n].events = POLLIN;
        p[n].revents = 0;
        n++;
    }
    uint64_t usec = UINT64_MAX;
    if (sd->get_timeout(bus, &usec) >= 0 && usec != UINT64_MAX) {
        /* An absolute CLOCK_MONOTONIC time; 0 means now. */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
        int ms = usec <= now ? 0 : (int)((usec - now + 999) / 1000);
        if (timeout_ms < 0 || ms < timeout_ms) timeout_ms = ms;
    }
    int r = poll(p, (nfds_t)n, timeout_ms);
    if (r > 0 && wake_fd >= 0) {
        for (int i = 0; i < n; i++) {
            if (p[i].fd == wake_fd && (p[i].revents & POLLIN)) {
                uint64_t v;
                ssize_t got = read(wake_fd, &v, sizeof(v));
                (void)got;
            }
        }
    }
}

void lbus_drain(const lbus_t *sd, void *bus)
{
    for (int i = 0; i < 1000; i++) {
        if (sd->process(bus, NULL) <= 0) break;
    }
}

int lbus_wake_fd(void)
{
    return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

void lbus_wake(int fd)
{
    if (fd < 0) return;
    uint64_t one = 1;
    ssize_t n = write(fd, &one, sizeof(one));
    (void)n;
}

const char *lbus_why(const lbus_error_t *e, int rc, char *buf, size_t cap)
{
    if (e != NULL && e->message != NULL) {
        snprintf(buf, cap, "%s%s%s", e->name ? e->name : "", e->name ? ": " : "", e->message);
    } else if (e != NULL && e->name != NULL) {
        snprintf(buf, cap, "%s", e->name);
    } else {
        snprintf(buf, cap, "%s", strerror(rc < 0 ? -rc : rc));
    }
    return buf;
}

bool lbus_read_variant(const lbus_t *sd, void *m, char want, void *out, size_t cap)
{
    char type = 0;
    const char *contents = NULL;
    if (sd->message_peek_type(m, &type, &contents) <= 0 || type != 'v' || contents == NULL) {
        sd->message_skip(m, "v");
        return false;
    }
    if (want == 'a') {
        /* a byte array as a string */
        if (strcmp(contents, "ay") != 0 || sd->message_enter_container(m, 'v', "ay") <= 0) {
            sd->message_skip(m, "v");
            return false;
        }
        const void *p = NULL;
        size_t n = 0;
        if (sd->message_read_array(m, 'y', &p, &n) < 0) n = 0;
        if (cap > 0) {
            if (n >= cap) n = cap - 1;
            if (n > 0 && p != NULL) memcpy(out, p, n);
            ((char *)out)[n] = '\0';
        }
        sd->message_exit_container(m);
        return true;
    }
    if (contents[0] != want || contents[1] != '\0') {
        sd->message_skip(m, "v");
        return false;
    }
    if (sd->message_enter_container(m, 'v', contents) <= 0) {
        sd->message_skip(m, "v");
        return false;
    }
    bool ok;
    if (want == 's' || want == 'o' || want == 'g') {
        const char *s = NULL;
        ok = sd->message_read_basic(m, want, &s) > 0;
        if (cap > 0) snprintf((char *)out, cap, "%s", (ok && s) ? s : "");
    } else {
        ok = sd->message_read_basic(m, want, out) > 0;
    }
    sd->message_exit_container(m);
    return ok;
}
