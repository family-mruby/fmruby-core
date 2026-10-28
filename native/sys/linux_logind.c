/*
 * linux_logind.c - see linux_logind.h.
 */

#include "linux_logind.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "comp_link.h"

/* How long a suspend waits for the lock at most. */
#define SLEEP_WAIT_MS 3000

typedef struct {
    const char *name;
    const char *message;
    int need_free;
} sdb_error_t;

typedef int (*sdb_handler_t)(void *m, void *userdata, sdb_error_t *ret_error);

static struct {
    int (*open_system)(void **bus);
    int (*match_signal)(void *bus, void **slot, const char *sender, const char *path, const char *interface,
                        const char *member, sdb_handler_t callback, void *userdata);
    int (*process)(void *bus, void **r);
    int (*wait)(void *bus, uint64_t timeout_usec);
    int (*call_method)(void *bus, const char *destination, const char *path, const char *interface,
                       const char *member, sdb_error_t *ret_error, void **reply, const char *types, ...);
    int (*message_read)(void *m, const char *types, ...);
    void *(*message_unref)(void *m);
    void (*error_free)(sdb_error_t *e);
    void *(*flush_close_unref)(void *bus);
    int (*path_encode)(const char *prefix, const char *external_id, char **ret_path);
} sd;

static struct {
    pthread_t thread;
    atomic_bool running;
    atomic_bool stop;
    void *bus;
    int inhibit_fd;          /* the delay inhibitor, -1 none */
    bool sleeping;           /* PrepareForSleep(true) seen, waiting for the lock */
    uint64_t sleep_at_ms;
} s_lg = { .inhibit_fd = -1 };

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static bool load(char *why, size_t cap)
{
    void *lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        snprintf(why, cap, "libsystemd.so.0: %s", dlerror());
        return false;
    }
    *(void **)&sd.open_system = dlsym(lib, "sd_bus_open_system");
    *(void **)&sd.match_signal = dlsym(lib, "sd_bus_match_signal");
    *(void **)&sd.process = dlsym(lib, "sd_bus_process");
    *(void **)&sd.wait = dlsym(lib, "sd_bus_wait");
    *(void **)&sd.call_method = dlsym(lib, "sd_bus_call_method");
    *(void **)&sd.message_read = dlsym(lib, "sd_bus_message_read");
    *(void **)&sd.message_unref = dlsym(lib, "sd_bus_message_unref");
    *(void **)&sd.error_free = dlsym(lib, "sd_bus_error_free");
    *(void **)&sd.flush_close_unref = dlsym(lib, "sd_bus_flush_close_unref");
    *(void **)&sd.path_encode = dlsym(lib, "sd_bus_path_encode");
    if (!sd.open_system || !sd.match_signal || !sd.process || !sd.wait || !sd.call_method || !sd.message_read ||
        !sd.message_unref || !sd.error_free || !sd.flush_close_unref || !sd.path_encode) {
        snprintf(why, cap, "libsystemd.so.0 lacks the sd-bus calls");
        return false;
    }
    return true;
}

/* A delay inhibitor for sleep: logind waits (its InhibitDelayMaxSec) until
 * it is let go. */
static void inhibit_take(void)
{
    if (s_lg.inhibit_fd >= 0) return;
    sdb_error_t err = { NULL, NULL, 0 };
    void *reply = NULL;
    int rc = sd.call_method(s_lg.bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                            "org.freedesktop.login1.Manager", "Inhibit", &err, &reply, "ssss", "sleep", "Family mruby",
                            "Lock the screen before sleeping", "delay");
    if (rc >= 0) {
        int fd = -1;
        if (sd.message_read(reply, "h", &fd) >= 0 && fd >= 0) s_lg.inhibit_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    } else {
        fprintf(stderr, "native: logind: no sleep inhibitor (%s)\n", err.message ? err.message : strerror(-rc));
    }
    sd.error_free(&err);
    if (reply) sd.message_unref(reply);
}

static void inhibit_let_go(const char *why)
{
    if (s_lg.inhibit_fd < 0) return;
    close(s_lg.inhibit_fd);
    s_lg.inhibit_fd = -1;
    fprintf(stderr, "native: logind: the sleep may go on (%s)\n", why);
}

