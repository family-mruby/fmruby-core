/*
 * linux_net.c - see linux_net.h.
 */

#define _GNU_SOURCE
#include "linux_net.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "linux_bus.h"

#define NM "org.freedesktop.NetworkManager"
#define NM_PATH "/org/freedesktop/NetworkManager"
#define NM_SETTINGS_PATH "/org/freedesktop/NetworkManager/Settings"
#define NM_IF_DEVICE "org.freedesktop.NetworkManager.Device"
#define NM_IF_WIRELESS "org.freedesktop.NetworkManager.Device.Wireless"
#define NM_IF_AP "org.freedesktop.NetworkManager.AccessPoint"
#define NM_IF_ACTIVE "org.freedesktop.NetworkManager.Connection.Active"
#define NM_IF_SETTINGS "org.freedesktop.NetworkManager.Settings"
#define NM_IF_CONN "org.freedesktop.NetworkManager.Settings.Connection"
#define AGENT_PATH "/org/freedesktop/NetworkManager/SecretAgent"
#define AGENT_IF "org.freedesktop.NetworkManager.SecretAgent"
#define AGENT_ID "family-mruby"

#define NET_AP_MAX 40
#define SAVED_MAX 64
#define PATH_CAP 128
#define PW_CAP 128

#define STALE_MS 3000
#define SAVED_STALE_MS 30000
#define SCAN_WAIT_MS 4000
#define CONNECT_MS 45000
#define CALL_USEC (25ull * 1000000ull)

/* NetworkManager's numbers. */
#define NM_DEVICE_TYPE_WIFI 2
#define NM_AC_ACTIVATED 2
#define NM_AC_DEACTIVATING 3
#define NM_AC_DEACTIVATED 4

typedef enum { ST_STARTING = 0, ST_NONE, ST_NODEVICE, ST_OFF, ST_OK } net_status_t;
static const char *const STATUS_NAMES[] = { "starting", "none", "nodevice", "off", "ok" };

/* Shared with the callers (under lock). */
static struct {
    pthread_mutex_t lock;
    net_status_t status;
    char active[33];
    char busy[12];
    char code[16];
    char arg[96];
    linux_net_ap_t ap[NET_AP_MAX];
    int nap;
    uint64_t refreshed_ms;
    bool want_refresh, want_scan, want_disconnect, want_connect;
    char c_ssid[33];
    char c_pw[PW_CAP];
    bool connecting;
} s_sh = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* The thread's own. */
static struct {
    atomic_bool started;
    pthread_t thread;
    int wake_fd;
    const lbus_t *sd;
    void *bus;
    bool agent;
    char dev[PATH_CAP];
    struct {
        char ssid[33];
        char path[PATH_CAP];
    } saved[SAVED_MAX];
    int nsaved;
    uint64_t saved_ms;
    uint64_t scan_until;
    /* the attempt under way */
    bool trying;
    char t_ssid[33];
    char t_pw[PW_CAP];
    char t_active[PATH_CAP];
    char t_conn[PATH_CAP];
    bool t_created;
    uint64_t t_deadline;
} s_th = { .wake_fd = -1 };

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void wipe(void *p, size_t n)
{
    explicit_bzero(p, n);
}

/* ---- the pure parts ---- */

static int ap_cmp(const void *a, const void *b)
{
    const linux_net_ap_t *x = a, *y = b;
    if (x->active != y->active) return x->active ? -1 : 1;
    if (x->strength != y->strength) return x->strength > y->strength ? -1 : 1;
    return strcmp(x->ssid, y->ssid);
}

int linux_net_ap_tidy(linux_net_ap_t *ap, int n)
{
    int out = 0;
    for (int i = 0; i < n; i++) {
        if (ap[i].ssid[0] == '\0') continue;
        int same = -1;
        for (int j = 0; j < out; j++) {
            if (strcmp(ap[j].ssid, ap[i].ssid) == 0) same = j;
        }
        if (same < 0) {
            ap[out++] = ap[i];
            continue;
        }
        linux_net_ap_t *k = &ap[same];
        bool active = k->active || ap[i].active;
        bool saved = k->saved || ap[i].saved;
        if (ap[i].strength > k->strength) *k = ap[i];
        k->active = active;
        k->saved = saved;
    }
    qsort(ap, (size_t)out, sizeof(ap[0]), ap_cmp);
    return out;
}

