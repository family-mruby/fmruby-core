// The per-app working directory of the file HAL (doc/api_fixes F7).
//
// One fixed-size string per app slot, empty for "/". Only the app in a slot
// writes its string (chdir) and reads it (every relative path it resolves);
// the app layer clears it when the app ends, after the app has stopped
// running, so no lock is needed. The strings sit in PSRAM on the devices.
//
// The HAL does not know what an app is: the app layer installs a function
// that names the calling task's slot (fmrb_hal_file_set_cwd_slot).

#include "fmrb_hal_file.h"
#include "fmrb_hal_file_cwd.h"
#include "fmrb_attr.h"
#include "fmrb_limits.h"
#include <string.h>

FMRB_EXT_RAM_BSS_ATTR static char s_cwd[FMRB_MAX_APPS][FMRB_HAL_FILE_CWD_MAX];
FMRB_EXT_RAM_BSS_ATTR static fmrb_hal_file_cwd_slot_fn_t s_slot_fn;

void fmrb_hal_file_set_cwd_slot(fmrb_hal_file_cwd_slot_fn_t fn) {
    s_slot_fn = fn;
}

static int current_slot(void) {
    if (s_slot_fn == NULL) {
        return -1;
    }
    int slot = s_slot_fn();
    return (slot >= 0 && slot < FMRB_MAX_APPS) ? slot : -1;
}

void fmrb_hal_file_cwd_forget(int slot) {
    if (slot >= 0 && slot < FMRB_MAX_APPS) {
        s_cwd[slot][0] = '\0';
    }
}

// Append the names of `in` to the folded absolute path out[0..*po).
// "." is dropped, ".." drops the last name (never the root).
static bool push_names(const char *in, char *out, size_t out_len, size_t *po) {
    size_t o = *po;
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - seg);
        if (len == 1 && seg[0] == '.') {
            continue;
        }
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            while (o > 1 && out[o - 1] != '/') o--;   // drop the last name
            if (o > 1) o--;                           // and its slash
            continue;
        }
        if (o > 1) {
            if (o + 1 >= out_len) return false;
            out[o++] = '/';
        }
        if (o + len >= out_len) return false;
        memcpy(out + o, seg, len);
        o += len;
    }
    *po = o;
    return true;
}

bool fmrb_hal_file_normalize(const char *path, char *out, size_t out_len) {
    if (path == NULL || out == NULL || out_len < 2) {
        return false;
    }
    size_t o = 0;
    out[o++] = '/';
    if (path[0] != '/') {
        int slot = current_slot();
        if (slot >= 0 && !push_names(s_cwd[slot], out, out_len, &o)) {
            return false;
        }
    }
    if (!push_names(path, out, out_len, &o)) {
        return false;
    }
    out[o] = '\0';
    return true;
}

const char *fmrb_hal_file_cwd_apply(const char *path, char *buf, size_t len) {
    if (path == NULL || path[0] == '/') {
        return path;
    }
    return fmrb_hal_file_normalize(path, buf, len) ? buf : NULL;
}

fmrb_err_t fmrb_hal_file_chdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }
    int slot = current_slot();
    if (slot < 0) {
        return FMRB_ERR_NOT_SUPPORTED;
    }
    if (path[0] == '\0') {
        return FMRB_ERR_NOT_FOUND;
    }
    char dir[FMRB_HAL_FILE_CWD_MAX];
    if (!fmrb_hal_file_normalize(path, dir, sizeof(dir))) {
        return FMRB_ERR_INVALID_PARAM;
    }
    // A virtual mount-point directory (/mnt) counts, as it does for Dir.open,
    // whether or not the platform's stat reports it.
    if (fmrb_hal_file_virtual_children(dir, NULL) == NULL) {
        fmrb_file_info_t info;
        if (fmrb_hal_file_stat(dir, &info) != FMRB_OK) {
            return FMRB_ERR_NOT_FOUND;
        }
        if (!FMRB_S_ISDIR(info.mode) && !info.is_dir) {
            return FMRB_ERR_INVALID_STATE;
        }
    }
    if (strcmp(dir, "/") == 0) {
        s_cwd[slot][0] = '\0';
    } else {
        memcpy(s_cwd[slot], dir, strlen(dir) + 1);
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_file_getcwd(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    int slot = current_slot();
    const char *cwd = (slot >= 0 && s_cwd[slot][0] != '\0') ? s_cwd[slot] : "/";
    size_t n = strlen(cwd);
    if (n + 1 > size) {
        return FMRB_ERR_INVALID_PARAM;
    }
    memcpy(buffer, cwd, n + 1);
    return FMRB_OK;
}
