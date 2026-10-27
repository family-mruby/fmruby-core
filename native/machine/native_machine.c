/*
 * native_machine.c - see native_machine.h.
 *
 * Runs on the native program's main thread before the machine starts (and
 * in the host tests), with tomlc99's default allocator: fmrb_toml_init()
 * points tomlc99 at the fmrb pools only later, on the machine's side, and
 * nothing parsed here outlives this file.
 */

#include "native_machine.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toml.h"

static int fail(char *err, size_t n, const char *fmt, ...)
{
    if (err != NULL && n > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int copy_str(char *dst, size_t cap, const char *value, const char *key,
                    char *err, size_t n)
{
    size_t len = strlen(value);
    if (len >= cap) return fail(err, n, "%s: value too long (%zu bytes, at most %zu)", key, len, cap - 1);
    memcpy(dst, value, len + 1);
    return 0;
}

static int parse_int(const char *value, int lo, int hi, int *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

static int parse_form(const char *value, int *w, int *h)
{
    int a = 0, b = 0;
    char tail = 0;
    if (sscanf(value, "%dx%d%c", &a, &b, &tail) != 2 || a <= 0 || b <= 0) return -1;
    *w = a;
    *h = b;
    return 0;
}

void native_machine_defaults(native_machine_t *m)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "built-in");
    snprintf(m->description, sizeof(m->description), "development: an SDL window (the built-in default)");
    snprintf(m->screen.form, sizeof(m->screen.form), "426x240");
    m->screen.w = 426;
    m->screen.h = 240;
    snprintf(m->display.backend, sizeof(m->display.backend), "sdl");
    snprintf(m->display.device, sizeof(m->display.device), "auto");
    snprintf(m->display.mode, sizeof(m->display.mode), "auto");
    m->display.rotate = 0;
    m->display.scale = 0;
    snprintf(m->input.backend, sizeof(m->input.backend), "sdl");
    snprintf(m->input.devices[0], sizeof(m->input.devices[0]), "auto");
    m->input.n_devices = 1;
    snprintf(m->audio.backend, sizeof(m->audio.backend), "sdl");
    snprintf(m->audio.device, sizeof(m->audio.device), "default");
    m->audio.latency_ms = 100;
    snprintf(m->audio.mixer, sizeof(m->audio.mixer), "auto");
    snprintf(m->desktop.variant, sizeof(m->desktop.variant), "mouse");
    snprintf(m->system.services, sizeof(m->system.services), "linux");
    snprintf(m->system.exit_key, sizeof(m->system.exit_key), "none");
    snprintf(m->system.power, sizeof(m->system.power), "exit");
    m->system.power_log_only = false;
    snprintf(m->files.host, sizeof(m->files.host), "auto");
    snprintf(m->files.root, sizeof(m->files.root), "auto");
    m->compositor.autostart[0] = '\0';
}

/* system.power: "exit", "logind", or both, comma-separated (a TOML list
 * arrives that way), each once. */
static int set_power(native_machine_t *m, const char *value, char *err, size_t n)
{
    char out[sizeof(m->system.power)];
    size_t used = 0;
    int count = 0;
    const char *p = value;
    out[0] = '\0';
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        while (len > 0 && *p == ' ') { p++; len--; }
        while (len > 0 && p[len - 1] == ' ') len--;
        bool is_exit = len == 4 && strncmp(p, "exit", 4) == 0;
        bool is_logind = len == 6 && strncmp(p, "logind", 6) == 0;
        if (!is_exit && !is_logind) {
            return fail(err, n, "system.power: \"%s\" is not exit, logind or a list of them", value);
        }
        if (strstr(out, is_exit ? "exit" : "logind") != NULL) {
            return fail(err, n, "system.power: \"%s\" names one twice", value);
        }
        int w = snprintf(out + used, sizeof(out) - used, "%s%.*s", count ? "," : "", (int)len, p);
        if (w < 0 || (size_t)w >= sizeof(out) - used) return fail(err, n, "system.power: too long");
        used += (size_t)w;
        count++;
        if (comma == NULL) break;
        p = comma + 1;
    }
    memcpy(m->system.power, out, used + 1);
    return 0;
}