const char *linux_net_key_mgmt(uint32_t flags, uint32_t wpa_flags, uint32_t rsn_flags)
{
    uint32_t sec = wpa_flags | rsn_flags;
    if (sec == 0) return (flags & 0x1) ? NULL : "";   /* privacy alone: WEP */
    if (sec & 0x200) return NULL;                     /* 802.1X (enterprise) */
    if (sec & 0x100) return "wpa-psk";
    if (sec & 0x400) return "sae";
    return NULL;                                      /* OWE and others */
}

const char *linux_net_failure_code(uint32_t reason, bool had_password)
{
    switch (reason) {
    case 7:                      /* NO_SECRETS */
        return had_password ? "auth_failed" : "need_password";
    case 8: case 9: case 10: case 11:   /* the supplicant's failures */
        return "auth_failed";
    case 53:                     /* SSID_NOT_FOUND */
        return "no_network";
    default:
        return "failed";
    }
}

/* ---- the callers' side ---- */

static void kick(void)
{
    lbus_wake(s_th.wake_fd);
}

/* Appends raw text, or a field (a tab or a line break inside it as a
 * space). */
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

/* The text is made here (under the lock), then copied out. */
static char s_text[NET_AP_MAX * 64 + 256];

int32_t linux_net_list(bool scan, char *buf, size_t cap)
{
    linux_net_start();
    pthread_mutex_lock(&s_sh.lock);
    uint64_t now = ms_now();
    bool wake = false;
    if (scan && !s_sh.want_scan) {
        s_sh.want_scan = true;
        wake = true;
    }
    if (now - s_sh.refreshed_ms >= STALE_MS && !s_sh.want_refresh) {
        s_sh.want_refresh = true;
        wake = true;
    }
    size_t at = 0, room = sizeof(s_text);
    char num[48];
    at = put_raw(s_text, room, at, "wifi\t");
    at = put_raw(s_text, room, at, STATUS_NAMES[s_sh.status]);
    const char *fields[4] = { s_sh.active, s_sh.busy, s_sh.code, s_sh.arg };
    for (int i = 0; i < 4; i++) {
        at = put_raw(s_text, room, at, "\t");
        at = put_field(s_text, room, at, fields[i]);
    }
    at = put_raw(s_text, room, at, "\n");
    for (int i = 0; i < s_sh.nap; i++) {
        const linux_net_ap_t *a = &s_sh.ap[i];
        at = put_raw(s_text, room, at, "ap\t");
        at = put_field(s_text, room, at, a->ssid);
        snprintf(num, sizeof(num), "\t%u\t%d\t%d\t%d\n", a->strength, a->secured, a->active, a->saved);
        at = put_raw(s_text, room, at, num);
    }
    s_text[at] = '\0';
    if (buf != NULL && at < cap) memcpy(buf, s_text, at + 1);
    pthread_mutex_unlock(&s_sh.lock);
    if (wake) kick();
    return (int32_t)at;
}

fmrb_err_t linux_net_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > 32) return FMRB_ERR_INVALID_PARAM;
    if (password != NULL && strlen(password) >= PW_CAP) return FMRB_ERR_INVALID_PARAM;
    linux_net_start();
    pthread_mutex_lock(&s_sh.lock);
    if (s_sh.connecting || s_sh.want_connect) {
        pthread_mutex_unlock(&s_sh.lock);
        return FMRB_ERR_BUSY;
    }
    snprintf(s_sh.c_ssid, sizeof(s_sh.c_ssid), "%s", ssid);
    snprintf(s_sh.c_pw, sizeof(s_sh.c_pw), "%s", password ? password : "");
    s_sh.want_connect = true;
    s_sh.connecting = true;
    snprintf(s_sh.busy, sizeof(s_sh.busy), "connecting");
    s_sh.code[0] = '\0';
    snprintf(s_sh.arg, sizeof(s_sh.arg), "%s", ssid);
    pthread_mutex_unlock(&s_sh.lock);
    kick();
    return FMRB_OK;
}

fmrb_err_t linux_net_disconnect(void)
{
    linux_net_start();
    pthread_mutex_lock(&s_sh.lock);
    s_sh.want_disconnect = true;
    pthread_mutex_unlock(&s_sh.lock);
    kick();
    return FMRB_OK;
}

/* ---- the thread: NetworkManager calls ---- */

