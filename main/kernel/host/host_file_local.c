// See host_file_local.h. Ported from fmruby-graphics-audio
// main/file_transfer/file_transfer_handler.c (BEGIN/DATA/END collapse
// into a single synchronous write since the data is already in memory).

#include "host_file_local.h"

#include "fmrb_log.h"
#include "fmrb_link_cobs.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>

static const char *TAG = "host_file";

// On the device the VFS mounts LittleFS at /flash; the wasm build reaches
// the same tree through the POSIX HAL's cwd-relative "flash" directory.
#ifdef FMRB_PLATFORM_WASM
#define LOCAL_BASE_PATH "flash"
#else
#define LOCAL_BASE_PATH "/flash"
#endif
#define LOCAL_MAX_PATH  256

// Build full path from a /flash-relative path (leading '/' tolerated)
static int build_full_path(char *out, size_t out_size,
                           const char *rel_path, uint16_t path_len)
{
    if (!rel_path || path_len == 0 || path_len >= 120) {
        return -1;
    }

    const char *p = rel_path;
    uint16_t len = path_len;
    if (p[0] == '/') {
        p++;
        len--;
    }

    int written = snprintf(out, out_size, "%s/%.*s", LOCAL_BASE_PATH, (int)len, p);
    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }
    return 0;
}

// Create parent directories recursively
static void ensure_parent_dirs(const char *path)
{
    char tmp[LOCAL_MAX_PATH];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + strlen(LOCAL_BASE_PATH) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

fmrb_err_t host_file_local_write(const char *path, uint16_t path_len,
                                 const uint8_t *data, uint32_t len)
{
    char full_path[LOCAL_MAX_PATH];
    if (build_full_path(full_path, sizeof(full_path), path, path_len) != 0) {
        FMRB_LOGE(TAG, "write: invalid path");
        return FMRB_ERR_INVALID_PARAM;
    }

    ensure_parent_dirs(full_path);

    FILE *fp = fopen(full_path, "wb");
    if (!fp) {
        FMRB_LOGE(TAG, "write: cannot open %s: %s", full_path, strerror(errno));
        return FMRB_ERR_FAILED;
    }

    size_t written = (len > 0 && data) ? fwrite(data, 1, len, fp) : 0;
    fclose(fp);

    if (written != len) {
        FMRB_LOGE(TAG, "write: short write %zu/%u to %s",
                  written, (unsigned)len, full_path);
        remove(full_path);
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "write: %u bytes -> %s", (unsigned)len, full_path);
    return FMRB_OK;
}

// CRC32 of a stored file. Mirrors file_crc32 in the WROVER's
// file_transfer_handler.c so both targets report a comparable value.
static uint32_t local_file_crc32(const char *full_path)
{
    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        return 0;
    }
    uint32_t crc = 0;
    uint8_t buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        crc = fmrb_link_crc32_update(crc, buf, n);
    }
    fclose(fp);
    return crc;
}

fmrb_err_t host_file_local_status(const char *path, uint16_t path_len,
                                  uint8_t *out_exists, uint32_t *out_size,
                                  uint32_t *out_checksum)
{
    *out_exists = 0;
    *out_size = 0;
    if (out_checksum) {
        *out_checksum = 0;
    }

    char full_path[LOCAL_MAX_PATH];
    if (build_full_path(full_path, sizeof(full_path), path, path_len) != 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    struct stat st;
    if (stat(full_path, &st) == 0) {
        *out_exists = 1;
        *out_size = (uint32_t)st.st_size;
        if (out_checksum) {
            *out_checksum = local_file_crc32(full_path);
        }
    }
    return FMRB_OK;
}

fmrb_err_t host_file_local_delete(const char *path, uint16_t path_len)
{
    char full_path[LOCAL_MAX_PATH];
    if (build_full_path(full_path, sizeof(full_path), path, path_len) != 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (remove(full_path) != 0) {
        FMRB_LOGW(TAG, "delete: failed to remove %s: %s", full_path, strerror(errno));
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "delete: removed %s", full_path);
    return FMRB_OK;
}

// Recursive removal limited to a max depth to keep the stack bounded.
// LittleFS has no symlink concept, so plain DFS is safe.
#define RMDIR_MAX_DEPTH 8

// remove_self=false on the top-level call leaves the directory itself in
// place (only its contents are wiped).
static int rmdir_recursive(char *path, size_t path_cap, int depth,
                           bool remove_self, uint32_t *deleted)
{
    if (depth > RMDIR_MAX_DEPTH) {
        FMRB_LOGW(TAG, "rmdir: depth limit reached at %s", path);
        return -1;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        // Not a directory: try removing as a regular file.
        if (remove(path) == 0) {
            (*deleted)++;
            return 0;
        }
        if (errno == ENOENT) {
            return 0;  // Non-existent path is a no-op success
        }
        FMRB_LOGW(TAG, "rmdir: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    size_t path_len = strlen(path);
    struct dirent *ent;
    int err = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        int written = snprintf(path + path_len, path_cap - path_len,
                               "/%s", ent->d_name);
        if (written < 0 || (size_t)written >= path_cap - path_len) {
            FMRB_LOGW(TAG, "rmdir: path too long under %s", path);
            err = -1;
            path[path_len] = '\0';
            continue;
        }

        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (rmdir_recursive(path, path_cap, depth + 1, true, deleted) != 0) {
                err = -1;
            }
        } else {
            if (remove(path) == 0) {
                (*deleted)++;
            } else if (errno != ENOENT) {
                FMRB_LOGW(TAG, "rmdir: remove %s failed: %s", path, strerror(errno));
                err = -1;
            }
        }
        path[path_len] = '\0';
    }
    closedir(dir);

    if (!remove_self) {
        return err;
    }
    if (rmdir(path) == 0) {
        (*deleted)++;
    } else if (errno != ENOENT) {
        FMRB_LOGW(TAG, "rmdir: rmdir %s failed: %s", path, strerror(errno));
        err = -1;
    }
    return err;
}

// Reject paths that resolve outside of /flash/cache (parity with the
// WROVER handler: RMDIR is a cache-management operation only).
static bool path_is_inside_cache(const char *full_path)
{
    static const char prefix[] = LOCAL_BASE_PATH "/cache";
    const size_t plen = sizeof(prefix) - 1;
    if (strncmp(full_path, prefix, plen) != 0) {
        return false;
    }
    if (full_path[plen] != '\0' && full_path[plen] != '/') {
        return false;
    }
    if (strstr(full_path, "/..") != NULL) {
        return false;
    }
    return true;
}

fmrb_err_t host_file_local_rmdir(const char *path, uint16_t path_len,
                                 uint32_t *out_deleted, uint8_t *out_status)
{
    *out_deleted = 0;
    *out_status = 0;

    char full_path[LOCAL_MAX_PATH];
    if (build_full_path(full_path, sizeof(full_path), path, path_len) != 0) {
        *out_status = 1;
        return FMRB_ERR_INVALID_PARAM;
    }

    if (!path_is_inside_cache(full_path)) {
        FMRB_LOGE(TAG, "rmdir: path %s outside cache root, rejected", full_path);
        *out_status = 1;
        return FMRB_ERR_INVALID_PARAM;
    }

    // Top-level: clear contents but leave the cache root in place
    uint32_t deleted = 0;
    int rc = rmdir_recursive(full_path, sizeof(full_path), 0, false, &deleted);
    *out_deleted = deleted;
    *out_status = (rc == 0) ? 0 : 2;

    FMRB_LOGI(TAG, "rmdir: %s deleted=%u status=%u",
              full_path, (unsigned)deleted, (unsigned)*out_status);
    return (rc == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}
