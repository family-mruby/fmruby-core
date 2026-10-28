/*
 * fake_nm.c - NetworkManager's part the settings window uses
 * (doc/linux_native/ L14), standing in for it on a bus of the check's own
 * (a dbus-daemon given as DBUS_SYSTEM_BUS_ADDRESS): never the machine's.
 *
 *   fake_nm LOG
 *
 * It owns org.freedesktop.NetworkManager and serves:
 *
 *   /org/freedesktop/NetworkManager      WirelessEnabled, GetDevices,
 *                                        ActivateConnection,
 *                                        AddAndActivateConnection
 *   .../Devices/1 (ethernet), .../2 (Wi-Fi)  DeviceType, StateReason,
 *                                        Disconnect; the Wi-Fi one
 *                                        ActiveAccessPoint, GetAllAccessPoints,
 *                                        RequestScan
 *   .../AccessPoint/1..6                 Ssid, Strength, Flags, WpaFlags,
 *                                        RsnFlags
 *   .../Settings, .../Settings/N         ListConnections, GetSettings, Delete
 *   .../AgentManager                     Register
 *   .../ActiveConnection/N               State
 *
 * The networks: fmrb-open (open), fmrb-home (WPA2, saved with its password
 * kept), fmrb-cafe (WPA2, twice, not saved; the password "cafe1234"),
 * fmrb-wallet (WPA2, saved, the password not kept: asked of the registered
 * secret agent; "walletpw"), a hidden one, fmrb-corp (802.1X). An attempt
 * is decided a second after it starts: State 2 (activated) and the access
 * point active, or State 4 with the device's StateReason (7 no secrets, 8
 * the supplicant: a wrong password).
 *
 * The log: "ready", "GetDevices", "RequestScan", "Register <id>",
 * "Activate <ssid>", "AddAndActivate <ssid> key-mgmt=<k> psk=<right|wrong|
 * none>", "GetSecrets <ssid>: <right|wrong|none>", "connected <ssid>",
 * "failed <ssid> <reason>", "Delete <ssid>", "Disconnect". A password itself
 * is never written.
 *
 * A test program, not part of the product: libc and sd-bus.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <systemd/sd-bus.h>

#define NM "org.freedesktop.NetworkManager"
#define P "/org/freedesktop/NetworkManager"

static FILE *g_log;
static sd_bus *g_bus;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
    const char *ssid;
    uint8_t strength;
    uint32_t flags, wpa, rsn;
} ap_t;

#define NAP 7
static const ap_t g_ap[NAP] = {
    { "fmrb-open", 70, 0, 0, 0 },
    { "fmrb-home", 85, 1, 0, 0x188 },
    { "fmrb-cafe", 50, 1, 0, 0x188 },
    { "fmrb-cafe", 30, 1, 0, 0x188 },
    { "fmrb-wallet", 60, 1, 0, 0x188 },
    { "", 90, 1, 0, 0x188 },
    { "fmrb-corp", 40, 1, 0, 0x288 },
};

typedef struct {
    bool used;
    char ssid[33];
    char psk[64];          /* kept: "" none (asked of the agent) */
    bool want_agent;       /* the password is the agent's */
} conn_t;

#define NCONN 8
static conn_t g_conn[NCONN];
static int g_active_ap = -1;            /* index into g_ap, -1 none */
static uint32_t g_reason;
static char g_agent[128];

/* the attempt under way */
static struct {
    bool on;
    int ap;
    int conn;
    double at;
    bool ok;
    uint32_t reason;
    bool agent_pending;
    uint32_t state;        /* the active connection's: 1, 2, 4 */
    int n;                 /* the active connection's number */
} g_try;

static const char *right_psk(const char *ssid)
{
    if (strcmp(ssid, "fmrb-cafe") == 0) return "cafe1234";
    if (strcmp(ssid, "fmrb-wallet") == 0) return "walletpw";
    if (strcmp(ssid, "fmrb-home") == 0) return "homepass";
    return "";
}

static int ap_of(const char *path)
{
    int i = 0;
    if (sscanf(path, P "/AccessPoint/%d", &i) == 1 && i >= 1 && i <= NAP) return i - 1;
    return -1;
}

static int conn_of(const char *path)
{
    int i = 0;
    if (sscanf(path, P "/Settings/%d", &i) == 1 && i >= 1 && i <= NCONN && g_conn[i - 1].used) return i - 1;
    return -1;
}

/* ---- the manager ---- */