static void outcome(const char *code, const char *arg)
{
    pthread_mutex_lock(&s_sh.lock);
    snprintf(s_sh.code, sizeof(s_sh.code), "%s", code);
    snprintf(s_sh.arg, sizeof(s_sh.arg), "%s", arg ? arg : "");
    pthread_mutex_unlock(&s_sh.lock);
}

static bool get_prop(const char *path, const char *iface, const char *member, const char *type, void **reply,
                     char *why, size_t cap)
{
    lbus_error_t err = LBUS_ERROR_NULL;
    int rc = s_th.sd->get_property(s_th.bus, NM, path, iface, member, &err, reply, type);
    if (rc < 0) {
        if (why) lbus_why(&err, rc, why, cap);
        s_th.sd->error_free(&err);
        *reply = NULL;
        return false;
    }
    s_th.sd->error_free(&err);
    return true;
}

static bool get_u32(const char *path, const char *iface, const char *member, uint32_t *out)
{
    void *r = NULL;
    if (!get_prop(path, iface, member, "u", &r, NULL, 0)) return false;
    bool ok = s_th.sd->message_read(r, "u", out) >= 0;
    s_th.sd->message_unref(r);
    return ok;
}

static bool get_byte(const char *path, const char *iface, const char *member, uint8_t *out)
{
    void *r = NULL;
    if (!get_prop(path, iface, member, "y", &r, NULL, 0)) return false;
    bool ok = s_th.sd->message_read(r, "y", out) >= 0;
    s_th.sd->message_unref(r);
    return ok;
}

static bool get_bool(const char *path, const char *iface, const char *member, bool *out, char *why, size_t cap)
{
    void *r = NULL;
    if (!get_prop(path, iface, member, "b", &r, why, cap)) return false;
    int v = 0;
    bool ok = s_th.sd->message_read(r, "b", &v) >= 0;
    *out = v != 0;
    s_th.sd->message_unref(r);
    return ok;
}

static bool get_obj(const char *path, const char *iface, const char *member, char *out, size_t cap)
{
    void *r = NULL;
    if (!get_prop(path, iface, member, "o", &r, NULL, 0)) return false;
    const char *o = NULL;
    bool ok = s_th.sd->message_read(r, "o", &o) >= 0 && o != NULL;
    snprintf(out, cap, "%s", ok ? o : "/");
    s_th.sd->message_unref(r);
    return ok;
}

static bool get_ssid(const char *ap, char *out, size_t cap)
{
    void *r = NULL;
    out[0] = '\0';
    if (!get_prop(ap, NM_IF_AP, "Ssid", "ay", &r, NULL, 0)) return false;
    const void *p = NULL;
    size_t n = 0;
    bool ok = s_th.sd->message_read_array(r, 'y', &p, &n) >= 0;
    if (ok) {
        if (n >= cap) n = cap - 1;
        if (n > 0) memcpy(out, p, n);
        out[n] = '\0';
        /* not text: shown as it is, a tab or a line break as a space */
        for (size_t i = 0; i < n; i++) {
            if ((unsigned char)out[i] < 0x20) out[i] = ' ';
        }
    }
    s_th.sd->message_unref(r);
    return ok;
}

/* Calls a method that returns a list of object paths ("ao"); fn gets each. */
static bool each_path(const char *path, const char *iface, const char *member, void (*fn)(const char *, void *),
                      void *ud, char *why, size_t cap)
{
    lbus_error_t err = LBUS_ERROR_NULL;
    void *r = NULL;
    int rc = s_th.sd->call_method(s_th.bus, NM, path, iface, member, &err, &r, "");
    if (rc < 0) {
        if (why) lbus_why(&err, rc, why, cap);
        s_th.sd->error_free(&err);
        return false;
    }
    s_th.sd->error_free(&err);
    if (s_th.sd->message_enter_container(r, 'a', "o") > 0) {
        const char *o = NULL;
        while (s_th.sd->message_read_basic(r, 'o', &o) > 0) fn(o, ud);
        s_th.sd->message_exit_container(r);
    }
    s_th.sd->message_unref(r);
    return true;
}

static void pick_wifi(const char *dev, void *ud)
{
    (void)ud;
    if (s_th.dev[0] != '\0') return;
    uint32_t type = 0;
    if (get_u32(dev, NM_IF_DEVICE, "DeviceType", &type) && type == NM_DEVICE_TYPE_WIFI) {
        snprintf(s_th.dev, sizeof(s_th.dev), "%s", dev);
    }
}

