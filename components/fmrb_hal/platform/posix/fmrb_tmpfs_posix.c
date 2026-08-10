/*
 * /tmp on the Linux dev build.
 *
 * The device keeps /tmp in RAM (platform/esp32/fmrb_tmpfs_esp32.c). Here it is
 * a host directory under the same "flash" root the rest of the namespace uses,
 * which the POSIX path resolver already reaches without a special case. Two
 * behaviours have to be reproduced by hand so the simulator is a fair test of
 * the device:
 *
 *   - volatility: the directory is emptied at boot, because on device a reboot
 *     is what clears the store.
 *   - a capacity limit: writes beyond FMRB_TMPFS_CAPACITY_BYTES fail, so an app
 *     that must survive a full /tmp can be exercised here.
 *
 * The host's own /tmp is deliberately not used: it looks different inside and
 * outside the container, and it would outlive a restart.
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "fmrb_tmpfs.h"
#include "fmrb_log.h"

static const char *TMPFS_TAG = "fmrb_tmpfs";

/* Same root as fmrb_hal_file_posix's BASE_PATH; kept local so this file does
   not reach into the HAL's internals. */
#define TMPFS_HOST_DIR "flash" FMRB_TMPFS_MOUNT

static size_t s_used = 0;

/* True when a resolved host path lives under the /tmp mount. */
bool fmrb_tmpfs_owns_path(const char *host_path)
{
    if (host_path == NULL) {
        return false;
    }
    size_t n = strlen(TMPFS_HOST_DIR);
    return strncmp(host_path, TMPFS_HOST_DIR, n) == 0 &&
           (host_path[n] == '\0' || host_path[n] == '/');
}

/* True when a resolved host path names something *inside* /tmp that is not a
   direct child -- i.e. a subdirectory, which the flat device store cannot
   represent. */
bool fmrb_tmpfs_path_too_deep(const char *host_path)
{
    if (!fmrb_tmpfs_owns_path(host_path)) {
        return false;
    }
    const char *rest = host_path + strlen(TMPFS_HOST_DIR);
    if (*rest != '/') {
        return false;
    }
    rest++;
    return strchr(rest, '/') != NULL;
}

/* Charge `delta` bytes against the mount. Returns false (charging nothing) when
   that would exceed the capacity. */
bool fmrb_tmpfs_charge(size_t delta)
{
    if (delta == 0) {
        return true;
    }
    if (s_used + delta > FMRB_TMPFS_CAPACITY_BYTES) {
        return false;
    }
    s_used += delta;
    return true;
}

void fmrb_tmpfs_release(size_t bytes)
{
    s_used = (bytes > s_used) ? 0 : (s_used - bytes);
}

/* Size of a file under /tmp, 0 when it does not exist. */
size_t fmrb_tmpfs_file_size(const char *host_path)
{
    struct stat st;
    if (stat(host_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return (size_t)st.st_size;
}

fmrb_err_t fmrb_tmpfs_init(void)
{
    if (mkdir(TMPFS_HOST_DIR, 0755) != 0 && errno != EEXIST) {
        FMRB_LOGE(TMPFS_TAG, "cannot create %s: %s", TMPFS_HOST_DIR, strerror(errno));
        return FMRB_ERR_FAILED;
    }

    /* Empty it: on device this store does not survive a reboot. */
    int removed = 0;
    DIR *dir = opendir(TMPFS_HOST_DIR);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", TMPFS_HOST_DIR, entry->d_name);
            if (unlink(path) == 0) {
                removed++;
            } else if (rmdir(path) == 0) {
                removed++;
            }
        }
        closedir(dir);
    }
    s_used = 0;

    FMRB_LOGI(TMPFS_TAG, "%s ready: %d KB capacity (%d stale entries cleared)",
              FMRB_TMPFS_MOUNT, (int)(FMRB_TMPFS_CAPACITY_BYTES / 1024), removed);
    return FMRB_OK;
}

void fmrb_tmpfs_usage(size_t *used, size_t *capacity)
{
    if (used != NULL) {
        *used = s_used;
    }
    if (capacity != NULL) {
        *capacity = FMRB_TMPFS_CAPACITY_BYTES;
    }
}