bool native_machine_power_has(const native_machine_t *m, const char *group)
{
    size_t len = strlen(group);
    const char *p = m->system.power;
    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t item = comma ? (size_t)(comma - p) : strlen(p);
        if (item == len && strncmp(p, group, len) == 0) return true;
        if (comma == NULL) break;
        p = comma + 1;
    }
    return false;
}

/* files.host / files.root: "auto", "none" or an absolute directory. */
static int set_files_place(char *dst, size_t cap, const char *value, const char *key,
                           char *err, size_t n)
{
    if (strcmp(value, "auto") != 0 && strcmp(value, "none") != 0 && value[0] != '/') {
        return fail(err, n, "%s: \"%s\" is not auto, none or an absolute directory", key, value);
    }
    return copy_str(dst, cap, value, key, err, n);
}

static int set_devices(native_machine_t *m, const char *value, char *err, size_t n)
{
    int count = 0;
    const char *p = value;
    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        while (len > 0 && (*p == ' ')) { p++; len--; }
        while (len > 0 && p[len - 1] == ' ') len--;
        if (len > 0) {
            if (count >= NATIVE_MACHINE_MAX_DEVICES) {
                return fail(err, n, "input.devices: more than %d devices", NATIVE_MACHINE_MAX_DEVICES);
            }
            if (len >= NATIVE_MACHINE_STR) return fail(err, n, "input.devices: a path is too long");
            memcpy(m->input.devices[count], p, len);
            m->input.devices[count][len] = '\0';
            count++;
        }
        if (comma == NULL) break;
        p = comma + 1;
    }
    if (count == 0) return fail(err, n, "input.devices: empty (use [\"auto\"])");
    m->input.n_devices = count;
    return 0;
}