/* The SSID of a saved connection's settings (a{sa{sv}}), "" when it is not
 * a Wi-Fi one. */
static void settings_ssid(void *m, char *ssid, size_t cap)
{
    const lbus_t *sd = s_th.sd;
    ssid[0] = '\0';
    if (sd->message_enter_container(m, 'a', "{sa{sv}}") <= 0) return;
    while (sd->message_enter_container(m, 'e', "sa{sv}") > 0) {
        const char *group = NULL;
        sd->message_read_basic(m, 's', &group);
        bool wifi = group != NULL && strcmp(group, "802-11-wireless") == 0;
        if (sd->message_enter_container(m, 'a', "{sv}") > 0) {
            while (sd->message_enter_container(m, 'e', "sv") > 0) {
                const char *key = NULL;
                sd->message_read_basic(m, 's', &key);
                if (wifi && key != NULL && strcmp(key, "ssid") == 0) {
                    lbus_read_variant(sd, m, 'a', ssid, cap);
                } else {
                    sd->message_skip(m, "v");
                }
                sd->message_exit_container(m);
            }
            sd->message_exit_container(m);
        }
        sd->message_exit_container(m);
    }
    sd->message_exit_container(m);
}

static void add_saved(const char *conn, void *ud)
{
    (void)ud;
    if (s_th.nsaved >= SAVED_MAX) return;
    lbus_error_t err = LBUS_ERROR_NULL;
    void *r = NULL;
    int rc = s_th.sd->call_method(s_th.bus, NM, conn, NM_IF_CONN, "GetSettings", &err, &r, "");
    s_th.sd->error_free(&err);
    if (rc < 0) return;
    char ssid[33];
    settings_ssid(r, ssid, sizeof(ssid));
    s_th.sd->message_unref(r);
    if (ssid[0] == '\0') return;
    snprintf(s_th.saved[s_th.nsaved].ssid, sizeof(s_th.saved[0].ssid), "%s", ssid);
    snprintf(s_th.saved[s_th.nsaved].path, sizeof(s_th.saved[0].path), "%s", conn);
    s_th.nsaved++;
}

static const char *saved_path(const char *ssid)
{
    for (int i = 0; i < s_th.nsaved; i++) {
        if (strcmp(s_th.saved[i].ssid, ssid) == 0) return s_th.saved[i].path;
    }
    return NULL;
}

typedef struct {
    linux_net_ap_t ap[NET_AP_MAX];
    char path[NET_AP_MAX][PATH_CAP];
    char key[NET_AP_MAX][8];      /* the key management a new connection needs ("-": none we set up) */
    int n;
    char active_ap[PATH_CAP];
} scan_t;

static scan_t s_scan;

static void add_ap(const char *ap, void *ud)
{
    scan_t *sc = ud;
    if (sc->n >= NET_AP_MAX) return;
    linux_net_ap_t *a = &sc->ap[sc->n];
    memset(a, 0, sizeof(*a));
    if (!get_ssid(ap, a->ssid, sizeof(a->ssid))) return;
    get_byte(ap, NM_IF_AP, "Strength", &a->strength);
    uint32_t flags = 0, wpa = 0, rsn = 0;
    get_u32(ap, NM_IF_AP, "Flags", &flags);
    get_u32(ap, NM_IF_AP, "WpaFlags", &wpa);
    get_u32(ap, NM_IF_AP, "RsnFlags", &rsn);
    const char *km = linux_net_key_mgmt(flags, wpa, rsn);
    a->secured = km == NULL || km[0] != '\0';
    a->active = strcmp(ap, sc->active_ap) == 0;
    a->saved = saved_path(a->ssid) != NULL;
    snprintf(sc->path[sc->n], PATH_CAP, "%s", ap);
    snprintf(sc->key[sc->n], sizeof(sc->key[0]), "%s", km ? km : "-");
    sc->n++;
}

static void set_status(net_status_t st)
{
    pthread_mutex_lock(&s_sh.lock);
    s_sh.status = st;
    if (st != ST_OK) {
        s_sh.nap = 0;
        s_sh.active[0] = '\0';
    }
    s_sh.refreshed_ms = ms_now();
    pthread_mutex_unlock(&s_sh.lock);
}

