// fmrb_secret_path.c - the rules for the board's secret files over the
// remote file services. See fmrb_secret_path.h.

#include "fmrb_secret_path.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Deep enough for every rule below (they look at three segments at most);
// a longer path only needs to be told apart from the short ones.
#define SECRET_PATH_SEGS 4

typedef struct {
    const char *seg[SECRET_PATH_SEGS];
    size_t len[SECRET_PATH_SEGS];
    size_t count;     // segments seen, may exceed SECRET_PATH_SEGS
    bool dotdot;
} split_path_t;

static void split_path(const char *path, split_path_t *out)
{
    memset(out, 0, sizeof(*out));
    if (path == NULL) return;
    const char *p = path;
    bool first = true;
    while (*p) {
        while (*p == '/') p++;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - seg);
        if (len == 0 || (len == 1 && seg[0] == '.')) continue;
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            out->dotdot = true;
            continue;
        }
        // "/flash/..." is the root under another name (the HAL's alias).
        if (first && len == 5 && strncmp(seg, "flash", 5) == 0) {
            first = false;
            continue;
        }
        first = false;
        if (out->count < SECRET_PATH_SEGS) {
            out->seg[out->count] = seg;
            out->len[out->count] = len;
        }
        out->count++;
    }
}

static bool seg_is(const split_path_t *sp, size_t i, const char *name)
{
    size_t n = strlen(name);
    return i < sp->count && i < SECRET_PATH_SEGS && sp->len[i] == n &&
           strncmp(sp->seg[i], name, n) == 0;
}

// "wifi.toml" and anything named after it ("wifi.toml.part", the upload
// devctl writes before renaming it into place).
static bool seg_is_wifi(const split_path_t *sp, size_t i)
{
    static const char wifi[] = "wifi.toml";
    size_t n = sizeof(wifi) - 1;
    return i < sp->count && i < SECRET_PATH_SEGS && sp->len[i] >= n &&
           strncmp(sp->seg[i], wifi, n) == 0;
}

fmrb_path_class_t fmrb_path_classify(const char *path)
{
    split_path_t sp;
    split_path(path, &sp);
    if (sp.dotdot) return FMRB_PATH_SECRET;

    if (seg_is(&sp, 0, "home") && seg_is(&sp, 1, "asterism")) {
        if (sp.count == 2) return FMRB_PATH_SETTINGS;
        if (sp.count == 3 && seg_is(&sp, 2, "config.toml")) return FMRB_PATH_SETTINGS;
        return FMRB_PATH_SECRET;
    }
    if (sp.count == 2 && (seg_is(&sp, 0, "home") || seg_is(&sp, 0, "etc")) &&
        seg_is_wifi(&sp, 1)) {
        return FMRB_PATH_WIFI;
    }
    return FMRB_PATH_PLAIN;
}

bool fmrb_path_contains_secret(const char *path)
{
    split_path_t sp;
    split_path(path, &sp);
    if (sp.dotdot) return true;
    if (sp.count == 0) return true;
    if (sp.count == 1) return seg_is(&sp, 0, "home") || seg_is(&sp, 0, "etc");
    if (sp.count == 2) return seg_is(&sp, 0, "home") && seg_is(&sp, 1, "asterism");
    return false;
}

// How strongly a path is kept from being read: a name may only be moved to
// one kept at least as strongly, or a rename would make it readable.
static int hide_level(fmrb_path_class_t cls)
{
    return cls == FMRB_PATH_SECRET ? 2 : (cls == FMRB_PATH_WIFI ? 1 : 0);
}

fmrb_remote_fs_verdict_t fmrb_remote_fs_check(fmrb_remote_fs_op_t op, const char *path,
                                              const char *to, bool window_open)
{
    fmrb_path_class_t cls = fmrb_path_classify(path);
    switch (op) {
    case FMRB_REMOTE_FS_GET:
        if (cls == FMRB_PATH_SECRET) return FMRB_REMOTE_FS_DENY_SECRET;
        if (cls == FMRB_PATH_WIFI && !window_open) return FMRB_REMOTE_FS_DENY_CLOSED;
        return FMRB_REMOTE_FS_ALLOW;

    case FMRB_REMOTE_FS_PUT:
    case FMRB_REMOTE_FS_RM:
        if (cls == FMRB_PATH_PLAIN) return FMRB_REMOTE_FS_ALLOW;
        return window_open ? FMRB_REMOTE_FS_ALLOW : FMRB_REMOTE_FS_DENY_CLOSED;

    case FMRB_REMOTE_FS_RENAME: {
        fmrb_path_class_t to_cls = fmrb_path_classify(to);
        // Moving a secret (or a directory holding one) to a name kept less
        // strongly would make it readable: never, window or not.
        int from_level = fmrb_path_contains_secret(path) ? 2 : hide_level(cls);
        if (hide_level(to_cls) < from_level) return FMRB_REMOTE_FS_DENY_SECRET;
        if (cls == FMRB_PATH_PLAIN && to_cls == FMRB_PATH_PLAIN) return FMRB_REMOTE_FS_ALLOW;
        return window_open ? FMRB_REMOTE_FS_ALLOW : FMRB_REMOTE_FS_DENY_CLOSED;
    }
    }
    return FMRB_REMOTE_FS_DENY_SECRET;
}

// ---- the WiFi summary --------------------------------------------------------

// The value of `key = ...` on one line ([start, end)), or NULL. Only bare
// keys at the start of a line count, so "#ssid", "ssid2" and the password
// line never match "ssid".
static const char *toml_line_value(const char *start, const char *end, const char *key)
{
    const char *p = start;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    size_t n = strlen(key);
    if ((size_t)(end - p) < n || strncmp(p, key, n) != 0) return NULL;
    p += n;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end || *p != '=') return NULL;
    p++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A TOML basic string at p (up to end) into out as UTF-8. False when it is
