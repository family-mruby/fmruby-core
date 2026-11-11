// The saved Wi-Fi networks: wifi.toml to a struct and back (wifi_conf.h).
// Pure C, shared by the devices' Wi-Fi task and the host test.

#include "wifi_conf.h"

#include <stdio.h>
#include <string.h>

void wifi_conf_init(wifi_conf_t *conf)
{
    memset(conf, 0, sizeof(*conf));
    conf->enable = true;
}

void wifi_conf_wipe(wifi_conf_t *conf)
{
    volatile char *p = (volatile char *)conf;
    for (size_t i = 0; i < sizeof(*conf); i++) p[i] = 0;
}

int wifi_conf_find(const wifi_conf_t *conf, const char *ssid)
{
    if (!ssid || !ssid[0]) return -1;
    for (int i = 0; i < conf->count; i++) {
        if (strcmp(conf->nets[i].ssid, ssid) == 0) return i;
    }
    return -1;
}

// One string value, copied into dst when it fits; the tomlc99 copy is
// cleared and released either way. false: missing, not a string or too long.
static bool take_string(const toml_table_t *tab, const char *key, char *dst, size_t cap,
                        void (*xfree)(void *))
{
    dst[0] = '\0';
    toml_datum_t d = toml_string_in(tab, key);
    if (!d.ok) return false;
    size_t n = strlen(d.u.s);
    bool fits = n < cap;
    if (fits) memcpy(dst, d.u.s, n + 1);
    memset(d.u.s, 0, n);
    if (xfree) xfree(d.u.s);
    return fits;
}

// Appends at the end (the file's order); repeated names and overflow dropped.
static void append_net(wifi_conf_t *conf, const wifi_conf_net_t *net)
{
    if (net->ssid[0] == '\0') return;
    if (wifi_conf_find(conf, net->ssid) >= 0) return;
    if (conf->count >= WIFI_CONF_MAX) return;
    conf->nets[conf->count++] = *net;
}

int wifi_conf_from_toml(const toml_table_t *root, wifi_conf_t *conf, void (*xfree)(void *))
{
    wifi_conf_init(conf);
    if (!root) return 0;

    wifi_conf_net_t net;
    const toml_table_t *wifi = toml_table_in(root, "wifi");
    if (wifi) {
        toml_datum_t en = toml_bool_in(wifi, "enable");
        conf->enable = en.ok && en.u.b;
        take_string(wifi, "hostname", conf->hostname, sizeof(conf->hostname), xfree);
        memset(&net, 0, sizeof(net));
        bool ok = take_string(wifi, "ssid", net.ssid, sizeof(net.ssid), xfree);
        bool pw_ok = true;
        if (toml_key_exists(wifi, "password")) {
            pw_ok = take_string(wifi, "password", net.password, sizeof(net.password), xfree);
        }
        toml_datum_t hid = toml_bool_in(wifi, "hidden");
        net.hidden = hid.ok && hid.u.b;
        if (ok && pw_ok) append_net(conf, &net);
    }

    const toml_array_t *arr = toml_array_in(root, "networks");
    int n = arr ? toml_array_nelem(arr) : 0;
    for (int i = 0; i < n; i++) {
        const toml_table_t *t = toml_table_at(arr, i);
        if (!t) continue;
        memset(&net, 0, sizeof(net));
        bool ok = take_string(t, "ssid", net.ssid, sizeof(net.ssid), xfree);
        bool pw_ok = true;
        if (toml_key_exists(t, "password")) {
            pw_ok = take_string(t, "password", net.password, sizeof(net.password), xfree);
        }
        toml_datum_t hid = toml_bool_in(t, "hidden");
        net.hidden = hid.ok && hid.u.b;
        if (ok && pw_ok) append_net(conf, &net);
    }
    memset(&net, 0, sizeof(net));
    return conf->count;
}

