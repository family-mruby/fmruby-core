/*
 * sys_linux_read.c - the battery and the network link from sysfs and procfs
 * (sys_linux_read.h). Plain file reads, so the host tests run it over a
 * made-up tree.
 */

#include "sys_linux_read.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fmrb_platform_sys.h"

/* The first line of a small file, without its newline. false when there is
 * no file (or it is empty). */
static bool read_line(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    bool ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (!ok) return false;
    out[strcspn(out, "\r\n")] = '\0';
    return out[0] != '\0';
}

static bool read_long(const char *path, long *out)
{
    char buf[64];
    if (!read_line(path, buf, sizeof(buf))) return false;
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf) return false;
    *out = v;
    return true;
}

/* dir/name/leaf into out. false when it does not fit (then not used). */
static bool join3(char *out, size_t cap, const char *dir, const char *name, const char *leaf)
{
    int n = snprintf(out, cap, "%s/%s/%s", dir, name, leaf);
    return n > 0 && (size_t)n < cap;
}

static void copy_name(char *out, size_t cap, const char *name)
{
    size_t len = strlen(name);
    if (len >= cap) len = cap - 1;
    memcpy(out, name, len);
    out[len] = '\0';
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int clamp_percent(long v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (int)v;
}

/* Directory entries in name order, so "the first" means the same on every
 * machine (readdir's order is the filesystem's). */
static int sorted_names(const char *dir, char names[][64], int max)
{
    DIR *d = opendir(dir);
    if (d == NULL) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.') continue;
        if (strlen(e->d_name) >= 64) continue;
        copy_name(names[n++], 64, e->d_name);
    }
    closedir(d);
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[64];
            memcpy(t, names[j], 64);
            memcpy(names[j], names[j - 1], 64);
            memcpy(names[j - 1], t, 64);
        }
    }
    return n;
}

#define MAX_ENTRIES 32

bool sys_linux_battery_read(const char *sysfs, sys_linux_battery_t *out)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/class/power_supply", sysfs);
    char names[MAX_ENTRIES][64];
    int n = sorted_names(dir, names, MAX_ENTRIES);
    for (int i = 0; i < n; i++) {
        char path[640], text[64];
        join3(path, sizeof(path), dir, names[i], "type");
        if (!read_line(path, text, sizeof(text)) || strcmp(text, "Battery") != 0) continue;
        long present = 1;
        join3(path, sizeof(path), dir, names[i], "present");
        if (read_long(path, &present) && present == 0) continue;

        long cap = -1, now = 0, full = 0;
        join3(path, sizeof(path), dir, names[i], "capacity");
        if (!read_long(path, &cap)) {
            static const char *const pairs[][2] = {
                { "charge_now", "charge_full" }, { "energy_now", "energy_full" },
            };
            for (int k = 0; k < 2 && cap < 0; k++) {
                char pn[640], pf[640];
                if (join3(pn, sizeof(pn), dir, names[i], pairs[k][0]) &&
                    join3(pf, sizeof(pf), dir, names[i], pairs[k][1]) &&
                    read_long(pn, &now) && read_long(pf, &full) && full > 0) {
                    cap = (now * 100 + full / 2) / full;
                }
            }
        }
        if (cap < 0) continue;

        memset(out, 0, sizeof(*out));
        out->percent = clamp_percent(cap);
        join3(path, sizeof(path), dir, names[i], "status");
        out->charging = read_line(path, text, sizeof(text)) && strcmp(text, "Charging") == 0;
        copy_name(out->name, sizeof(out->name), names[i]);
        return true;
    }
    return false;
}

int sys_linux_wireless_signal(const char *procfs, const char *iface)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/net/wireless", procfs);
    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;
    char line[256];
    int signal = -1;
    size_t ilen = strlen(iface);
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (*p == ' ') p++;
        if (strncmp(p, iface, ilen) != 0 || p[ilen] != ':') continue;
        /* "wlan0: 0000   54.  -56.  -256 ..." -- status, then link quality */
        unsigned status = 0;
        double link = 0;
        if (sscanf(p + ilen + 1, "%x %lf", &status, &link) == 2) {
            long pct = (long)(link * 100.0 / 70.0 + 0.5);
            signal = clamp_percent(pct);
        }
        break;
    }
    fclose(f);
    return signal;
}

/* The interface of the default route in /proc/net/route ("" when none). */
static void default_route_iface(const char *procfs, char *out, size_t cap)
{
    out[0] = '\0';
    char path[512];
    snprintf(path, sizeof(path), "%s/net/route", procfs);
    FILE *f = fopen(path, "r");
    if (f == NULL) return;
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        char iface[32];
        unsigned long dest = 1, flags = 0;
        unsigned long gw = 0;
        if (sscanf(line, "%31s %lx %lx %lx", iface, &dest, &gw, &flags) != 4) continue;
        /* Destination 0 and the route is up (RTF_UP = 1). */
        if (dest == 0 && (flags & 1u)) {
            copy_name(out, cap, iface);
            break;
        }
    }
    fclose(f);
}

