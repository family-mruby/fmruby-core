// A picture file's head and the whole file, through the file HAL
// (fmrb_picture332.h). Used by BMP332.info / BMP332.to_png (bmp332.c) and the
// app boundary's FmrbGfx#picture_info (main/app/fmrb_boundary_gfx.c).

#include "fmrb_picture332.h"
#include "fmrb_hal_file.h"

static int stat_file(const char *path, fmrb_file_info_t *st)
{
    if (!path || fmrb_hal_file_stat(path, st) != FMRB_OK) return -1;
    if (st->is_dir || FMRB_S_ISDIR(st->mode)) return -1;
    return 0;
}

int fmrb_picture332_file_info(const char *path, fmrb_picture332_file_info_t *out)
{
    fmrb_file_info_t st;
    if (!out || stat_file(path, &st) != 0) return -1;
    uint8_t head[FMRB_PICTURE332_HEAD_BYTES];
    fmrb_file_t f;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &f) != FMRB_OK) return -1;
    size_t n = 0;
    fmrb_err_t r = fmrb_hal_file_read(f, head, sizeof(head), &n);
    fmrb_hal_file_close(f);
    if (r != FMRB_OK) return -1;
    fmrb_picture332_head_t h;
    if (fmrb_picture332_head(head, n, &h) != 0) return -1;
    out->width = h.width;
    out->height = h.height;
    out->format = h.format;
    out->bytes = st.size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)st.size;
    out->mtime = st.mtime;
    return 0;
}

int fmrb_picture332_file_read(const char *path, size_t max_bytes, const fmrb_png332_allocator_t *a,
                              uint8_t **buf, size_t *len)
{
    fmrb_file_info_t st;
    *buf = NULL;
    *len = 0;
    if (stat_file(path, &st) != 0 || st.size == 0) return FMRB_PICTURE332_E_UNREADABLE;
    if (st.size > max_bytes) {
        *len = st.size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (size_t)st.size;
        return FMRB_PICTURE332_E_FILE_TOO_LARGE;
    }
    size_t size = (size_t)st.size;
    uint8_t *b = (uint8_t *)a->alloc(a->ud, size);
    if (!b) return FMRB_PICTURE332_E_NOMEM;
    fmrb_file_t f;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &f) != FMRB_OK) {
        a->free(a->ud, b);
        return FMRB_PICTURE332_E_UNREADABLE;
    }
    size_t n = 0;
    fmrb_err_t r = fmrb_hal_file_read(f, b, size, &n);
    fmrb_hal_file_close(f);
    if (r != FMRB_OK || n != size) {
        a->free(a->ud, b);
        return FMRB_PICTURE332_E_UNREADABLE;
    }
    *buf = b;
    *len = size;
    return FMRB_PICTURE332_OK;
}