int native_machine_set(native_machine_t *m, const char *key, const char *value,
                       char *err, size_t n)
{
    if (strcmp(key, "name") == 0) return copy_str(m->name, sizeof(m->name), value, key, err, n);
    if (strcmp(key, "description") == 0) return copy_str(m->description, sizeof(m->description), value, key, err, n);

    if (strcmp(key, "screen.form") == 0 && strcmp(value, "output") == 0) {
        m->screen.w = 0;
        m->screen.h = 0;
        return copy_str(m->screen.form, sizeof(m->screen.form), value, key, err, n);
    }
    if (strcmp(key, "screen.form") == 0) {
        int w = 0, h = 0;
        if (parse_form(value, &w, &h) != 0) return fail(err, n, "screen.form: \"%s\" is not WxH", value);
        m->screen.w = w;
        m->screen.h = h;
        return copy_str(m->screen.form, sizeof(m->screen.form), value, key, err, n);
    }

    if (strcmp(key, "display.backend") == 0) return copy_str(m->display.backend, sizeof(m->display.backend), value, key, err, n);
    if (strcmp(key, "display.device") == 0) return copy_str(m->display.device, sizeof(m->display.device), value, key, err, n);
    if (strcmp(key, "display.mode") == 0) {
        int w = 0, h = 0, hz = 0;
        char tail = 0;
        if (strcmp(value, "auto") != 0 &&
            sscanf(value, "%dx%d%c", &w, &h, &tail) != 2 &&
            !(sscanf(value, "%dx%d@%d%c", &w, &h, &hz, &tail) == 3 && hz > 0)) {
            return fail(err, n, "display.mode: \"%s\" is not auto, WxH or WxH@Hz", value);
        }
        return copy_str(m->display.mode, sizeof(m->display.mode), value, key, err, n);
    }
    if (strcmp(key, "display.rotate") == 0) {
        int r = 0;
        if (parse_int(value, 0, 270, &r) != 0 || (r != 0 && r != 90 && r != 180 && r != 270)) {
            return fail(err, n, "display.rotate: \"%s\" is not 0, 90, 180 or 270", value);
        }
        m->display.rotate = r;
        return 0;
    }
    if (strcmp(key, "display.scale") == 0) {
        int s = 0;
        if (strcmp(value, "auto") == 0) {
            m->display.scale = 0;
            return 0;
        }
        if (parse_int(value, 1, 16, &s) != 0) return fail(err, n, "display.scale: \"%s\" is not auto or 1-16", value);
        m->display.scale = s;
        return 0;
    }

    if (strcmp(key, "display.fullscreen") == 0) {
        if (strcmp(value, "true") == 0) m->display.fullscreen = true;
        else if (strcmp(value, "false") == 0) m->display.fullscreen = false;
        else return fail(err, n, "display.fullscreen: \"%s\" is not true or false", value);
        return 0;
    }

    if (strcmp(key, "input.backend") == 0) return copy_str(m->input.backend, sizeof(m->input.backend), value, key, err, n);
    if (strcmp(key, "input.devices") == 0) return set_devices(m, value, err, n);
    if (strcmp(key, "input.keymap") == 0) return copy_str(m->input.keymap, sizeof(m->input.keymap), value, key, err, n);
    if (strcmp(key, "input.keymap_device") == 0) return copy_str(m->input.keymap_device, sizeof(m->input.keymap_device), value, key, err, n);

    if (strcmp(key, "audio.backend") == 0) return copy_str(m->audio.backend, sizeof(m->audio.backend), value, key, err, n);
    if (strcmp(key, "audio.device") == 0) return copy_str(m->audio.device, sizeof(m->audio.device), value, key, err, n);
    if (strcmp(key, "audio.latency_ms") == 0) {
        int v = 0;
        if (parse_int(value, 10, 2000, &v) != 0) return fail(err, n, "audio.latency_ms: \"%s\" is not 10-2000", value);
        m->audio.latency_ms = v;
        return 0;
    }
    if (strcmp(key, "audio.mixer") == 0) {
        if (value[0] == '\0') return fail(err, n, "audio.mixer: empty (use \"auto\" or \"none\")");
        return copy_str(m->audio.mixer, sizeof(m->audio.mixer), value, key, err, n);
    }

    if (strcmp(key, "desktop.variant") == 0) return copy_str(m->desktop.variant, sizeof(m->desktop.variant), value, key, err, n);

    if (strcmp(key, "system.services") == 0) {
        if (strcmp(value, "none") != 0 && strcmp(value, "linux") != 0) {
            return fail(err, n, "system.services: \"%s\" is not none or linux", value);
        }
        return copy_str(m->system.services, sizeof(m->system.services), value, key, err, n);
    }
    if (strcmp(key, "system.exit_key") == 0) {
        if (strcmp(value, "none") != 0 && strcmp(value, "esc_long") != 0) {
            return fail(err, n, "system.exit_key: \"%s\" is not none or esc_long", value);
        }
        return copy_str(m->system.exit_key, sizeof(m->system.exit_key), value, key, err, n);
    }
    if (strcmp(key, "system.power") == 0) return set_power(m, value, err, n);
    if (strcmp(key, "system.power_log_only") == 0) {
        if (strcmp(value, "true") == 0) m->system.power_log_only = true;
        else if (strcmp(value, "false") == 0) m->system.power_log_only = false;
        else return fail(err, n, "system.power_log_only: \"%s\" is not true or false", value);
        return 0;
    }

    if (strcmp(key, "files.host") == 0) {
        return set_files_place(m->files.host, sizeof(m->files.host), value, key, err, n);
    }
    if (strcmp(key, "files.root") == 0) {
        return set_files_place(m->files.root, sizeof(m->files.root), value, key, err, n);
    }

    if (strcmp(key, "compositor.autostart") == 0) {
        return copy_str(m->compositor.autostart, sizeof(m->compositor.autostart), value, key, err, n);
    }

    return fail(err, n, "unknown key \"%s\"", key);
}