static int p_wireless(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
                      void *ud, sd_bus_error *e)
{
    (void)b; (void)path; (void)iface; (void)prop; (void)ud; (void)e;
    return sd_bus_message_append(reply, "b", 1);
}

static int m_get_devices(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    say("GetDevices");
    return sd_bus_reply_method_return(m, "ao", 2, P "/Devices/1", P "/Devices/2");
}

static void start_try(int ap, int conn, bool ok, uint32_t reason)
{
    g_try.on = true;
    g_try.ap = ap;
    g_try.conn = conn;
    g_try.at = now() + 1.0;
    g_try.ok = ok;
    g_try.reason = reason;
    g_try.agent_pending = false;
    g_try.state = 1;
    g_try.n++;
}

static int on_secrets(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    g_try.agent_pending = false;
    const char *ssid = g_conn[g_try.conn].ssid;
    if (sd_bus_message_is_method_error(m, NULL)) {
        say("GetSecrets %s: none (%s)", ssid, sd_bus_message_get_error(m)->name);
        g_try.ok = false;
        g_try.reason = 7;
        return 0;
    }
    /* a{sa{sv}}: 802-11-wireless-security.psk */
    char psk[64] = "";
    if (sd_bus_message_enter_container(m, 'a', "{sa{sv}}") > 0) {
        while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
            const char *g = NULL;
            sd_bus_message_read(m, "s", &g);
            sd_bus_message_enter_container(m, 'a', "{sv}");
            while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
                const char *k = NULL;
                sd_bus_message_read(m, "s", &k);
                if (k && strcmp(k, "psk") == 0) {
                    const char *v = NULL;
                    sd_bus_message_read(m, "v", "s", &v);
                    if (v) snprintf(psk, sizeof(psk), "%s", v);
                } else {
                    sd_bus_message_skip(m, "v");
                }
                sd_bus_message_exit_container(m);
            }
            sd_bus_message_exit_container(m);
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
    }
    bool right = strcmp(psk, right_psk(ssid)) == 0;
    say("GetSecrets %s: %s", ssid, psk[0] ? (right ? "right" : "wrong") : "none");
    g_try.ok = right;
    g_try.reason = right ? 0 : 8;
    return 0;
}

static void ask_agent(int conn)
{
    if (g_agent[0] == '\0') {
        g_try.ok = false;
        g_try.reason = 7;
        return;
    }
    sd_bus_message *m = NULL;
    sd_bus_message_new_method_call(g_bus, &m, g_agent, P "/SecretAgent", NM ".SecretAgent", "GetSecrets");
    sd_bus_message_open_container(m, 'a', "{sa{sv}}");
    sd_bus_message_open_container(m, 'e', "sa{sv}");
    sd_bus_message_append(m, "s", "connection");
    sd_bus_message_open_container(m, 'a', "{sv}");
    sd_bus_message_append(m, "{sv}", "id", "s", g_conn[conn].ssid);
    sd_bus_message_close_container(m);
    sd_bus_message_close_container(m);
    sd_bus_message_close_container(m);
    char path[64];
    snprintf(path, sizeof(path), P "/Settings/%d", conn + 1);
    sd_bus_message_append(m, "osasu", path, "802-11-wireless-security", 1, "psk", (uint32_t)1);
    g_try.agent_pending = true;
    sd_bus_call_async(g_bus, NULL, m, on_secrets, NULL, 10 * 1000000ull);
    sd_bus_message_unref(m);
}

static int m_activate(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    const char *c = NULL, *d = NULL, *a = NULL;
    sd_bus_message_read(m, "ooo", &c, &d, &a);
    int ci = conn_of(c);
    int ai = ap_of(a);
    if (ci < 0 || ai < 0) return sd_bus_error_set(e, NM ".Error.UnknownConnection", "no such connection");
    say("Activate %s", g_conn[ci].ssid);
    if (g_conn[ci].want_agent) {
        start_try(ai, ci, false, 7);
        ask_agent(ci);
    } else {
        start_try(ai, ci, true, 0);
    }
    char path[64];
    snprintf(path, sizeof(path), P "/ActiveConnection/%d", g_try.n);
    return sd_bus_reply_method_return(m, "o", path);
}