static void agent_register(void)
{
    if (s_th.agent) return;
    lbus_error_t err = LBUS_ERROR_NULL;
    int rc = s_th.sd->call_method(s_th.bus, NM, NM_PATH "/AgentManager", NM ".AgentManager", "Register", &err, NULL,
                                  "s", AGENT_ID);
    if (rc >= 0) {
        s_th.agent = true;
    } else {
        char why[160];
        fprintf(stderr, "native: wifi: not a secret agent (%s): saved networks without a kept password will "
                        "not connect\n", lbus_why(&err, rc, why, sizeof(why)));
    }
    s_th.sd->error_free(&err);
}

static void refresh(void)
{
    char why[200];
    bool on = false;
    if (!get_bool(NM_PATH, NM, "WirelessEnabled", &on, why, sizeof(why))) {
        if (s_th.agent || s_th.dev[0]) fprintf(stderr, "native: wifi: NetworkManager went away (%s)\n", why);
        s_th.agent = false;
        s_th.dev[0] = '\0';
        set_status(ST_NONE);
        return;
    }
    agent_register();
    if (s_th.dev[0] == '\0') {
        each_path(NM_PATH, NM, "GetDevices", pick_wifi, NULL, NULL, 0);
        if (s_th.dev[0] != '\0') fprintf(stderr, "native: wifi: NetworkManager's device %s\n", s_th.dev);
    }
    if (s_th.dev[0] == '\0') {
        set_status(ST_NODEVICE);
        return;
    }
    if (!on) {
        set_status(ST_OFF);
        return;
    }
    uint64_t now = ms_now();
    if (s_th.saved_ms == 0 || now - s_th.saved_ms >= SAVED_STALE_MS) {
        s_th.nsaved = 0;
        each_path(NM_SETTINGS_PATH, NM_IF_SETTINGS, "ListConnections", add_saved, NULL, NULL, 0);
        s_th.saved_ms = now;
    }
    scan_t *sc = &s_scan;
    memset(sc, 0, sizeof(*sc));
    if (!get_obj(s_th.dev, NM_IF_WIRELESS, "ActiveAccessPoint", sc->active_ap, sizeof(sc->active_ap))) {
        sc->active_ap[0] = '\0';
    }
    if (!each_path(s_th.dev, NM_IF_WIRELESS, "GetAllAccessPoints", add_ap, sc, why, sizeof(why))) {
        /* the device went away */
        s_th.dev[0] = '\0';
        set_status(ST_NODEVICE);
        return;
    }
    char active[33] = "";
    for (int i = 0; i < sc->n; i++) {
        if (sc->ap[i].active) snprintf(active, sizeof(active), "%s", sc->ap[i].ssid);
    }
    linux_net_ap_t tidy[NET_AP_MAX];
    memcpy(tidy, sc->ap, sizeof(tidy[0]) * (size_t)sc->n);
    int n = linux_net_ap_tidy(tidy, sc->n);
    pthread_mutex_lock(&s_sh.lock);
    s_sh.status = ST_OK;
    memcpy(s_sh.ap, tidy, sizeof(tidy[0]) * (size_t)n);
    s_sh.nap = n;
    snprintf(s_sh.active, sizeof(s_sh.active), "%s", active);
    s_sh.refreshed_ms = ms_now();
    pthread_mutex_unlock(&s_sh.lock);
}

/* The strongest access point of an SSID in the last refresh, and the key
 * management a new connection to it needs. */
static bool find_ap(const char *ssid, char *path, size_t cap, char *key, size_t kcap)
{
    int best = -1;
    for (int i = 0; i < s_scan.n; i++) {
        if (strcmp(s_scan.ap[i].ssid, ssid) != 0) continue;
        if (best < 0 || s_scan.ap[i].strength > s_scan.ap[best].strength) best = i;
    }
    if (best < 0) return false;
    snprintf(path, cap, "%s", s_scan.path[best]);
    snprintf(key, kcap, "%s", s_scan.key[best]);
    return true;
}

static void append_sv_s(void *m, const char *key, const char *val)
{
    const lbus_t *sd = s_th.sd;
    sd->message_open_container(m, 'e', "sv");
    sd->message_append_basic(m, 's', key);
    sd->message_open_container(m, 'v', "s");
    sd->message_append_basic(m, 's', val);
    sd->message_close_container(m);
    sd->message_close_container(m);
}

