/*
 * linux_fcitx.c - see linux_fcitx.h.
 */

#include "linux_fcitx.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "linux_bus.h"

#define FCITX_NAME "org.fcitx.Fcitx5"
#define FCITX_PATH "/controller"
#define FCITX_IFACE "org.fcitx.Fcitx.Controller1"
/* After a call: once soon (fcitx5 has read the key by then, as a rule) and
 * once later (it had not). */
#define ASK_SOON_MS 40
#define ASK_LATER_MS 300
#define CALL_TIMEOUT_US 500000u

static struct {
    atomic_bool started, ok;
    atomic_bool kick, watch;
    int wake_fd;
    const lbus_t *sd;
    linux_fcitx_cb_t cb;
    pthread_t thread;
} s;

linux_fcitx_answer_t linux_fcitx_judge(int state, const char *im)
{
    if (im != NULL && im[0] != '\0') {
        return strncmp(im, "keyboard-", 9) == 0 ? LINUX_FCITX_LETTERS : LINUX_FCITX_JAPANESE;
    }
    if (state == 2) return LINUX_FCITX_JAPANESE;
    if (state == 1) return LINUX_FCITX_LETTERS;
    return LINUX_FCITX_NO_CONTEXT;
}

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool not_there(const lbus_error_t *e)
{
    return e->name != NULL && (strcmp(e->name, "org.freedesktop.DBus.Error.ServiceUnknown") == 0 ||
                               strcmp(e->name, "org.freedesktop.DBus.Error.NameHasNoOwner") == 0 ||
                               strcmp(e->name, "org.freedesktop.DBus.Error.UnknownObject") == 0 ||
                               strcmp(e->name, "org.freedesktop.DBus.Error.UnknownMethod") == 0);
}

/* One method of the controller, no arguments, one value back (type 'i' or
 * 's'). 1 answered, 0 fcitx5 is not there, -1 another failure (said once). */
static int ask_one(void *bus, const char *member, char type, void *out, size_t cap)
{
    const lbus_t *sd = s.sd;
    void *m = NULL, *reply = NULL;
    lbus_error_t err = LBUS_ERROR_NULL;
    int rc = sd->new_method_call(bus, &m, FCITX_NAME, FCITX_PATH, FCITX_IFACE, member);
    if (rc < 0) return -1;
    sd->message_set_auto_start(m, 0);   /* never start a fcitx5 that is not running */
    rc = sd->call(bus, m, CALL_TIMEOUT_US, &err, &reply);
    sd->message_unref(m);
    int result;
    if (rc < 0) {
        result = not_there(&err) ? 0 : -1;
        if (result < 0) {
            static atomic_bool said;
            if (!atomic_exchange(&said, true)) {
                char why[200];
                fprintf(stderr, "native: IME: fcitx5's %s did not answer (%s)\n", member,
                        lbus_why(&err, rc, why, sizeof(why)));
            }
        }
    } else if (type == 's') {
        const char *v = NULL;
        result = sd->message_read_basic(reply, 's', &v) > 0 ? 1 : -1;
        snprintf((char *)out, cap, "%s", (result > 0 && v) ? v : "");
    } else {
        result = sd->message_read_basic(reply, type, out) > 0 ? 1 : -1;
    }
    if (reply != NULL) sd->message_unref(reply);
    sd->error_free(&err);
    return result;
}

static void ask(void *bus)
{
    int state = 0;
    char im[64] = "";
    int r = ask_one(bus, "State", 'i', &state, 0);
    if (r == 0) {
        s.cb(LINUX_FCITX_ABSENT, "");
        return;
    }
    if (r < 0) return;
    if (ask_one(bus, "CurrentInputMethod", 's', im, sizeof(im)) <= 0) im[0] = '\0';
    s.cb(linux_fcitx_judge(state, im), im);
}

static void *run(void *arg)
{
    (void)arg;
    const lbus_t *sd = s.sd;
    void *bus = NULL;
    int rc = sd->open_user(&bus);
    if (rc < 0) {
        fprintf(stderr, "native: IME: no session bus (%s): fcitx5's state is not asked\n", strerror(-rc));
        atomic_store(&s.ok, false);
        s.cb(LINUX_FCITX_ABSENT, "");
        return NULL;
    }
    uint64_t soon = 0, later = 0, poll_at = 0;
    for (;;) {
        uint64_t now = mono_ms();
        if (atomic_exchange(&s.kick, false)) {
            soon = now + ASK_SOON_MS;
            later = now + ASK_LATER_MS;
        }
        bool watch = atomic_load(&s.watch);
        if (!watch) poll_at = 0;
        else if (poll_at == 0) poll_at = now + LINUX_FCITX_POLL_MS;
        bool go = false;
        if (soon != 0 && now >= soon) {
            soon = 0;
            go = true;
        }
        if (later != 0 && now >= later) {
            later = 0;
            go = true;
        }
        if (poll_at != 0 && now >= poll_at) go = true;
        if (go) {
            ask(bus);
            now = mono_ms();
            if (watch) poll_at = now + LINUX_FCITX_POLL_MS;
        }
        lbus_drain(sd, bus);   /* what the bus sent on its own (NameAcquired ...) */
        uint64_t next = 0;
        if (soon != 0) next = soon;
        if (later != 0 && (next == 0 || later < next)) next = later;
        if (poll_at != 0 && (next == 0 || poll_at < next)) next = poll_at;
        int timeout = -1;
        if (next != 0) timeout = next <= now ? 0 : (int)(next - now);
        lbus_wait(sd, bus, s.wake_fd, timeout);
    }
    return NULL;
}

bool linux_fcitx_start(linux_fcitx_cb_t cb)
{
    if (atomic_exchange(&s.started, true)) return atomic_load(&s.ok);
    char why[200];
    s.sd = lbus(why, sizeof(why));
    if (s.sd == NULL) {
        fprintf(stderr, "native: IME: fcitx5's state is not asked (%s)\n", why);
        return false;
    }
    s.cb = cb;
    s.wake_fd = lbus_wake_fd();
    atomic_store(&s.ok, true);
    atomic_store(&s.kick, true);
    if (pthread_create(&s.thread, NULL, run, NULL) != 0) {
        atomic_store(&s.ok, false);
        return false;
    }
    pthread_detach(s.thread);
    return true;
}

void linux_fcitx_ask(void)
{
    if (!atomic_load(&s.ok)) return;
    atomic_store(&s.kick, true);
    lbus_wake(s.wake_fd);
}

void linux_fcitx_watch(bool on)
{
    if (!atomic_load(&s.ok)) return;
    if (atomic_exchange(&s.watch, on) != on) lbus_wake(s.wake_fd);
}
