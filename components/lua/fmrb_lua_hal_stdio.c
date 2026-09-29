/*
 * fmrb_lua_hal_stdio.c -- the file HAL behind the Lua core's stdio calls.
 *
 * See fmrb_lua_hal_stdio.h. A file the Lua code opens by name is a HAL handle
 * wrapped in a stdio stream with fopencookie(3): every read, write, seek and
 * close on it is one HAL call, taken under the HAL's lock. The Lua core then
 * uses the stream exactly as it used the FILE that fopen returned.
 *
 * This file is NOT compiled with the redirecting header, so the names below
 * are the C library's own.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* fopencookie */
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "fmrb_hal_file.h"
#include "fmrb_hal_time.h"
#include "fmrb_mem.h"

/* The stdio mode as HAL open flags. The HAL has no read-and-append open, so
   "a+" appends but cannot read back (a read answers an error). */
static uint32_t mode_to_flags(const char *mode) {
    int plus = (mode != NULL) && (strchr(mode, '+') != NULL);
    switch (mode ? mode[0] : 'r') {
    case 'w': return (plus ? FMRB_O_RDWR : FMRB_O_WRONLY) | FMRB_O_CREAT | FMRB_O_TRUNC;
    case 'a': return FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_APPEND;
    default:  return plus ? FMRB_O_RDWR : FMRB_O_RDONLY;
    }
}

/* A failed HAL call does not always leave errno behind (a bad parameter or a
   full handle table never reaches the VFS). The Lua libraries put strerror
   into their error message, so give it a reason. */
static void set_errno_if_clear(int fallback) {
    if (errno == 0) errno = fallback;
}

/* The cookie of a plain stream is the HAL handle itself. A tmpfile stream
   also carries its name, to delete the file when the stream is closed. */
typedef struct {
    fmrb_file_t handle;
    char name[32];
} tmp_cookie_t;

#if defined(__GLIBC__)
typedef off64_t cookie_off_t;
#else
typedef off_t cookie_off_t;   /* newlib's _off_t, musl's off_t */
#endif

static ssize_t hal_read(fmrb_file_t h, char *buf, size_t n) {
    size_t got = 0;
    errno = 0;
    if (fmrb_hal_file_read(h, buf, n, &got) != FMRB_OK) { set_errno_if_clear(EIO); return -1; }
    return (ssize_t)got;
}
static ssize_t hal_write(fmrb_file_t h, const char *buf, size_t n) {
    size_t put = 0;
    errno = 0;
    if (fmrb_hal_file_write(h, buf, n, &put) != FMRB_OK) { set_errno_if_clear(EIO); return -1; }
    /* a short write with no error reported is a full disk to the caller */
    if (put == 0 && n > 0) { set_errno_if_clear(ENOSPC); return -1; }
    return (ssize_t)put;
}
static int hal_seek(fmrb_file_t h, cookie_off_t *off, int whence) {
    fmrb_seek_mode_t m = (whence == SEEK_CUR) ? FMRB_SEEK_CUR
                       : (whence == SEEK_END) ? FMRB_SEEK_END : FMRB_SEEK_SET;
    uint32_t pos = 0;
    errno = 0;
    if (fmrb_hal_file_seek(h, (int32_t)*off, m) != FMRB_OK ||
        fmrb_hal_file_tell(h, &pos) != FMRB_OK) {
        set_errno_if_clear(EINVAL);
        return -1;
    }
    *off = (cookie_off_t)pos;
    return 0;
}

static ssize_t plain_read(void *c, char *buf, size_t n) { return hal_read((fmrb_file_t)c, buf, n); }
static ssize_t plain_write(void *c, const char *buf, size_t n) { return hal_write((fmrb_file_t)c, buf, n); }
static int plain_seek(void *c, cookie_off_t *off, int whence) { return hal_seek((fmrb_file_t)c, off, whence); }
static int plain_close(void *c) {
    return fmrb_hal_file_close((fmrb_file_t)c) == FMRB_OK ? 0 : EOF;
}

static ssize_t tmp_read(void *c, char *buf, size_t n) { return hal_read(((tmp_cookie_t *)c)->handle, buf, n); }
static ssize_t tmp_write(void *c, const char *buf, size_t n) { return hal_write(((tmp_cookie_t *)c)->handle, buf, n); }
static int tmp_seek(void *c, cookie_off_t *off, int whence) { return hal_seek(((tmp_cookie_t *)c)->handle, off, whence); }
static int tmp_close(void *c) {
    tmp_cookie_t *t = (tmp_cookie_t *)c;
    int r = fmrb_hal_file_close(t->handle) == FMRB_OK ? 0 : EOF;
    fmrb_hal_file_remove(t->name);
    fmrb_sys_free(t);
    return r;
}