static void attempt_end(const char *code, const char *arg)
{
    if (s_th.t_created && s_th.t_conn[0] && strcmp(code, "connected") != 0) {
        /* The connection made for this attempt (it is ours): not kept. */
        lbus_error_t err = LBUS_ERROR_NULL;
        s_th.sd->call_method(s_th.bus, NM, s_th.t_conn, NM_IF_CONN, "Delete", &err, NULL, "");
        s_th.sd->error_free(&err);
    }
    fprintf(stderr, "native: wifi: %s%s%s\n", code, arg && arg[0] ? " " : "", arg ? arg : "");
    wipe(s_th.t_pw, sizeof(s_th.t_pw));
    s_th.trying = false;
    s_th.t_created = false;
    s_th.t_active[0] = s_th.t_conn[0] = '\0';
    s_th.saved_ms = 0;   /* the saved ones may have changed */
    pthread_mutex_lock(&s_sh.lock);
    s_sh.connecting = false;
    s_sh.busy[0] = '\0';
    snprintf(s_sh.code, sizeof(s_sh.code), "%s", code);
    snprintf(s_sh.arg, sizeof(s_sh.arg), "%s", arg ? arg : "");
    pthread_mutex_unlock(&s_sh.lock);
    /* What it changed (the active network, the saved ones), at once. */
    refresh();
}

static void attempt_start(void)
{
    const lbus_t *sd = s_th.sd;
    char ap[PATH_CAP], key[8], why[200];
    if (s_th.dev[0] == '\0') refresh();
    if (s_th.dev[0] == '\0') {
        attempt_end("failed", s_th.t_ssid);
        return;
    }
    if (!find_ap(s_th.t_ssid, ap, sizeof(ap), key, sizeof(key))) {
        attempt_end("no_network", s_th.t_ssid);
        return;
    }
    const char *saved = saved_path(s_th.t_ssid);
    lbus_error_t err = LBUS_ERROR_NULL;
    void *r = NULL;
    int rc;
    s_th.trying = true;
    s_th.t_deadline = ms_now() + CONNECT_MS;
    if (saved != NULL) {
        snprintf(s_th.t_conn, sizeof(s_th.t_conn), "%s", saved);
        fprintf(stderr, "native: wifi: connecting to \"%s\" (its saved connection)\n", s_th.t_ssid);
        rc = sd->call_method(s_th.bus, NM, NM_PATH, NM, "ActivateConnection", &err, &r, "ooo", saved, s_th.dev, ap);
        if (rc >= 0) {
            const char *a = NULL;
            sd->message_read(r, "o", &a);
            snprintf(s_th.t_active, sizeof(s_th.t_active), "%s", a ? a : "");
        }
    } else {
        if (strcmp(key, "-") == 0) {
            attempt_end("enterprise", s_th.t_ssid);
            return;
        }
        if (key[0] != '\0' && s_th.t_pw[0] == '\0') {
            attempt_end("need_password", s_th.t_ssid);
            return;
        }
        fprintf(stderr, "native: wifi: connecting to \"%s\" (a new connection%s)\n", s_th.t_ssid,
                key[0] ? ", with its password" : ", open");
        void *m = NULL;
        sd->new_method_call(s_th.bus, &m, NM, NM_PATH, NM, "AddAndActivateConnection");
        sd->message_open_container(m, 'a', "{sa{sv}}");
        /* connection */
        sd->message_open_container(m, 'e', "sa{sv}");
        sd->message_append_basic(m, 's', "connection");
        sd->message_open_container(m, 'a', "{sv}");
        append_sv_s(m, "id", s_th.t_ssid);
        append_sv_s(m, "type", "802-11-wireless");
        sd->message_close_container(m);
        sd->message_close_container(m);
        /* 802-11-wireless */
        sd->message_open_container(m, 'e', "sa{sv}");
        sd->message_append_basic(m, 's', "802-11-wireless");
        sd->message_open_container(m, 'a', "{sv}");
        sd->message_open_container(m, 'e', "sv");
        sd->message_append_basic(m, 's', "ssid");
        sd->message_open_container(m, 'v', "ay");
        sd->message_open_container(m, 'a', "y");
        for (const char *p = s_th.t_ssid; *p; p++) {
            uint8_t b = (uint8_t)*p;
            sd->message_append_basic(m, 'y', &b);
        }
        sd->message_close_container(m);
        sd->message_close_container(m);
        sd->message_close_container(m);
        append_sv_s(m, "mode", "infrastructure");
        sd->message_close_container(m);
        sd->message_close_container(m);
        if (key[0] != '\0') {
            sd->message_open_container(m, 'e', "sa{sv}");
            sd->message_append_basic(m, 's', "802-11-wireless-security");
            sd->message_open_container(m, 'a', "{sv}");
            append_sv_s(m, "key-mgmt", key);
            append_sv_s(m, "psk", s_th.t_pw);
            sd->message_close_container(m);
            sd->message_close_container(m);
        }
        sd->message_close_container(m);
        sd->message_append(m, "oo", s_th.dev, ap);
        rc = sd->call(s_th.bus, m, CALL_USEC, &err, &r);
        sd->message_unref(m);
        if (rc >= 0) {
            const char *c = NULL, *a = NULL;
            sd->message_read(r, "oo", &c, &a);
            snprintf(s_th.t_conn, sizeof(s_th.t_conn), "%s", c ? c : "");
            snprintf(s_th.t_active, sizeof(s_th.t_active), "%s", a ? a : "");
            s_th.t_created = true;
        }
    }
    if (r) sd->message_unref(r);
    if (rc < 0) {
        lbus_why(&err, rc, why, sizeof(why));
        sd->error_free(&err);
        attempt_end("refused", why);
        return;
    }
    sd->error_free(&err);
}