bool wifi_conf_add(wifi_conf_t *conf, const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > WIFI_CONF_SSID_MAX) return false;
    if (!password) password = "";
    if (strlen(password) > WIFI_CONF_PASS_MAX) return false;

    int at = wifi_conf_find(conf, ssid);
    if (at == 0 && strcmp(conf->nets[0].password, password) == 0) return false;

    wifi_conf_net_t net;
    memset(&net, 0, sizeof(net));
    strcpy(net.ssid, ssid);
    strcpy(net.password, password);
    if (at >= 0) {
        net.hidden = conf->nets[at].hidden;
        // Close the gap it leaves.
        for (int i = at; i + 1 < conf->count; i++) conf->nets[i] = conf->nets[i + 1];
        conf->count--;
    } else if (conf->count >= WIFI_CONF_MAX) {
        conf->count = WIFI_CONF_MAX - 1;  // the oldest (last) goes
    }
    for (int i = conf->count; i > 0; i--) conf->nets[i] = conf->nets[i - 1];
    conf->nets[0] = net;
    conf->count++;
    for (int i = conf->count; i < WIFI_CONF_MAX; i++) memset(&conf->nets[i], 0, sizeof(conf->nets[i]));
    memset(&net, 0, sizeof(net));
    return true;
}

bool wifi_conf_forget(wifi_conf_t *conf, const char *ssid)
{
    int at = wifi_conf_find(conf, ssid);
    if (at < 0) return false;
    for (int i = at; i + 1 < conf->count; i++) conf->nets[i] = conf->nets[i + 1];
    conf->count--;
    memset(&conf->nets[conf->count], 0, sizeof(conf->nets[conf->count]));
    return true;
}

// Bounded appends: o keeps counting past cap so the caller learns the size.
static size_t put_raw(char *out, size_t cap, size_t o, const char *s)
{
    for (; *s; s++, o++) {
        if (o + 1 < cap) out[o] = *s;
    }
    if (cap) out[o + 1 < cap ? o : cap - 1] = '\0';
    return o;
}

// A TOML basic string: quotes, backslashes and control bytes escaped; the
// rest (UTF-8 included) as it is.
static size_t put_str(char *out, size_t cap, size_t o, const char *s)
{
    o = put_raw(out, cap, o, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[8];
        if (*p == '"') {
            strcpy(esc, "\\\"");
        } else if (*p == '\\') {
            strcpy(esc, "\\\\");
        } else if (*p < 0x20 || *p == 0x7F) {
            snprintf(esc, sizeof(esc), "\\u%04X", (unsigned)*p);
        } else {
            esc[0] = (char)*p;
            esc[1] = '\0';
        }
        o = put_raw(out, cap, o, esc);
    }
    return put_raw(out, cap, o, "\"");
}

size_t wifi_conf_to_text(const wifi_conf_t *conf, char *out, size_t cap)
{
    size_t o = 0;
    if (cap) out[0] = '\0';
    o = put_raw(out, cap, o,
                "# Wi-Fi settings (/usr/share/doc/wifi.md). The Wi-Fi page writes this\n"
                "# file; up to 5 networks, newest first.\n"
                "[wifi]\n");
    o = put_raw(out, cap, o, conf->enable ? "enable = true\n" : "enable = false\n");
    if (conf->hostname[0]) {
        o = put_raw(out, cap, o, "hostname = ");
        o = put_str(out, cap, o, conf->hostname);
        o = put_raw(out, cap, o, "\n");
    }
    for (int i = 0; i < conf->count; i++) {
        const wifi_conf_net_t *n = &conf->nets[i];
        o = put_raw(out, cap, o, "\n[[networks]]\nssid = ");
        o = put_str(out, cap, o, n->ssid);
        o = put_raw(out, cap, o, "\npassword = ");
        o = put_str(out, cap, o, n->password);
        o = put_raw(out, cap, o, "\n");
        if (n->hidden) o = put_raw(out, cap, o, "hidden = true\n");
    }
    return o;
}
