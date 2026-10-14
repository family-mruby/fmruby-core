// fmrb_secret_path.c - the rules for the board's secret files over the
// remote file services. See fmrb_secret_path.h.

#include "fmrb_secret_path.h"

#include <stddef.h>
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
        return FMRB_PATH_SECRET;
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

fmrb_remote_fs_verdict_t fmrb_remote_fs_check(fmrb_remote_fs_op_t op, const char *path,
                                              const char *to, bool window_open)
{
    fmrb_path_class_t cls = fmrb_path_classify(path);
    switch (op) {
    case FMRB_REMOTE_FS_GET:
        return cls == FMRB_PATH_SECRET ? FMRB_REMOTE_FS_DENY_SECRET : FMRB_REMOTE_FS_ALLOW;

    case FMRB_REMOTE_FS_PUT:
    case FMRB_REMOTE_FS_RM:
        if (cls == FMRB_PATH_PLAIN) return FMRB_REMOTE_FS_ALLOW;
        return window_open ? FMRB_REMOTE_FS_ALLOW : FMRB_REMOTE_FS_DENY_CLOSED;

    case FMRB_REMOTE_FS_RENAME: {
        fmrb_path_class_t to_cls = fmrb_path_classify(to);
        // Moving a secret (or a directory holding one) to a name that is not
        // a secret would make it readable: never, window or not.
        bool from_hides = (cls == FMRB_PATH_SECRET) || fmrb_path_contains_secret(path);
        if (from_hides && to_cls != FMRB_PATH_SECRET) return FMRB_REMOTE_FS_DENY_SECRET;
        if (cls == FMRB_PATH_PLAIN && to_cls == FMRB_PATH_PLAIN) return FMRB_REMOTE_FS_ALLOW;
        return window_open ? FMRB_REMOTE_FS_ALLOW : FMRB_REMOTE_FS_DENY_CLOSED;
    }
    }
    return FMRB_REMOTE_FS_DENY_SECRET;
}