static void attempt_check(void)
{
    uint32_t st = 0;
    bool there = s_th.t_active[0] && get_u32(s_th.t_active, NM_IF_ACTIVE, "State", &st);
    if (there && st == NM_AC_ACTIVATED) {
        attempt_end("connected", s_th.t_ssid);
        return;
    }
    if (!there || st == NM_AC_DEACTIVATING || st == NM_AC_DEACTIVATED || ms_now() > s_th.t_deadline) {
        uint32_t reason = 0;
        void *r = NULL;
        if (get_prop(s_th.dev, NM_IF_DEVICE, "StateReason", "(uu)", &r, NULL, 0)) {
            uint32_t dst = 0;
            s_th.sd->message_read(r, "(uu)", &dst, &reason);
            s_th.sd->message_unref(r);
        }
        attempt_end(linux_net_failure_code(reason, s_th.t_pw[0] != '\0'), s_th.t_ssid);
    }
}

/* The secret agent: NetworkManager asks for the password of the network
 * being connected to. */
static int on_agent(void *m, void *userdata, lbus_error_t *ret_error)
{
    (void)userdata;
    (void)ret_error;
    const lbus_t *sd = s_th.sd;
    if (sd->message_is_method_call(m, AGENT_IF, "GetSecrets") > 0) {
        sd->message_skip(m, "a{sa{sv}}");
        const char *path = NULL, *setting = NULL;
        uint32_t flags = 0;
        sd->message_read(m, "os", &path, &setting);
        sd->message_skip(m, "as");
        sd->message_read(m, "u", &flags);
        /* REQUEST_NEW (0x2): the password given did not work; asking again
         * would only give the same, so the attempt ends now. */
        bool ours = s_th.trying && s_th.t_pw[0] != '\0' && !(flags & 0x2) && setting != NULL &&
                    strcmp(setting, "802-11-wireless-security") == 0 &&
                    (s_th.t_conn[0] == '\0' || (path != NULL && strcmp(path, s_th.t_conn) == 0));
        if (!ours) {
            return sd->reply_method_errorf(m, AGENT_IF ".Error.NoSecrets", "no password was given");
        }
        void *rep = NULL;
        if (sd->new_method_return(m, &rep) < 0) return -ENOMEM;
        sd->message_open_container(rep, 'a', "{sa{sv}}");
        sd->message_open_container(rep, 'e', "sa{sv}");
        sd->message_append_basic(rep, 's', "802-11-wireless-security");
        sd->message_open_container(rep, 'a', "{sv}");
        append_sv_s(rep, "psk", s_th.t_pw);
        sd->message_close_container(rep);
        sd->message_close_container(rep);
        sd->message_close_container(rep);
        int rc = sd->send(s_th.bus, rep, NULL);
        sd->message_unref(rep);
        fprintf(stderr, "native: wifi: gave NetworkManager the password it asked for\n");
        return rc < 0 ? rc : 1;
    }
    if (sd->message_is_method_call(m, AGENT_IF, "CancelGetSecrets") > 0 ||
        sd->message_is_method_call(m, AGENT_IF, "SaveSecrets") > 0 ||
        sd->message_is_method_call(m, AGENT_IF, "DeleteSecrets") > 0) {
        /* Nothing kept here: the passwords are NetworkManager's. */
        return sd->reply_method_return(m, "");
    }
    return 0;
}