static int on_lock(void *m, void *userdata, sdb_error_t *ret_error)
{
    (void)m;
    (void)userdata;
    (void)ret_error;
    fprintf(stderr, "native: logind: Lock: locking the screen\n");
    comp_link_command(COMP_CMD_LOCK, 1);
    return 0;
}

static int on_sleep(void *m, void *userdata, sdb_error_t *ret_error)
{
    (void)userdata;
    (void)ret_error;
    int going = 0;
    if (sd.message_read(m, "b", &going) < 0) return 0;
    if (going) {
        fprintf(stderr, "native: logind: PrepareForSleep: locking the screen before the sleep\n");
        comp_link_command(COMP_CMD_LOCK, 1);
        s_lg.sleeping = true;
        s_lg.sleep_at_ms = ms_now();
    } else {
        fprintf(stderr, "native: logind: woken\n");
        s_lg.sleeping = false;
        inhibit_take();
    }
    return 0;
}

/* The session's object path: XDG_SESSION_ID's, else logind's answer for
 * this process. NULL when neither. */
static char *session_path(void)
{
    const char *id = getenv("XDG_SESSION_ID");
    char *path = NULL;
    if (id != NULL && id[0] != '\0') {
        if (sd.path_encode("/org/freedesktop/login1/session", id, &path) >= 0) return path;
    }
    sdb_error_t err = { NULL, NULL, 0 };
    void *reply = NULL;
    int rc = sd.call_method(s_lg.bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                            "org.freedesktop.login1.Manager", "GetSessionByPID", &err, &reply, "u",
                            (uint32_t)getpid());
    const char *o = NULL;
    if (rc >= 0 && sd.message_read(reply, "o", &o) >= 0 && o != NULL) path = strdup(o);
    sd.error_free(&err);
    if (reply) sd.message_unref(reply);
    return path;
}

static void *watch(void *arg)
{
    (void)arg;
    while (!atomic_load(&s_lg.stop)) {
        for (;;) {
            int r = sd.process(s_lg.bus, NULL);
            if (r <= 0) break;
        }
        if (s_lg.sleeping) {
            if (comp_link_lock_state() == COMP_LOCK_LOCKED) {
                s_lg.sleeping = false;
                inhibit_let_go("the screen is locked");
            } else if (ms_now() - s_lg.sleep_at_ms >= SLEEP_WAIT_MS) {
                s_lg.sleeping = false;
                inhibit_let_go("the lock did not come in time");
            }
        }
        sd.wait(s_lg.bus, s_lg.sleeping ? 50000 : 500000);
    }
    inhibit_let_go("the program ends");
    sd.flush_close_unref(s_lg.bus);
    s_lg.bus = NULL;
    return NULL;
}

bool linux_logind_watch_start(void)
{
    if (atomic_load(&s_lg.running)) return true;
    char why[200];
    if (!load(why, sizeof(why))) {
        fprintf(stderr, "native: logind: not watched (%s)\n", why);
        return false;
    }
    int rc = sd.open_system(&s_lg.bus);
    if (rc < 0) {
        fprintf(stderr, "native: logind: not watched (system bus: %s)\n", strerror(-rc));
        return false;
    }
    char *path = session_path();
    if (path != NULL) {
        sd.match_signal(s_lg.bus, NULL, "org.freedesktop.login1", path, "org.freedesktop.login1.Session", "Lock",
                        on_lock, NULL);
    }
    sd.match_signal(s_lg.bus, NULL, "org.freedesktop.login1", "/org/freedesktop/login1",
                    "org.freedesktop.login1.Manager", "PrepareForSleep", on_sleep, NULL);
    inhibit_take();
    fprintf(stderr, "native: logind: watching %s for Lock, and PrepareForSleep%s\n", path ? path : "(no session)",
            s_lg.inhibit_fd >= 0 ? " (a delay inhibitor held)" : "");
    free(path);
    atomic_store(&s_lg.stop, false);
    if (pthread_create(&s_lg.thread, NULL, watch, NULL) != 0) {
        sd.flush_close_unref(s_lg.bus);
        s_lg.bus = NULL;
        return false;
    }
    atomic_store(&s_lg.running, true);
    return true;
}

void linux_logind_watch_stop(void)
{
    if (!atomic_load(&s_lg.running)) return;
    atomic_store(&s_lg.stop, true);
    pthread_join(s_lg.thread, NULL);
    atomic_store(&s_lg.running, false);
}