/* One TOML value as the text native_machine_set takes. */
static int value_text(const toml_table_t *tab, const char *key, char *out, size_t cap,
                      char *err, size_t n, const char *where)
{
    toml_array_t *arr = toml_array_in(tab, key);
    if (arr != NULL) {
        size_t used = 0;
        out[0] = '\0';
        int count = toml_array_nelem(arr);
        for (int i = 0; i < count; i++) {
            toml_datum_t d = toml_string_at(arr, i);
            if (!d.ok) return fail(err, n, "%s: the list must hold strings", where);
            int w = snprintf(out + used, cap - used, "%s%s", i ? "," : "", d.u.s);
            free(d.u.s);
            if (w < 0 || (size_t)w >= cap - used) return fail(err, n, "%s: list too long", where);
            used += (size_t)w;
        }
        return 0;
    }
    toml_datum_t d = toml_string_in(tab, key);
    if (d.ok) {
        int w = snprintf(out, cap, "%s", d.u.s);
        free(d.u.s);
        if (w < 0 || (size_t)w >= cap) return fail(err, n, "%s: value too long", where);
        return 0;
    }
    d = toml_int_in(tab, key);
    if (d.ok) {
        snprintf(out, cap, "%lld", (long long)d.u.i);
        return 0;
    }
    d = toml_bool_in(tab, key);
    if (d.ok) {
        snprintf(out, cap, "%s", d.u.b ? "true" : "false");
        return 0;
    }
    return fail(err, n, "%s: not a string, number, boolean or list of strings", where);
}

static int load_table(native_machine_t *m, const toml_table_t *tab, const char *section,
                      char *err, size_t n)
{
    for (int i = 0;; i++) {
        const char *key = toml_key_in(tab, i);
        if (key == NULL) break;
        char dotted[96];
        if (section[0] != '\0') snprintf(dotted, sizeof(dotted), "%s.%s", section, key);
        else snprintf(dotted, sizeof(dotted), "%s", key);

        toml_table_t *sub = toml_table_in(tab, key);
        if (sub != NULL) {
            if (section[0] != '\0') return fail(err, n, "[%s]: nested tables are not used", dotted);
            static const char *const known[] = { "screen", "display", "input", "audio", "desktop", "system", "files",
                                                  "compositor", NULL };
            bool ok = false;
            for (int k = 0; known[k] != NULL; k++) ok |= (strcmp(key, known[k]) == 0);
            if (!ok) return fail(err, n, "unknown section [%s]", key);
            if (load_table(m, sub, key, err, n) != 0) return -1;
            continue;
        }
        char text[NATIVE_MACHINE_STR * NATIVE_MACHINE_MAX_DEVICES];
        if (value_text(tab, key, text, sizeof(text), err, n, dotted) != 0) return -1;
        if (native_machine_set(m, dotted, text, err, n) != 0) return -1;
    }
    return 0;
}

int native_machine_load_file(native_machine_t *m, const char *path, char *err, size_t n)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return fail(err, n, "%s: %s", path, strerror(errno));
    char perr[200];
    toml_table_t *root = toml_parse_file(f, perr, sizeof(perr));
    fclose(f);
    if (root == NULL) return fail(err, n, "%s: %s", path, perr);

    /* The name is the file's, unless the file says otherwise. */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(m->name, sizeof(m->name), "%s", base);
    char *dot = strstr(m->name, ".toml");
    if (dot != NULL && dot[5] == '\0') *dot = '\0';
    snprintf(m->source, sizeof(m->source), "%s", path);

    char inner[200];
    int rc = load_table(m, root, "", inner, sizeof(inner));
    toml_free(root);
    if (rc != 0) return fail(err, n, "%s: %s", path, inner);
    return 0;
}

void native_machine_resolve(const char *arg, const char *machines_dir, char *out, size_t out_len)
{
    size_t len = strlen(arg);
    bool is_path = strchr(arg, '/') != NULL || (len > 5 && strcmp(arg + len - 5, ".toml") == 0);
    if (is_path) snprintf(out, out_len, "%s", arg);
    else snprintf(out, out_len, "%s/%s.toml", machines_dir, arg);
}

int native_machine_check(const native_machine_t *m, char *err, size_t n)
{
    /* The screens this program has: Modern's 426x240 (its fullscreen
     * high-resolution mode stays with it), the Cardputer's 320x170, and the
     * compositor's output over its scale (L9d). */
    if (native_machine_screen_from_output(m)) {
        if (strcmp(m->display.backend, "wlroots") != 0) {
            return fail(err, n, "screen.form output needs display.backend wlroots (it is %s)", m->display.backend);
        }
        if (strcmp(m->desktop.variant, "mouse") != 0) {
            return fail(err, n, "screen.form output needs desktop.variant mouse");
        }
    } else if (!(m->screen.w == 426 && m->screen.h == 240) &&
               !(m->screen.w == 320 && m->screen.h == 170)) {
        return fail(err, n, "screen.form %s is not a screen this program has (426x240, 320x170, output)",
                    m->screen.form);
    }
    if (strcmp(m->desktop.variant, "mouse") != 0 && strcmp(m->desktop.variant, "keys") != 0) {
        return fail(err, n, "desktop.variant \"%s\" is not mouse or keys", m->desktop.variant);
    }
    if (m->input.keymap[0] != '\0' && strcmp(m->input.keymap, "cardputer_zero") != 0) {
        return fail(err, n, "input.keymap \"%s\" is not \"\" or cardputer_zero", m->input.keymap);
    }
    return 0;
}

