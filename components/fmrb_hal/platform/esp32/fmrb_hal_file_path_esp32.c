// The ESP32 file namespace: how a virtual path names a place on the VFS.
//
// Plain C over libc: it has no ESP-IDF dependency, so the host tests build it
// as it is (test/api_fixes/cwd) to hold the resolver's answers fixed.

#include "fmrb_hal_file.h"
#include "fmrb_hal_file_cwd.h"
#include "fmrb_hal_file_esp32_paths.h"
#include "fmrb_tmpfs.h"
#include <stdio.h>
#include <string.h>

// The bound the device's file paths are held to (the handle table's copy of a
// directory path, every resolved path buffer).
#define PATH_BUF_LEN 128

// Path-prefix alias table. Each entry maps a user-visible prefix to the
// underlying VFS mount point. An entry whose `mount` equals its `prefix` is
// the canonical name; other entries are aliases (the prefix is rewritten to
// `mount` before reaching the VFS). To add a new alias, add one row here -
// no other code needs to change.
typedef struct {
    const char *prefix;  // user-visible prefix, no trailing slash
    const char *mount;   // actual VFS mount point, no trailing slash
} path_alias_t;

static const path_alias_t s_path_aliases[] = {
    { "/flash",  LITTLEFS_PATH },     // canonical flash mount
    { "/sd",     SDCARD_PATH },       // canonical SD mount
    { "/mnt/sd", SDCARD_PATH },       // Unix-style alias for SD
    // RAM filesystem, registered as its own VFS (fmrb_tmpfs_esp32.c). Listed
    // here so it is left alone instead of being folded under the flash mount.
    { FMRB_TMPFS_MOUNT, FMRB_TMPFS_MOUNT },
};

#define PATH_ALIAS_COUNT (sizeof(s_path_aliases) / sizeof(s_path_aliases[0]))

// Virtual mount-point directories that the underlying VFS does not own. They
// only exist as "containers" so the real mounts (/mnt/sd) are reachable via a
// Unix-style namespace. opendir/readdir on these returns the synthetic child
// list; stat reports them as directories.
typedef struct {
    const char *path;            // virtual directory path, no trailing slash
    const char *const *children; // null-terminated? no - count is explicit
    size_t count;
} path_virtual_t;

static const char *const s_root_extra[]    = { "mnt", "tmp" };
static const char *const s_mnt_children[]  = { "sd" };

static const path_virtual_t s_virtual_dirs[] = {
    { "/",    s_root_extra,   2 },  // "mnt" and "tmp" appear in addition to flash entries
    { "/mnt", s_mnt_children, 1 },  // purely virtual: only contains "sd"
};

#define PATH_VIRTUAL_COUNT (sizeof(s_virtual_dirs) / sizeof(s_virtual_dirs[0]))

// Resolve a virtual path to the underlying VFS path.
// Order of resolution:
//   1. Match against the alias table (prefix must terminate at '/' or '\0').
//   2. Otherwise, treat as living under the flash mount.
// A relative path is first put under the calling app's working directory and
// folded (fmrb_hal_file_cwd_apply), so it is resolved as the absolute path it
// names; an absolute path is used exactly as given.
fmrb_err_t fmrb_hal_file_resolve_path(const char *path,
                                      char *out, size_t out_len) {
    if (path == NULL || out == NULL || out_len == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    char abs[PATH_BUF_LEN];
    const char *virtual_path = fmrb_hal_file_cwd_apply(path, abs, sizeof(abs));
    if (virtual_path == NULL) {
        out[0] = '\0';
        return FMRB_ERR_INVALID_PARAM;
    }
    for (size_t i = 0; i < PATH_ALIAS_COUNT; i++) {
        const char *prefix = s_path_aliases[i].prefix;
        size_t plen = strlen(prefix);
        if (strncmp(virtual_path, prefix, plen) == 0 &&
            (virtual_path[plen] == '\0' || virtual_path[plen] == '/')) {
            snprintf(out, out_len, "%s%s",
                     s_path_aliases[i].mount, virtual_path + plen);
            return FMRB_OK;
        }
    }
    snprintf(out, out_len, "%s%s", LITTLEFS_PATH, virtual_path);
    return FMRB_OK;
}

// Look up the synthetic children for a virtual mount-point directory.
// Returns NULL if `virtual_path` is not a known virtual directory.
// A relative path is taken from the working directory, like the resolver.
const char *const *fmrb_hal_file_virtual_children(const char *path,
                                                  size_t *out_count) {
    char abs[PATH_BUF_LEN];
    const char *virtual_path = fmrb_hal_file_cwd_apply(path, abs, sizeof(abs));
    if (virtual_path == NULL) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    for (size_t i = 0; i < PATH_VIRTUAL_COUNT; i++) {
        if (strcmp(virtual_path, s_virtual_dirs[i].path) == 0) {
            if (out_count) *out_count = s_virtual_dirs[i].count;
            return s_virtual_dirs[i].children;
        }
    }
    if (out_count) *out_count = 0;
    return NULL;
}

// The device's storage is one writable volume (fmrb_hal_file.h).
bool fmrb_hal_file_path_is_read_only(const char *virtual_path) {
    (void)virtual_path;
    return false;
}

bool fmrb_hal_file_entry_is_read_only(const char *virtual_path) {
    (void)virtual_path;
    return false;
}