FILE *fmrb_lua_fopen(const char *path, const char *mode) {
    fmrb_file_t h = NULL;
    if (path == NULL || mode == NULL) { errno = EINVAL; return NULL; }
    errno = 0;
    if (fmrb_hal_file_open(path, mode_to_flags(mode), &h) != FMRB_OK || h == NULL) {
        set_errno_if_clear(ENOENT);
        return NULL;
    }
    cookie_io_functions_t fns = { plain_read, plain_write, plain_seek, plain_close };
    FILE *f = fopencookie(h, mode, fns);
    if (f == NULL) {
        fmrb_hal_file_close(h);
        errno = ENOMEM;
    }
    return f;
}

/* The Lua core reopens only to switch a file it opened by name to binary
   mode (luaL_loadfilex); the HAL has no text mode, so close and open again. */
FILE *fmrb_lua_freopen(const char *path, const char *mode, FILE *stream) {
    if (stream != NULL) fclose(stream);
    return fmrb_lua_fopen(path, mode);
}

int fmrb_lua_remove(const char *path) {
    fmrb_file_info_t info;
    if (path == NULL) { errno = EINVAL; return -1; }
    errno = 0;
    if (fmrb_hal_file_stat(path, &info) != FMRB_OK) { set_errno_if_clear(ENOENT); return -1; }
    /* remove(3) takes an empty directory too */
    fmrb_err_t r = (info.is_dir || FMRB_S_ISDIR(info.mode)) ? fmrb_hal_file_rmdir(path)
                                                           : fmrb_hal_file_remove(path);
    if (r != FMRB_OK) { set_errno_if_clear(EACCES); return -1; }
    return 0;
}

int fmrb_lua_rename(const char *from, const char *to) {
    if (from == NULL || to == NULL) { errno = EINVAL; return -1; }
    errno = 0;
    if (fmrb_hal_file_rename(from, to) != FMRB_OK) { set_errno_if_clear(ENOENT); return -1; }
    return 0;
}

/* A name under /tmp (the RAM filesystem) that nothing is using. The clock
   seeds it rather than a static counter, to keep this out of static RAM. */
static int make_tmp_name(char *buf, size_t cap) {
    uint32_t seed = (uint32_t)fmrb_hal_time_get_us();
    for (int i = 0; i < 64; i++) {
        fmrb_file_info_t info;
        snprintf(buf, cap, "/tmp/lua_%08lx", (unsigned long)(seed + (uint32_t)i * 2654435761u));
        if (fmrb_hal_file_stat(buf, &info) != FMRB_OK) return 0;
    }
    errno = EEXIST;
    return -1;
}

char *fmrb_lua_tmpnam(char *buf) {
    /* tmpnam(NULL) would hand back a static buffer; the Lua core always
       passes its own (LUA_TMPNAMBUFSIZE = L_tmpnam, at least 20 bytes) */
    if (buf == NULL) { errno = EINVAL; return NULL; }
    return make_tmp_name(buf, 20) == 0 ? buf : NULL;
}

FILE *fmrb_lua_tmpfile(void) {
    tmp_cookie_t *t = (tmp_cookie_t *)fmrb_sys_malloc(sizeof(tmp_cookie_t));
    if (t == NULL) { errno = ENOMEM; return NULL; }
    if (make_tmp_name(t->name, sizeof t->name) != 0) { fmrb_sys_free(t); return NULL; }
    errno = 0;
    if (fmrb_hal_file_open(t->name, FMRB_O_RDWR | FMRB_O_CREAT | FMRB_O_TRUNC, &t->handle) != FMRB_OK) {
        set_errno_if_clear(EACCES);
        fmrb_sys_free(t);
        return NULL;
    }
    cookie_io_functions_t fns = { tmp_read, tmp_write, tmp_seek, tmp_close };
    FILE *f = fopencookie(t, "w+", fns);
    if (f == NULL) {
        fmrb_hal_file_close(t->handle);
        fmrb_hal_file_remove(t->name);
        fmrb_sys_free(t);
        errno = ENOMEM;
    }
    return f;
}