bool native_machine_screen_from_output(const native_machine_t *m)
{
    return strcmp(m->screen.form, "output") == 0;
}

/* The smallest screen the desktop is laid out for (Modern's). */
#define OUTPUT_SCREEN_MIN_W 426
#define OUTPUT_SCREEN_MIN_H 240
/* The largest: what the program's frame copy holds (main_native.c's
 * FRAME_CAP_PX); a 4K output at scale 1 gets this, laid on it at 2x. */
#define OUTPUT_SCREEN_MAX_W 1920
#define OUTPUT_SCREEN_MAX_H 1080

void native_machine_output_screen(const native_machine_t *m, int out_w, int out_h, int *w, int *h,
                                  char *why, size_t why_len)
{
    int s = m->display.scale > 0 ? m->display.scale : 2;
    *w = OUTPUT_SCREEN_MIN_W;
    *h = OUTPUT_SCREEN_MIN_H;
    if (out_w <= 0 || out_h <= 0) {
        if (why) snprintf(why, why_len, "the output's size is not known");
        return;
    }
    if (out_h > out_w) {
        if (why) snprintf(why, why_len, "a portrait output (%dx%d): the %dx%d screen", out_w, out_h, *w, *h);
        return;
    }
    int sw = out_w / s, sh = out_h / s;
    if (sw < OUTPUT_SCREEN_MIN_W || sh < OUTPUT_SCREEN_MIN_H) {
        if (why) {
            snprintf(why, why_len, "output %dx%d over %d is %dx%d, under %dx%d: the %dx%d screen", out_w, out_h, s,
                     sw, sh, OUTPUT_SCREEN_MIN_W, OUTPUT_SCREEN_MIN_H, *w, *h);
        }
        return;
    }
    bool capped = sw > OUTPUT_SCREEN_MAX_W || sh > OUTPUT_SCREEN_MAX_H;
    if (sw > OUTPUT_SCREEN_MAX_W) sw = OUTPUT_SCREEN_MAX_W;
    if (sh > OUTPUT_SCREEN_MAX_H) sh = OUTPUT_SCREEN_MAX_H;
    *w = sw;
    *h = sh;
    if (why) {
        snprintf(why, why_len, "output %dx%d over %d%s", out_w, out_h, s,
                 capped ? ", kept within 1920x1080" : "");
    }
}

void native_machine_describe(const native_machine_t *m, char *out, size_t out_len)
{
    char scale[16];
    if (m->display.scale > 0) snprintf(scale, sizeof(scale), "%d", m->display.scale);
    else snprintf(scale, sizeof(scale), "auto");
    bool alsa = strcmp(m->audio.backend, "alsa") == 0;
    snprintf(out, out_len,
             "machine %s (%s): screen %s, display %s device=%s mode=%s rotate=%d scale=%s%s, "
             "input %s%s%s, audio %s%s%s, desktop %s, services %s, power %s%s, files host=%s root=%s%s%s",
             m->name, m->source[0] ? m->source : "built-in", m->screen.form,
             m->display.backend, m->display.device, m->display.mode, m->display.rotate, scale,
             m->display.fullscreen ? " fullscreen" : "",
             m->input.backend, m->input.keymap[0] ? " keymap=" : "", m->input.keymap,
             m->audio.backend, alsa ? " device=" : "", alsa ? m->audio.device : "",
             m->desktop.variant, m->system.services, m->system.power,
             m->system.power_log_only ? " (log only)" : "", m->files.host, m->files.root,
             m->compositor.autostart[0] ? ", autostart " : "", m->compositor.autostart);
}
