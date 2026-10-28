/*
 * linux_bt.c - see linux_bt.h.
 */

#include "linux_bt.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "linux_bus.h"

#define BLUEZ "org.bluez"
#define IF_ADAPTER "org.bluez.Adapter1"
#define IF_DEVICE "org.bluez.Device1"

#define DEV_MAX 24
#define PATH_CAP 96
#define STALE_MS 2000
#define CALL_USEC (35ull * 1000000ull)

typedef struct {
    char path[PATH_CAP];
    char addr[18];
    char name[64];
    char icon[32];
    char adapter[PATH_CAP];
    bool connected, paired, bonded, trusted;
} bt_dev_t;

static struct {
    pthread_mutex_t lock;
    int status;                 /* 0 starting, 1 none, 2 ok */
    bool powered;
    char busy[18];
    char code[16];
    char arg[128];
    bt_dev_t dev[DEV_MAX];
    int ndev;
    uint64_t refreshed_ms;
    bool want_refresh;
    int want_power;             /* -1 none, 0 off, 1 on */
    bool want_conn;
    bool conn_on;
    char conn_addr[18];
} s_sh = { .lock = PTHREAD_MUTEX_INITIALIZER, .want_power = -1 };

static struct {
    atomic_bool started;
    pthread_t thread;
    int wake_fd;
    const lbus_t *sd;
    void *bus;
    char adapter[PATH_CAP];
} s_th = { .wake_fd = -1 };

static const char *const STATUS_NAMES[] = { "starting", "none", "ok" };

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

bool linux_bt_known(bool paired, bool bonded, bool trusted)
{
    return paired || bonded || trusted;
}

static size_t put_raw(char *out, size_t cap, size_t at, const char *s)
{
    for (; *s && at + 1 < cap; s++) out[at++] = *s;
    return at;
}

static size_t put_field(char *out, size_t cap, size_t at, const char *s)
{
    for (; *s && at + 1 < cap; s++) out[at++] = (*s == '\t' || *s == '\n') ? ' ' : *s;
    return at;
}

static char s_text[DEV_MAX * 160 + 256];

int32_t linux_bt_list(char *buf, size_t cap)
{
    linux_bt_start();
    pthread_mutex_lock(&s_sh.lock);
    bool wake = false;
    if (ms_now() - s_sh.refreshed_ms >= STALE_MS && !s_sh.want_refresh) {
        s_sh.want_refresh = true;
        wake = true;
    }
    size_t at = 0, room = sizeof(s_text);
    at = put_raw(s_text, room, at, "bt\t");
    at = put_raw(s_text, room, at, STATUS_NAMES[s_sh.status]);
    at = put_raw(s_text, room, at, s_sh.powered ? "\t1\t" : "\t0\t");
    at = put_field(s_text, room, at, s_sh.busy);
    at = put_raw(s_text, room, at, "\t");
    at = put_field(s_text, room, at, s_sh.code);
    at = put_raw(s_text, room, at, "\t");
    at = put_field(s_text, room, at, s_sh.arg);
    at = put_raw(s_text, room, at, "\n");
    for (int i = 0; i < s_sh.ndev; i++) {
        const bt_dev_t *d = &s_sh.dev[i];
        at = put_raw(s_text, room, at, "dev\t");
        at = put_field(s_text, room, at, d->addr);
        at = put_raw(s_text, room, at, "\t");
        at = put_field(s_text, room, at, d->name);
        at = put_raw(s_text, room, at, d->connected ? "\t1\t" : "\t0\t");
        at = put_field(s_text, room, at, d->icon);
        at = put_raw(s_text, room, at, "\n");
    }
    s_text[at] = '\0';
    if (buf != NULL && at < cap) memcpy(buf, s_text, at + 1);
    pthread_mutex_unlock(&s_sh.lock);
    if (wake) lbus_wake(s_th.wake_fd);
    return (int32_t)at;
}

fmrb_err_t linux_bt_power(bool on)
{
    linux_bt_start();
    pthread_mutex_lock(&s_sh.lock);
    s_sh.want_power = on ? 1 : 0;
    pthread_mutex_unlock(&s_sh.lock);
    lbus_wake(s_th.wake_fd);
    return FMRB_OK;
}

fmrb_err_t linux_bt_connect(const char *address, bool on)
{
    if (address == NULL || strlen(address) != 17) return FMRB_ERR_INVALID_PARAM;
    linux_bt_start();
    pthread_mutex_lock(&s_sh.lock);
    if (s_sh.want_conn || s_sh.busy[0]) {
        pthread_mutex_unlock(&s_sh.lock);
        return FMRB_ERR_BUSY;
    }
    s_sh.want_conn = true;
    s_sh.conn_on = on;
    snprintf(s_sh.conn_addr, sizeof(s_sh.conn_addr), "%s", address);
    snprintf(s_sh.busy, sizeof(s_sh.busy), "%s", address);
    s_sh.code[0] = '\0';
    s_sh.arg[0] = '\0';
    pthread_mutex_unlock(&s_sh.lock);
    lbus_wake(s_th.wake_fd);
    return FMRB_OK;
}

/* ---- the thread ---- */