bool sys_linux_link_read(const char *sysfs, const char *procfs, sys_linux_link_t *out)
{
    memset(out, 0, sizeof(*out));
    out->signal = -1;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/class/net", sysfs);
    char names[MAX_ENTRIES][64];
    int n = sorted_names(dir, names, MAX_ENTRIES);

    char route[32];
    default_route_iface(procfs, route, sizeof(route));

    const char *first_any = NULL;
    const char *first_wireless = NULL;
    const char *chosen = NULL;
    bool chosen_up = false;
    for (int i = 0; i < n; i++) {
        char path[640], text[32];
        long type = 0;
        join3(path, sizeof(path), dir, names[i], "type");
        if (read_long(path, &type) && type == 772) continue;   /* ARPHRD_LOOPBACK */
        if (strcmp(names[i], "lo") == 0) continue;
        join3(path, sizeof(path), dir, names[i], "wireless");
        bool wl = is_dir(path);
        if (first_any == NULL) first_any = names[i];
        if (wl && first_wireless == NULL) first_wireless = names[i];
        if (route[0] != '\0' && strcmp(route, names[i]) == 0) {
            join3(path, sizeof(path), dir, names[i], "operstate");
            chosen = names[i];
            chosen_up = read_line(path, text, sizeof(text)) &&
                        (strcmp(text, "up") == 0 || strcmp(text, "unknown") == 0);
        }
    }
    if (first_any == NULL) return false;
    if (chosen == NULL || !chosen_up) {
        chosen = first_wireless ? first_wireless : first_any;
        chosen_up = false;
    }
    copy_name(out->iface, sizeof(out->iface), chosen);
    char path[640];
    join3(path, sizeof(path), dir, chosen, "wireless");
    out->wireless = is_dir(path);
    out->connected = chosen_up;
    if (out->wireless && out->connected) out->signal = sys_linux_wireless_signal(procfs, chosen);
    return true;
}

int sys_linux_mixer_to_step(long value, long min, long max, int max_step)
{
    if (max <= min || max_step <= 0) return 0;
    if (value <= min) return 0;
    if (value >= max) return max_step;
    return (int)(((value - min) * max_step + (max - min) / 2) / (max - min));
}

long sys_linux_step_to_mixer(int step, long min, long max, int max_step)
{
    if (max_step <= 0 || max <= min) return min;
    if (step <= 0) return min;
    if (step >= max_step) return max;
    return min + ((max - min) * step + max_step / 2) / max_step;
}

const char *sys_linux_logind_method(unsigned action)
{
    switch (action) {
    case FMRB_SYS_POWER_OFF:     return "PowerOff";
    case FMRB_SYS_POWER_RESTART: return "Reboot";
    case FMRB_SYS_POWER_SUSPEND: return "Suspend";
    default:                     return NULL;
    }
}

/* The value of key in a KEY=VALUE file (shell or ini style; in an ini file
 * only inside [section], when one is named), quotes and blanks taken off.
 * false when the key is not there or is empty. */
static bool conf_value(const char *path, const char *section, const char *key, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    char line[512];
    bool in = section == NULL;
    bool got = false;
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        p[strcspn(p, "\r\n")] = '\0';
        if (*p == '[') {
            if (section != NULL) {
                size_t slen = strlen(section);
                in = strncmp(p + 1, section, slen) == 0 && p[1 + slen] == ']';
            }
            continue;
        }
        if (!in || strncmp(p, key, klen) != 0) continue;
        char *q = p + klen;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '=') continue;
        q++;
        while (*q == ' ' || *q == '\t' || *q == '"' || *q == '\'') q++;
        size_t n = strlen(q);
        while (n > 0 && (q[n - 1] == ' ' || q[n - 1] == '\t' || q[n - 1] == '"' || q[n - 1] == '\'')) n--;
        q[n] = '\0';
        snprintf(out, cap, "%s", q);
        got = out[0] != '\0';
    }
    fclose(f);
    return got;
}

/* The first of a comma-separated list, as jp or us. */
static const char *layout_of(char *list)
{
    list[strcspn(list, ", ")] = '\0';
    return strcmp(list, "jp") == 0 ? "jp" : "us";
}

const char *sys_linux_keyboard_layout(const char *kxkbrc, const char *keyboard, char *how, size_t how_len)
{
    char v[256];
    char use[16];
    if (kxkbrc != NULL && conf_value(kxkbrc, "Layout", "Use", use, sizeof(use)) && strcmp(use, "true") == 0 &&
        conf_value(kxkbrc, "Layout", "LayoutList", v, sizeof(v))) {
        if (how) snprintf(how, how_len, "LayoutList=%s in %s", v, kxkbrc);
        return layout_of(v);
    }
    if (keyboard != NULL && conf_value(keyboard, NULL, "XKBLAYOUT", v, sizeof(v))) {
        if (how) snprintf(how, how_len, "XKBLAYOUT=%s in %s", v, keyboard);
        return layout_of(v);
    }
    if (how) snprintf(how, how_len, "no layout set in %s or %s", kxkbrc ? kxkbrc : "-", keyboard ? keyboard : "-");
    return "us";
}
