/*
 * The app boundary's FmrbFile flat functions (doc/app_boundary W4b-3): the C
 * bodies behind fmrb_gen/fmrb_boundary.h, generated from sig/fmrb_file.rbs.
 * Every engine's glue is generated and calls these.
 *
 * Whole files by path, with no state kept between calls (a C app or a Lua
 * app reads a file with one call, as Ruby's File.read does; reading by
 * position stays Ruby's File). They go through the file HAL, so an app
 * reaches the same places as Ruby's File does, and the HAL decides what can
 * be written. A path comes as a pointer and a length: it is copied to end
 * with a NUL before the HAL sees it.
 *
 * A read gives its bytes into the caller's buffer and returns the whole
 * length (the glue asks again with that length): the file is read again from
 * its start, so a second call gives the same answer. FMRB_BOUNDARY_NONE is
 * nil in Ruby: no file there, no size known.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_hal.h"
#include "fmrb_tmpfs.h"

#define ERR_FAILED FMRB_BOUNDARY_ERR_FAILED
#define ERR_ARG    FMRB_BOUNDARY_ERR_ARG
#define NONE       FMRB_BOUNDARY_NONE

#if defined(FMRB_PLATFORM_WASM)
// The browser's storage room as the page last saw it (wasm/backend/storage_wasm.c).
int64_t fmrb_wasm_storage_free(void);
#endif

// The path with a NUL; false when it does not fit (an argument error).
static bool path_copy(char *dst, const char *path, int32_t len)
{
    if (!path || len <= 0 || len >= FMRB_MAX_PATH_LEN || memchr(path, '\0', (size_t)len)) {
        return false;
    }
    memcpy(dst, path, (size_t)len);
    dst[len] = '\0';
    return true;
}

int32_t fmrb_bnd_file_read(const char *path, int32_t path_len, uint8_t *out, int32_t out_size)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len) || out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    fmrb_file_info_t info;
    if (fmrb_hal_file_stat(p, &info) != FMRB_OK || FMRB_S_ISDIR(info.mode)) {
        return NONE;
    }
    if (info.size > INT32_MAX) {
        return ERR_FAILED;
    }
    int32_t size = (int32_t)info.size;
    int32_t want = size < out_size ? size : out_size;
    if (want == 0) {
        return size;
    }
    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(p, FMRB_O_RDONLY, &f) != FMRB_OK) {
        return NONE;
    }
    int32_t done = 0;
    while (done < want) {
        size_t got = 0;
        if (fmrb_hal_file_read(f, out + done, (size_t)(want - done), &got) != FMRB_OK || got == 0) {
            break;
        }
        done += (int32_t)got;
    }
    fmrb_hal_file_close(f);
    // A file that grew shorter between stat and read: what was there.
    return done < want ? done : size;
}

static int32_t put_file(const char *path, int32_t path_len, const uint8_t *data, int32_t data_len,
                        uint32_t mode)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len) || data_len < 0 || (data_len > 0 && !data)) {
        return ERR_ARG;
    }
    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(p, FMRB_O_WRONLY | FMRB_O_CREAT | mode, &f) != FMRB_OK) {
        return 0;
    }
    int32_t done = 0;
    bool ok = true;
    while (done < data_len) {
        size_t put = 0;
        if (fmrb_hal_file_write(f, data + done, (size_t)(data_len - done), &put) != FMRB_OK || put == 0) {
            ok = false;
            break;
        }
        done += (int32_t)put;
    }
    if (fmrb_hal_file_close(f) != FMRB_OK) {
        ok = false;
    }
    return ok ? 1 : 0;
}

int32_t fmrb_bnd_file_write(const char *path, int32_t path_len, const uint8_t *data, int32_t data_len)
{
    return put_file(path, path_len, data, data_len, FMRB_O_TRUNC);
}

int32_t fmrb_bnd_file_append(const char *path, int32_t path_len, const uint8_t *data, int32_t data_len)
{
    return put_file(path, path_len, data, data_len, FMRB_O_APPEND);
}

int32_t fmrb_bnd_file_exist(const char *path, int32_t path_len)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len)) {
        return ERR_ARG;
    }
    fmrb_file_info_t info;
    return fmrb_hal_file_stat(p, &info) == FMRB_OK ? 1 : 0;
}

int64_t fmrb_bnd_file_size(const char *path, int32_t path_len)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len)) {
        return ERR_ARG;
    }
    fmrb_file_info_t info;
    if (fmrb_hal_file_stat(p, &info) != FMRB_OK || FMRB_S_ISDIR(info.mode)) {
        return NONE;
    }
    return (int64_t)info.size;
}

// A file, or a directory when it is empty.
int32_t fmrb_bnd_file_delete(const char *path, int32_t path_len)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len)) {
        return ERR_ARG;
    }
    fmrb_file_info_t info;
    if (fmrb_hal_file_stat(p, &info) != FMRB_OK) {
        return 0;
    }
    fmrb_err_t ret = FMRB_S_ISDIR(info.mode) ? fmrb_hal_file_rmdir(p) : fmrb_hal_file_remove(p);
    return ret == FMRB_OK ? 1 : 0;
}

// true when it was made or is a directory already.
int32_t fmrb_bnd_file_mkdir(const char *path, int32_t path_len)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len)) {
        return ERR_ARG;
    }
    fmrb_file_info_t info;
    if (fmrb_hal_file_stat(p, &info) == FMRB_OK) {
        return FMRB_S_ISDIR(info.mode) ? 1 : 0;
    }
    return fmrb_hal_file_mkdir(p) == FMRB_OK ? 1 : 0;
}

// The names, each ended by "\n" (a directory's with "/" before it). What does
// not fit out is counted all the same; a name is never cut in two.
int32_t fmrb_bnd_file_list_raw(const char *dir, int32_t dir_len, char *out, int32_t out_size)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, dir, dir_len) || out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    fmrb_dir_t d = NULL;
    if (fmrb_hal_file_opendir(p, &d) != FMRB_OK) {
        return NONE;
    }
    int32_t total = 0;
    bool full = false;
    fmrb_file_info_t info;
    while (fmrb_hal_file_readdir(d, &info) == FMRB_OK) {
        if (info.name[0] == '\0') {
            break;
        }
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }
        size_t n = strnlen(info.name, sizeof(info.name));
        bool is_dir = FMRB_S_ISDIR(info.mode);
        int32_t need = (int32_t)n + (is_dir ? 1 : 0) + 1;
        if (!full && total + need <= out_size) {
            memcpy(out + total, info.name, n);
            if (is_dir) {
                out[total + (int32_t)n] = '/';
            }
            out[total + need - 1] = '\n';
        } else {
            full = true;
        }
        if (total > INT32_MAX - need) {
            break;
        }
        total += need;
    }
    fmrb_hal_file_closedir(d);
    return total;
}

// The native build's distribution tree and /mnt/root; false elsewhere.
int32_t fmrb_bnd_file_path_read_only(const char *path, int32_t path_len)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len)) {
        return ERR_ARG;
    }
    return fmrb_hal_file_entry_is_read_only(p) ? 1 : 0;
}

// /tmp from the RAM filesystem's own count (statfs there would answer for
// the flash or the host's disk), anything else from the filesystem under it.
// The browser's statvfs is a constant: there /home answers the estimate the
// page last took of the browser's storage, and anything else is none.
int64_t fmrb_bnd_file_free_bytes(const char *path, int32_t path_len)
{
    char p[FMRB_MAX_PATH_LEN];
    if (!path_copy(p, path, path_len)) {
        return ERR_ARG;
    }
    size_t mlen = strlen(FMRB_TMPFS_MOUNT);
    if (strncmp(p, FMRB_TMPFS_MOUNT, mlen) == 0 && (p[mlen] == '\0' || p[mlen] == '/')) {
        size_t used = 0, cap = 0;
        fmrb_tmpfs_usage(&used, &cap);
        return (int64_t)(cap > used ? cap - used : 0);
    }
#if defined(FMRB_PLATFORM_WASM)
    if (strncmp(p, "/home", 5) == 0 && (p[5] == '\0' || p[5] == '/')) {
        int64_t room = fmrb_wasm_storage_free();
        return room < 0 ? NONE : room;
    }
    return NONE;
#else
    uint64_t total = 0, free_bytes = 0;
    if (fmrb_hal_file_statfs(p, &total, &free_bytes) != FMRB_OK || total == 0) {
        return NONE;
    }
    return free_bytes > INT64_MAX ? INT64_MAX : (int64_t)free_bytes;
#endif
}