static int dev_cmp(const void *a, const void *b)
{
    const bt_dev_t *x = a, *y = b;
    if (x->connected != y->connected) return x->connected ? -1 : 1;
    return strcmp(x->name, y->name);
}

/* One interface's properties a{sv}, into an adapter's or a device's
 * fields. */
static void read_props(void *m, bool adapter, bt_dev_t *d, bool *powered)
{
    const lbus_t *sd = s_th.sd;
    if (sd->message_enter_container(m, 'a', "{sv}") <= 0) return;
    while (sd->message_enter_container(m, 'e', "sv") > 0) {
        const char *key = NULL;
        sd->message_read_basic(m, 's', &key);
        int b = 0;
        if (key == NULL) {
            sd->message_skip(m, "v");
        } else if (adapter && strcmp(key, "Powered") == 0) {
            if (lbus_read_variant(sd, m, 'b', &b, 0)) *powered = b != 0;
        } else if (!adapter && strcmp(key, "Address") == 0) {
            lbus_read_variant(sd, m, 's', d->addr, sizeof(d->addr));
        } else if (!adapter && strcmp(key, "Alias") == 0) {
            lbus_read_variant(sd, m, 's', d->name, sizeof(d->name));
        } else if (!adapter && strcmp(key, "Name") == 0 && d->name[0] == '\0') {
            lbus_read_variant(sd, m, 's', d->name, sizeof(d->name));
        } else if (!adapter && strcmp(key, "Icon") == 0) {
            lbus_read_variant(sd, m, 's', d->icon, sizeof(d->icon));
        } else if (!adapter && strcmp(key, "Adapter") == 0) {
            lbus_read_variant(sd, m, 'o', d->adapter, sizeof(d->adapter));
        } else if (!adapter && (strcmp(key, "Connected") == 0 || strcmp(key, "Paired") == 0 ||
                                strcmp(key, "Bonded") == 0 || strcmp(key, "Trusted") == 0)) {
            if (lbus_read_variant(sd, m, 'b', &b, 0)) {
                if (key[0] == 'C') d->connected = b != 0;
                else if (key[0] == 'P') d->paired = b != 0;
                else if (key[0] == 'B') d->bonded = b != 0;
                else d->trusted = b != 0;
            }
        } else {
            sd->message_skip(m, "v");
        }
        sd->message_exit_container(m);
    }
    sd->message_exit_container(m);
}

static bt_dev_t s_found[DEV_MAX];

static void refresh(void)
{
    const lbus_t *sd = s_th.sd;
    lbus_error_t err = LBUS_ERROR_NULL;
    void *r = NULL;
    int rc = sd->call_method(s_th.bus, BLUEZ, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", &err,
                             &r, "");
    sd->error_free(&err);
    if (rc < 0) {
        if (s_th.adapter[0]) fprintf(stderr, "native: bluetooth: BlueZ went away\n");
        s_th.adapter[0] = '\0';
        pthread_mutex_lock(&s_sh.lock);
        s_sh.status = 1;
        s_sh.ndev = 0;
        s_sh.refreshed_ms = ms_now();
        pthread_mutex_unlock(&s_sh.lock);
        return;
    }
    char adapter[PATH_CAP] = "";
    bool powered = false;
    int n = 0;
    if (sd->message_enter_container(r, 'a', "{oa{sa{sv}}}") > 0) {
        while (sd->message_enter_container(r, 'e', "oa{sa{sv}}") > 0) {
            const char *path = NULL;
            sd->message_read_basic(r, 'o', &path);
            bt_dev_t d;
            memset(&d, 0, sizeof(d));
            bool is_dev = false;
            if (sd->message_enter_container(r, 'a', "{sa{sv}}") > 0) {
                while (sd->message_enter_container(r, 'e', "sa{sv}") > 0) {
                    const char *iface = NULL;
                    sd->message_read_basic(r, 's', &iface);
                    if (iface && strcmp(iface, IF_ADAPTER) == 0 && adapter[0] == '\0') {
                        snprintf(adapter, sizeof(adapter), "%s", path ? path : "");
                        read_props(r, true, NULL, &powered);
                    } else if (iface && strcmp(iface, IF_DEVICE) == 0) {
                        is_dev = true;
                        read_props(r, false, &d, NULL);
                    } else {
                        sd->message_skip(r, "a{sv}");
                    }
                    sd->message_exit_container(r);
                }
                sd->message_exit_container(r);
            }
            if (is_dev && n < DEV_MAX && linux_bt_known(d.paired, d.bonded, d.trusted) && d.addr[0]) {
                snprintf(d.path, sizeof(d.path), "%s", path ? path : "");
                if (d.name[0] == '\0') snprintf(d.name, sizeof(d.name), "%s", d.addr);
                s_found[n++] = d;
            }
            sd->message_exit_container(r);
        }
        sd->message_exit_container(r);
    }
    sd->message_unref(r);
    /* only the first adapter's devices */
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s_found[i].adapter[0] == '\0' || strcmp(s_found[i].adapter, adapter) == 0) s_found[k++] = s_found[i];
    }
    qsort(s_found, (size_t)k, sizeof(s_found[0]), dev_cmp);
    if (adapter[0] && strcmp(adapter, s_th.adapter) != 0) {
        fprintf(stderr, "native: bluetooth: BlueZ's adapter %s (%s, %d known devices)\n", adapter,
                powered ? "on" : "off", k);
    }
    snprintf(s_th.adapter, sizeof(s_th.adapter), "%s", adapter);
    pthread_mutex_lock(&s_sh.lock);
    s_sh.status = adapter[0] ? 2 : 1;
    s_sh.powered = powered;
    memcpy(s_sh.dev, s_found, sizeof(s_found[0]) * (size_t)k);
    s_sh.ndev = k;
    s_sh.refreshed_ms = ms_now();
    pthread_mutex_unlock(&s_sh.lock);
}

