// Files handed to and from a program in pieces (doc/checkpoint F2; the API is
// in fmrb_app.h). The file being received is written beside its name
// ("<path>.part") and renamed at the commit.

#ifdef FMRB_HAVE_CHECKPOINT

#include <stdio.h>
#include <string.h>

#include "fmrb_app.h"
#include "fmrb_attr.h"
#include "fmrb_hal.h"
#include "fmrb_log.h"

static const char *TAG = "transfer";

#define TRANSFER_DIR "/tmp/"
#define PART_SUFFIX ".part"

typedef struct {
    bool active;
    uint32_t id;
    uint32_t size;
    uint32_t got;
    fmrb_file_t f;
    char path[FMRB_MAX_PATH_LEN];
    char part[FMRB_MAX_PATH_LEN + sizeof(PART_SUFFIX)];
} transfer_t;

// External RAM on a device (two paths' worth; internal RAM does not grow).
FMRB_EXT_RAM_BSS_ATTR static transfer_t s_in;
FMRB_EXT_RAM_BSS_ATTR static uint32_t s_last_id;   // ids start at 1

// Only plain names under /tmp/: no way up and out of it.
static bool path_ok(const char *path)
{
    size_t n = path ? strlen(path) : 0;
    size_t d = sizeof(TRANSFER_DIR) - 1;
    if (n <= d || n >= FMRB_MAX_PATH_LEN || strncmp(path, TRANSFER_DIR, d) != 0) return false;
    if (strstr(path, "..") || strchr(path + d, '/')) return false;
    return true;
}

static void drop_in(void)
{
    if (!s_in.active) return;
    if (s_in.f) fmrb_hal_file_close(s_in.f);
    fmrb_hal_file_remove(s_in.part);
    s_in.f = NULL;
    s_in.active = false;
}

fmrb_err_t fmrb_app_transfer_begin(const char *path, uint32_t size, uint32_t *id)
{
    if (!path_ok(path) || !id) return FMRB_ERR_INVALID_PARAM;
    if (s_in.active) {
        FMRB_LOGW(TAG, "%s was not committed; dropped for %s", s_in.path, path);
        drop_in();
    }
    snprintf(s_in.path, sizeof(s_in.path), "%s", path);
    snprintf(s_in.part, sizeof(s_in.part), "%s" PART_SUFFIX, path);
    fmrb_err_t ret = fmrb_hal_file_open(s_in.part, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &s_in.f);
    if (ret != FMRB_OK) {
        s_in.f = NULL;
        return ret;
    }
    s_in.active = true;
    s_in.size = size;
    s_in.got = 0;
    s_in.id = ++s_last_id;
    *id = s_in.id;
    FMRB_LOGI(TAG, "receiving %s (%u bytes)", path, (unsigned)size);
    return FMRB_OK;
}

fmrb_err_t fmrb_app_transfer_chunk(uint32_t id, uint32_t pos, const uint8_t *data, size_t len)
{
    if (!s_in.active || id != s_in.id) return FMRB_ERR_INVALID_STATE;
    if (pos != s_in.got || (uint64_t)pos + len > s_in.size) return FMRB_ERR_INVALID_PARAM;
    size_t done = 0;
    fmrb_err_t ret = fmrb_hal_file_write(s_in.f, data, len, &done);
    if (ret != FMRB_OK || done != len) {
        FMRB_LOGW(TAG, "cannot write %s at %u", s_in.part, (unsigned)pos);
        drop_in();
        return ret != FMRB_OK ? ret : FMRB_ERR_FAILED;
    }
    s_in.got += (uint32_t)len;
    return FMRB_OK;
}

fmrb_err_t fmrb_app_transfer_commit(uint32_t id)
{
    if (!s_in.active || id != s_in.id) return FMRB_ERR_INVALID_STATE;
    if (s_in.got != s_in.size) {
        FMRB_LOGW(TAG, "%s: %u of %u bytes came; dropped", s_in.path, (unsigned)s_in.got, (unsigned)s_in.size);
        drop_in();
        return FMRB_ERR_INVALID_STATE;
    }
    fmrb_hal_file_close(s_in.f);
    s_in.f = NULL;
    s_in.active = false;
    fmrb_hal_file_remove(s_in.path);   // a file of that name from before
    fmrb_err_t ret = fmrb_hal_file_rename(s_in.part, s_in.path);
    if (ret != FMRB_OK) {
        fmrb_hal_file_remove(s_in.part);
        return ret;
    }
    FMRB_LOGI(TAG, "received %s (%u bytes)", s_in.path, (unsigned)s_in.size);
    return FMRB_OK;
}

fmrb_err_t fmrb_app_transfer_read(const char *path, uint32_t pos, uint8_t *buf, size_t max,
                                  size_t *got, uint32_t *size)
{
    *got = 0;
    *size = 0;
    if (!path_ok(path)) return FMRB_ERR_INVALID_PARAM;
    fmrb_file_t f = NULL;
    fmrb_err_t ret = fmrb_hal_file_open(path, FMRB_O_RDONLY, &f);
    if (ret != FMRB_OK) return ret;
    ret = fmrb_hal_file_size(f, size);
    if (ret == FMRB_OK && pos < *size) {
        ret = fmrb_hal_file_seek(f, (int32_t)pos, FMRB_SEEK_SET);
        size_t want = *size - pos < max ? *size - pos : max;
        if (ret == FMRB_OK) ret = fmrb_hal_file_read(f, buf, want, got);
    }
    fmrb_hal_file_close(f);
    return ret;
}

#endif /* FMRB_HAVE_CHECKPOINT */