static void *run(void *arg)
{
    (void)arg;
    char why[200];
    s_th.sd = lbus(why, sizeof(why));
    if (s_th.sd == NULL) {
        fprintf(stderr, "native: wifi: none (%s)\n", why);
        set_status(ST_NONE);
        return NULL;
    }
    int rc = s_th.sd->open_system(&s_th.bus);
    if (rc < 0) {
        fprintf(stderr, "native: wifi: none (no system bus: %s)\n", strerror(-rc));
        set_status(ST_NONE);
        return NULL;
    }
    s_th.sd->add_object(s_th.bus, NULL, AGENT_PATH, on_agent, NULL);
    for (;;) {
        lbus_drain(s_th.sd, s_th.bus);
        pthread_mutex_lock(&s_sh.lock);
        bool want_refresh = s_sh.want_refresh, want_scan = s_sh.want_scan;
        bool want_disc = s_sh.want_disconnect, want_conn = s_sh.want_connect;
        s_sh.want_refresh = s_sh.want_scan = s_sh.want_disconnect = s_sh.want_connect = false;
        if (want_conn) {
            snprintf(s_th.t_ssid, sizeof(s_th.t_ssid), "%s", s_sh.c_ssid);
            memcpy(s_th.t_pw, s_sh.c_pw, sizeof(s_th.t_pw));
            wipe(s_sh.c_pw, sizeof(s_sh.c_pw));
        }
        if (want_scan) snprintf(s_sh.busy, sizeof(s_sh.busy), "%s", s_sh.connecting ? "connecting" : "scanning");
        pthread_mutex_unlock(&s_sh.lock);

        if (want_disc && s_th.dev[0]) {
            lbus_error_t err = LBUS_ERROR_NULL;
            rc = s_th.sd->call_method(s_th.bus, NM, s_th.dev, NM_IF_DEVICE, "Disconnect", &err, NULL, "");
            if (rc < 0) {
                outcome("refused", lbus_why(&err, rc, why, sizeof(why)));
            } else {
                fprintf(stderr, "native: wifi: disconnected\n");
                outcome("disconnected", "");
            }
            s_th.sd->error_free(&err);
            want_refresh = true;
        }
        if (want_scan && s_th.dev[0]) {
            lbus_error_t err = LBUS_ERROR_NULL;
            void *m = NULL;
            s_th.sd->new_method_call(s_th.bus, &m, NM, s_th.dev, NM_IF_WIRELESS, "RequestScan");
            s_th.sd->message_open_container(m, 'a', "{sv}");
            s_th.sd->message_close_container(m);
            s_th.sd->call(s_th.bus, m, CALL_USEC, &err, NULL);   /* "not yet" (too soon) is fine */
            s_th.sd->message_unref(m);
            s_th.sd->error_free(&err);
            s_th.scan_until = ms_now() + SCAN_WAIT_MS;
            s_th.saved_ms = 0;
        }
        if (want_refresh || want_conn) refresh();
        if (want_conn) attempt_start();
        else if (s_th.trying) attempt_check();
        if (s_th.scan_until && ms_now() >= s_th.scan_until) {
            s_th.scan_until = 0;
            refresh();
            pthread_mutex_lock(&s_sh.lock);
            if (strcmp(s_sh.busy, "scanning") == 0) s_sh.busy[0] = '\0';
            pthread_mutex_unlock(&s_sh.lock);
        }
        int timeout = -1;
        if (s_th.trying) timeout = 400;
        else if (s_th.scan_until) timeout = 500;
        lbus_wait(s_th.sd, s_th.bus, s_th.wake_fd, timeout);
    }
    return NULL;
}

void linux_net_start(void)
{
    if (atomic_exchange(&s_th.started, true)) return;
    s_th.wake_fd = lbus_wake_fd();
    pthread_mutex_lock(&s_sh.lock);
    s_sh.want_refresh = true;
    pthread_mutex_unlock(&s_sh.lock);
    if (pthread_create(&s_th.thread, NULL, run, NULL) == 0) {
        pthread_detach(s_th.thread);
    } else {
        set_status(ST_NONE);
    }
}