// not one (a literal string 'x' is taken as it is).
static bool toml_string_value(const char *p, const char *end, char *out, size_t cap)
{
    size_t o = 0;
    if (cap == 0) return false;
    if (p < end && *p == '\'') {
        for (p++; p < end && *p != '\''; p++) {
            if (o + 1 < cap) out[o++] = *p;
        }
        out[o] = '\0';
        return p < end;
    }
    if (p >= end || *p != '"') return false;
    for (p++; p < end; p++) {
        char c = *p;
        if (c == '"') { out[o] = '\0'; return true; }
        if (c != '\\') { if (o + 1 < cap) out[o++] = c; continue; }
        if (++p >= end) break;
        uint32_t cp = 0;
        int digits = 0;
        switch (*p) {
        case 'n': cp = '\n'; break;
        case 't': cp = '\t'; break;
        case 'r': cp = '\r'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'u': digits = 4; break;
        case 'U': digits = 8; break;
        default: cp = (unsigned char)*p; break;
        }
        for (int i = 0; i < digits; i++) {
            int h = (p + 1 < end) ? hex_digit(p[1]) : -1;
            if (h < 0) { out[o] = '\0'; return false; }
            cp = (cp << 4) | (uint32_t)h;
            p++;
        }
        char enc[4];
        size_t len;
        if (cp < 0x80) { enc[0] = (char)cp; len = 1; }
        else if (cp < 0x800) { enc[0] = (char)(0xC0 | (cp >> 6)); enc[1] = (char)(0x80 | (cp & 0x3F)); len = 2; }
        else if (cp < 0x10000) { enc[0] = (char)(0xE0 | (cp >> 12)); enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                 enc[2] = (char)(0x80 | (cp & 0x3F)); len = 3; }
        else { enc[0] = (char)(0xF0 | ((cp >> 18) & 0x07)); enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
               enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[3] = (char)(0x80 | (cp & 0x3F)); len = 4; }
        for (size_t i = 0; i < len; i++) {
            if (o + 1 < cap) out[o++] = enc[i];
        }
    }
    out[o] = '\0';
    return false;
}

// Append s as a JSON string. Returns the new offset (never past cap - 1).
static size_t json_put_str(char *out, size_t cap, size_t o, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    if (o + 1 < cap) out[o++] = '"';
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (o + 2 >= cap) break;
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            if (o + 6 >= cap) break;
            out[o++] = '\\'; out[o++] = 'u'; out[o++] = '0'; out[o++] = '0';
            out[o++] = hex[c >> 4]; out[o++] = hex[c & 0xF];
        } else {
            if (o + 1 >= cap) break;
            out[o++] = (char)c;
        }
    }
    if (o + 1 < cap) out[o++] = '"';
    out[o < cap ? o : cap - 1] = '\0';
    return o;
}

static size_t json_put_raw(char *out, size_t cap, size_t o, const char *s)
{
    for (; *s && o + 1 < cap; s++) out[o++] = *s;
    out[o] = '\0';
    return o;
}

size_t fmrb_wifi_summary_json(const char *toml_text, const char *src, bool connected,
                              const char *ip, char *out, size_t cap)
{
    char ssid[66] = "";   // 32 bytes is the limit; room to see a longer one cut
    bool on = false;
    if (toml_text) {
        const char *p = toml_text;
        bool have_ssid = false, have_on = false;
        while (*p) {
            const char *line = p;
            while (*p && *p != '\n') p++;
            const char *end = p;
            if (*p) p++;
            const char *v;
            if (!have_ssid && (v = toml_line_value(line, end, "ssid")) != NULL) {
                have_ssid = toml_string_value(v, end, ssid, sizeof(ssid));
                if (!have_ssid) ssid[0] = '\0';
            } else if (!have_on && (v = toml_line_value(line, end, "enable")) != NULL) {
                on = (end - v >= 4 && strncmp(v, "true", 4) == 0);
                have_on = true;
            }
        }
    }
    if (cap == 0) return 0;
    out[0] = '\0';
    size_t o = 0;
    o = json_put_raw(out, cap, o, "{\"src\":");
    o = json_put_str(out, cap, o, src ? src : "");
    o = json_put_raw(out, cap, o, on ? ",\"on\":true,\"ssid\":" : ",\"on\":false,\"ssid\":");
    o = json_put_str(out, cap, o, ssid);
    o = json_put_raw(out, cap, o, connected ? ",\"conn\":true,\"ip\":" : ",\"conn\":false,\"ip\":");
    o = json_put_str(out, cap, o, ip ? ip : "");
    o = json_put_raw(out, cap, o, "}");
    // The SSID copy held nothing secret, but leave no trace of the file.
    memset(ssid, 0, sizeof(ssid));
    return o;
}

fmrb_setup_reason_t fmrb_setup_mode_reason(bool enabled, bool have_wifi_settings,
                                           bool button_held)
{
    if (!enabled) return FMRB_SETUP_NONE;
    // The button first: it is the owner asking, whatever the files say.
    if (button_held) return FMRB_SETUP_BUTTON;
    if (!have_wifi_settings) return FMRB_SETUP_NO_WIFI;
    return FMRB_SETUP_NONE;
}

const char *fmrb_setup_reason_text(fmrb_setup_reason_t reason)
{
    switch (reason) {
    case FMRB_SETUP_NO_WIFI: return "no WiFi settings";
    case FMRB_SETUP_BUTTON:  return "button held at boot";
    default:                 return "normal boot";
    }
}
