// Host-test stand-ins for the file HAL and the SYSTEM pool, for
// components/fmrb_kanji (tools/kanji/test/c_lookup.rb). Paths are host paths.
// A read can be made to fail (kd_fail_reads_after) to test the error path.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fmrb_hal_file.h"
#include "fmrb_mem.h"

long kd_live_bytes = 0;
long kd_live_blocks = 0;
long kd_fail_reads_after = -1;  // < 0: never

void *fmrb_sys_malloc(size_t size)
{
    size_t *p = malloc(sizeof(size_t) * 2 + size);
    if (p == NULL) {
        return NULL;
    }
    p[0] = size;
    kd_live_bytes += (long)size;
    kd_live_blocks++;
    return p + 2;
}

void fmrb_sys_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    size_t *p = (size_t *)ptr - 2;
    kd_live_bytes -= (long)p[0];
    kd_live_blocks--;
    free(p);
}

fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle)
{
    if (flags != FMRB_O_RDONLY) {
        return FMRB_ERR_NOT_SUPPORTED;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return FMRB_ERR_NOT_FOUND;
    }
    *out_handle = f;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_file_close(fmrb_file_t handle)
{
    fclose((FILE *)handle);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read)
{
    if (kd_fail_reads_after == 0) {
        return FMRB_ERR_FAILED;
    }
    if (kd_fail_reads_after > 0) {
        kd_fail_reads_after--;
    }
    *bytes_read = fread(buffer, 1, size, (FILE *)handle);
    return ferror((FILE *)handle) ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t fmrb_hal_file_seek(fmrb_file_t handle, int32_t offset, fmrb_seek_mode_t mode)
{
    int whence = mode == FMRB_SEEK_SET ? SEEK_SET : mode == FMRB_SEEK_CUR ? SEEK_CUR : SEEK_END;
    return fseek((FILE *)handle, offset, whence) == 0 ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t fmrb_hal_file_size(fmrb_file_t handle, uint32_t *size)
{
    FILE *f = (FILE *)handle;
    long here = ftell(f);
    if (fseek(f, 0, SEEK_END) != 0) {
        return FMRB_ERR_FAILED;
    }
    long end = ftell(f);
    fseek(f, here, SEEK_SET);
    *size = (uint32_t)end;
    return FMRB_OK;
}