static int m_add_activate(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    char ssid[33] = "", key[16] = "", psk[64] = "";
    sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
    while (sd_bus_message_enter_container(m, 'e', "sa{sv}") > 0) {
        const char *g = NULL;
        sd_bus_message_read(m, "s", &g);
        sd_bus_message_enter_container(m, 'a', "{sv}");
        while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
            const char *k = NULL;
            sd_bus_message_read(m, "s", &k);
            if (g && k && strcmp(g, "802-11-wireless") == 0 && strcmp(k, "ssid") == 0) {
                const void *p = NULL;
                size_t n = 0;
                sd_bus_message_enter_container(m, 'v', "ay");
                sd_bus_message_read_array(m, 'y', &p, &n);
                sd_bus_message_exit_container(m);
                if (n > 32) n = 32;
                memcpy(ssid, p, n);
                ssid[n] = '\0';
            } else if (g && k && strcmp(g, "802-11-wireless-security") == 0 && strcmp(k, "key-mgmt") == 0) {
                const char *v = NULL;
                sd_bus_message_read(m, "v", "s", &v);
                snprintf(key, sizeof(key), "%s", v ? v : "");
            } else if (g && k && strcmp(g, "802-11-wireless-security") == 0 && strcmp(k, "psk") == 0) {
                const char *v = NULL;
                sd_bus_message_read(m, "v", "s", &v);
                snprintf(psk, sizeof(psk), "%s", v ? v : "");
            } else {
                sd_bus_message_skip(m, "v");
            }
            sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    const char *d = NULL, *a = NULL;
    sd_bus_message_read(m, "oo", &d, &a);
    int ai = ap_of(a);
    if (ai < 0) return sd_bus_error_set(e, NM ".Error.UnknownDevice", "no such access point");
    const char *want = right_psk(ssid);
    bool right = strcmp(psk, want) == 0;
    say("AddAndActivate %s key-mgmt=%s psk=%s", ssid, key[0] ? key : "none",
        psk[0] ? (right ? "right" : "wrong") : "none");
    int ci = -1;
    for (int i = 0; i < NCONN; i++) {
        if (!g_conn[i].used) {
            ci = i;
            break;
        }
    }
    if (ci < 0) return sd_bus_error_set(e, NM ".Error.Failed", "full");
    g_conn[ci].used = true;
    snprintf(g_conn[ci].ssid, sizeof(g_conn[ci].ssid), "%s", ssid);
    snprintf(g_conn[ci].psk, sizeof(g_conn[ci].psk), "%s", psk);
    g_conn[ci].want_agent = false;
    start_try(ai, ci, right, right ? 0 : 8);
    char cpath[64], apath[64];
    snprintf(cpath, sizeof(cpath), P "/Settings/%d", ci + 1);
    snprintf(apath, sizeof(apath), P "/ActiveConnection/%d", g_try.n);
    return sd_bus_reply_method_return(m, "oo", cpath, apath);
}

static const sd_bus_vtable manager_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("WirelessEnabled", "b", p_wireless, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("GetDevices", "", "ao", m_get_devices, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ActivateConnection", "ooo", "o", m_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AddAndActivateConnection", "a{sa{sv}}oo", "oo", m_add_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

/* ---- the devices ---- */

static int p_dev_type(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
                      void *ud, sd_bus_error *e)
{
    (void)b; (void)iface; (void)prop; (void)ud; (void)e;
    return sd_bus_message_append(reply, "u", strcmp(path, P "/Devices/2") == 0 ? 2u : 1u);
}

static int p_reason(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
                    void *ud, sd_bus_error *e)
{
    (void)b; (void)path; (void)iface; (void)prop; (void)ud; (void)e;
    return sd_bus_message_append(reply, "(uu)", g_active_ap >= 0 ? 100u : 30u, g_reason);
}

static int m_disconnect(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    say("Disconnect");
    g_active_ap = -1;
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable device_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("DeviceType", "u", p_dev_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("StateReason", "(uu)", p_reason, 0, 0),
    SD_BUS_METHOD("Disconnect", "", "", m_disconnect, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static int p_active_ap(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
                       void *ud, sd_bus_error *e)
{
    (void)b; (void)path; (void)iface; (void)prop; (void)ud; (void)e;
    char p[64] = "/";
    if (g_active_ap >= 0) snprintf(p, sizeof(p), P "/AccessPoint/%d", g_active_ap + 1);
    return sd_bus_message_append(reply, "o", p);
}

static int m_get_aps(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    sd_bus_message *r = NULL;
    sd_bus_message_new_method_return(m, &r);
    sd_bus_message_open_container(r, 'a', "o");
    for (int i = 0; i < NAP; i++) {
        char p[64];
        snprintf(p, sizeof(p), P "/AccessPoint/%d", i + 1);
        sd_bus_message_append(r, "o", p);
    }
    sd_bus_message_close_container(r);
    int rc = sd_bus_send(NULL, r, NULL);
    sd_bus_message_unref(r);
    return rc;
}

static int m_scan(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    say("RequestScan");
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable wireless_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("ActiveAccessPoint", "o", p_active_ap, 0, 0),
    SD_BUS_METHOD("GetAllAccessPoints", "", "ao", m_get_aps, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RequestScan", "a{sv}", "", m_scan, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

/* ---- the access points ---- */

static int p_ap(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply, void *ud,
                sd_bus_error *e)
{
    (void)b; (void)iface; (void)ud; (void)e;
    int i = ap_of(path);
    if (i < 0) return -ENOENT;
    const ap_t *a = &g_ap[i];
    if (strcmp(prop, "Ssid") == 0) return sd_bus_message_append_array(reply, 'y', a->ssid, strlen(a->ssid));
    if (strcmp(prop, "Strength") == 0) return sd_bus_message_append(reply, "y", a->strength);
    if (strcmp(prop, "Flags") == 0) return sd_bus_message_append(reply, "u", a->flags);
    if (strcmp(prop, "WpaFlags") == 0) return sd_bus_message_append(reply, "u", a->wpa);
    return sd_bus_message_append(reply, "u", a->rsn);
}

static const sd_bus_vtable ap_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Ssid", "ay", p_ap, 0, 0),
    SD_BUS_PROPERTY("Strength", "y", p_ap, 0, 0),
    SD_BUS_PROPERTY("Flags", "u", p_ap, 0, 0),
    SD_BUS_PROPERTY("WpaFlags", "u", p_ap, 0, 0),
    SD_BUS_PROPERTY("RsnFlags", "u", p_ap, 0, 0),
    SD_BUS_VTABLE_END,
};

static int ap_find(sd_bus *b, const char *path, const char *iface, void *ud, void **found, sd_bus_error *e)
{
    (void)b; (void)iface; (void)ud; (void)e;
    if (ap_of(path) < 0) return 0;
    *found = NULL;
    return 1;
}

/* ---- the settings ---- */

static int m_list(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    sd_bus_message *r = NULL;
    sd_bus_message_new_method_return(m, &r);
    sd_bus_message_open_container(r, 'a', "o");
    for (int i = 0; i < NCONN; i++) {
        if (!g_conn[i].used) continue;
        char p[64];
        snprintf(p, sizeof(p), P "/Settings/%d", i + 1);
        sd_bus_message_append(r, "o", p);
    }
    sd_bus_message_close_container(r);
    int rc = sd_bus_send(NULL, r, NULL);
    sd_bus_message_unref(r);
    return rc;
}

static const sd_bus_vtable settings_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListConnections", "", "ao", m_list, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static int m_get_settings(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    int i = conn_of(sd_bus_message_get_path(m));
    if (i < 0) return sd_bus_error_set(e, NM ".Settings.InvalidConnection", "gone");
    sd_bus_message *r = NULL;
    sd_bus_message_new_method_return(m, &r);
    sd_bus_message_open_container(r, 'a', "{sa{sv}}");
    sd_bus_message_open_container(r, 'e', "sa{sv}");
    sd_bus_message_append(r, "s", "connection");
    sd_bus_message_open_container(r, 'a', "{sv}");
    sd_bus_message_append(r, "{sv}", "id", "s", g_conn[i].ssid);
    sd_bus_message_append(r, "{sv}", "type", "s", "802-11-wireless");
    sd_bus_message_close_container(r);
    sd_bus_message_close_container(r);
    sd_bus_message_open_container(r, 'e', "sa{sv}");
    sd_bus_message_append(r, "s", "802-11-wireless");
    sd_bus_message_open_container(r, 'a', "{sv}");
    sd_bus_message_open_container(r, 'e', "sv");
    sd_bus_message_append(r, "s", "ssid");
    sd_bus_message_open_container(r, 'v', "ay");
    sd_bus_message_append_array(r, 'y', g_conn[i].ssid, strlen(g_conn[i].ssid));
    sd_bus_message_close_container(r);
    sd_bus_message_close_container(r);
    sd_bus_message_append(r, "{sv}", "mode", "s", "infrastructure");
    sd_bus_message_close_container(r);
    sd_bus_message_close_container(r);
    sd_bus_message_close_container(r);
    int rc = sd_bus_send(NULL, r, NULL);
    sd_bus_message_unref(r);
    return rc;
}

static int m_delete(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    int i = conn_of(sd_bus_message_get_path(m));
    if (i < 0) return sd_bus_error_set(e, NM ".Settings.InvalidConnection", "gone");
    say("Delete %s", g_conn[i].ssid);
    g_conn[i].used = false;
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable conn_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetSettings", "", "a{sa{sv}}", m_get_settings, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Delete", "", "", m_delete, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

/* ---- the agent manager, the active connection ---- */

static int m_register(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud; (void)e;
    const char *id = NULL;
    sd_bus_message_read(m, "s", &id);
    snprintf(g_agent, sizeof(g_agent), "%s", sd_bus_message_get_sender(m));
    say("Register %s", id ? id : "");
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable agent_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Register", "s", "", m_register, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static int p_ac_state(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
                      void *ud, sd_bus_error *e)
{
    (void)b; (void)iface; (void)prop; (void)ud; (void)e;
    int n = 0;
    sscanf(path, P "/ActiveConnection/%d", &n);
    uint32_t st = n == g_try.n ? g_try.state : 4u;
    return sd_bus_message_append(reply, "u", st);
}

static const sd_bus_vtable ac_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("State", "u", p_ac_state, 0, 0),
    SD_BUS_VTABLE_END,
};

static int ac_find(sd_bus *b, const char *path, const char *iface, void *ud, void **found, sd_bus_error *e)
{
    (void)b; (void)iface; (void)ud; (void)e;
    int n = 0;
    if (sscanf(path, P "/ActiveConnection/%d", &n) != 1) return 0;
    *found = NULL;
    return 1;
}

/* An attempt's end, a second after its start (and after the agent). */
static void decide(void)
{
    if (!g_try.on || g_try.agent_pending || now() < g_try.at) return;
    g_try.on = false;
    const char *ssid = g_conn[g_try.conn].ssid;
    if (g_try.ok) {
        g_active_ap = g_try.ap;
        g_reason = 0;
        g_try.state = 2;
        say("connected %s", ssid);
    } else {
        g_active_ap = -1;
        g_reason = g_try.reason;
        g_try.state = 4;
        say("failed %s %u", ssid, g_try.reason);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fake_nm LOG\n");
        return 2;
    }
    g_log = fopen(argv[1], "a");
    if (!g_log) return 1;
    if (sd_bus_open_system(&g_bus) < 0) {
        say("error: no bus");
        return 1;
    }
    /* saved: fmrb-home (the password kept), fmrb-wallet (the agent's) */
    g_conn[0].used = true;
    snprintf(g_conn[0].ssid, sizeof(g_conn[0].ssid), "fmrb-home");
    snprintf(g_conn[0].psk, sizeof(g_conn[0].psk), "homepass");
    g_conn[1].used = true;
    snprintf(g_conn[1].ssid, sizeof(g_conn[1].ssid), "fmrb-wallet");
    g_conn[1].want_agent = true;

    sd_bus_add_object_vtable(g_bus, NULL, P, NM, manager_vt, NULL);
    sd_bus_add_object_vtable(g_bus, NULL, P "/Devices/1", NM ".Device", device_vt, NULL);
    sd_bus_add_object_vtable(g_bus, NULL, P "/Devices/2", NM ".Device", device_vt, NULL);
    sd_bus_add_object_vtable(g_bus, NULL, P "/Devices/2", NM ".Device.Wireless", wireless_vt, NULL);
    sd_bus_add_fallback_vtable(g_bus, NULL, P "/AccessPoint", NM ".AccessPoint", ap_vt, ap_find, NULL);
    sd_bus_add_object_vtable(g_bus, NULL, P "/Settings", NM ".Settings", settings_vt, NULL);
    /* One object per slot (a fallback on the Settings object's own path is
     * not looked up for its children); the methods refuse an unused one. */
    for (int i = 0; i < NCONN; i++) {
        char path[64];
        snprintf(path, sizeof(path), P "/Settings/%d", i + 1);
        sd_bus_add_object_vtable(g_bus, NULL, path, NM ".Settings.Connection", conn_vt, NULL);
    }
    sd_bus_add_object_vtable(g_bus, NULL, P "/AgentManager", NM ".AgentManager", agent_vt, NULL);
    sd_bus_add_fallback_vtable(g_bus, NULL, P "/ActiveConnection", NM ".Connection.Active", ac_vt, ac_find, NULL);
    if (sd_bus_request_name(g_bus, NM, 0) < 0) {
        say("error: the name");
        return 1;
    }
    say("ready");
    for (;;) {
        while (sd_bus_process(g_bus, NULL) > 0) {
        }
        decide();
        sd_bus_wait(g_bus, 100000);
    }
}