static void outcome(const char *code, const char *arg)
{
    fprintf(stderr, "native: bluetooth: %s%s%s\n", code, arg[0] ? " " : "", arg);
    pthread_mutex_lock(&s_sh.lock);
    snprintf(s_sh.code, sizeof(s_sh.code), "%s", code);
    snprintf(s_sh.arg, sizeof(s_sh.arg), "%s", arg);
    s_sh.busy[0] = '\0';
    pthread_mutex_unlock(&s_sh.lock);
}

static void do_power(bool on)
{
    char why[160];
    if (s_th.adapter[0] == '\0') {
        outcome("power_failed", "no adapter");
        return;
    }
    lbus_error_t err = LBUS_ERROR_NULL;
    int rc = s_th.sd->set_property(s_th.bus, BLUEZ, s_th.adapter, IF_ADAPTER, "Powered", &err, "b", on ? 1 : 0);
    if (rc < 0) outcome("power_failed", lbus_why(&err, rc, why, sizeof(why)));
    else outcome(on ? "powered_on" : "powered_off", "");
    s_th.sd->error_free(&err);
}

static void do_connect(const char *addr, bool on)
{
    char path[PATH_CAP] = "", name[64] = "", why[160];
    pthread_mutex_lock(&s_sh.lock);
    for (int i = 0; i < s_sh.ndev; i++) {
        if (strcmp(s_sh.dev[i].addr, addr) == 0) {
            snprintf(path, sizeof(path), "%s", s_sh.dev[i].path);
            snprintf(name, sizeof(name), "%s", s_sh.dev[i].name);
        }
    }
    pthread_mutex_unlock(&s_sh.lock);
    if (path[0] == '\0') {
        outcome("failed", "no such device");
        return;
    }
    fprintf(stderr, "native: bluetooth: %s %s (%s)\n", on ? "connecting" : "disconnecting", name, addr);
    void *m = NULL;
    s_th.sd->new_method_call(s_th.bus, &m, BLUEZ, path, IF_DEVICE, on ? "Connect" : "Disconnect");
    lbus_error_t err = LBUS_ERROR_NULL;
    int rc = s_th.sd->call(s_th.bus, m, CALL_USEC, &err, NULL);
    s_th.sd->message_unref(m);
    if (rc < 0) outcome("failed", lbus_why(&err, rc, why, sizeof(why)));
    else outcome(on ? "connected" : "disconnected", name);
    s_th.sd->error_free(&err);
}

static void *run(void *arg)
{
    (void)arg;
    char why[200];
    s_th.sd = lbus(why, sizeof(why));
    int rc = s_th.sd ? s_th.sd->open_system(&s_th.bus) : -1;
    if (s_th.sd == NULL || rc < 0) {
        fprintf(stderr, "native: bluetooth: none (%s)\n", s_th.sd ? strerror(-rc) : why);
        pthread_mutex_lock(&s_sh.lock);
        s_sh.status = 1;
        pthread_mutex_unlock(&s_sh.lock);
        return NULL;
    }
    for (;;) {
        lbus_drain(s_th.sd, s_th.bus);
        pthread_mutex_lock(&s_sh.lock);
        bool want_refresh = s_sh.want_refresh;
        int want_power = s_sh.want_power;
        bool want_conn = s_sh.want_conn, conn_on = s_sh.conn_on;
        char addr[18];
        snprintf(addr, sizeof(addr), "%s", s_sh.conn_addr);
        s_sh.want_refresh = false;
        s_sh.want_power = -1;
        s_sh.want_conn = false;
        pthread_mutex_unlock(&s_sh.lock);
        if (want_refresh || want_power >= 0 || want_conn) refresh();
        if (want_power >= 0) {
            do_power(want_power == 1);
            refresh();
        }
        if (want_conn) {
            do_connect(addr, conn_on);
            refresh();
        }
        lbus_wait(s_th.sd, s_th.bus, s_th.wake_fd, -1);
    }
    return NULL;
}

void linux_bt_start(void)
{
    if (atomic_exchange(&s_th.started, true)) return;
    s_th.wake_fd = lbus_wake_fd();
    pthread_mutex_lock(&s_sh.lock);
    s_sh.want_refresh = true;
    pthread_mutex_unlock(&s_sh.lock);
    if (pthread_create(&s_th.thread, NULL, run, NULL) == 0) pthread_detach(s_th.thread);
}
