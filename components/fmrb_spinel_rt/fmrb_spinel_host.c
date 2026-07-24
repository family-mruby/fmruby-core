/* fmrb_spinel_host.c -- create/destroy a Spinel runtime instance on an estalloc
 * pool. See fmrb_spinel_host.h for why this boundary exists (header isolation
 * from the mruby side). Built inside the fmrb_spinel_rt component, so it sees
 * SP_MULTI_CTX and the spinel_rt headers; it deliberately includes NO mruby /
 * main headers so the runtime's mrb_bool etc. do not collide. */
#include <string.h>
#include "fmrb_spinel_host.h"

#ifdef SP_MULTI_CTX
#include "sp_gc.h"   /* pulls sp_ctx.h: sp_instance_config, lifecycle */

/* estalloc entry points (picoruby, lib/estalloc). Declared here with opaque
 * (void*) handles so this file need not include estalloc.h. est_calloc zero-
 * fills, satisfying the sp_instance_config "alloc MUST zero" contract. */
extern void *est_init(void *ptr, unsigned int size);
extern void *est_calloc(void *est, unsigned int nmemb, unsigned int size);
extern void *est_realloc(void *est, void *ptr, unsigned int size);
extern void  est_free(void *est, void *ptr);
extern void  est_cleanup(void *est);

static void *est_alloc_hook(void *ud, size_t n)            { return est_calloc(ud, 1u, (unsigned int)n); }
static void *est_realloc_hook(void *ud, void *p, size_t n) { return est_realloc(ud, p, (unsigned int)n); }
static void  est_free_hook(void *ud, void *p)             { est_free(ud, p); }

/* ---- I/O backend: route Spinel File/Dir through the fmrb HAL (VFS) ----
 * The fmrb HAL (components/fmrb_hal/fmrb_hal_file.h) backs littlefs on ESP32 and
 * the host FS on Linux, and resolves virtual paths ("/app" -> flash/app). It is
 * declared here with opaque (void*) handles + int (fmrb_err_t, FMRB_OK == 0) so
 * this file stays free of the HAL/main headers (same isolation as est_* above);
 * the symbols resolve when main links fmrb_hal. fmrb_hal_finfo_t mirrors
 * fmrb_file_info_t's layout (only name/mode/size are read). */
typedef struct { char name[256]; unsigned int mode; unsigned long long size; unsigned char is_dir; unsigned int mtime; } fmrb_hal_finfo_t;
extern int fmrb_hal_file_open(const char *path, unsigned int flags, void **out_handle);
extern int fmrb_hal_file_close(void *handle);
extern int fmrb_hal_file_read(void *handle, void *buffer, size_t size, size_t *bytes_read);
extern int fmrb_hal_file_write(void *handle, const void *buffer, size_t size, size_t *bytes_written);
extern int fmrb_hal_file_seek(void *handle, int offset, int mode);
extern int fmrb_hal_file_tell(void *handle, unsigned int *position);
extern int fmrb_hal_file_stat(const char *path, fmrb_hal_finfo_t *info);
extern int fmrb_hal_file_opendir(const char *path, void **out_handle);
extern int fmrb_hal_file_closedir(void *handle);
extern int fmrb_hal_file_readdir(void *handle, fmrb_hal_finfo_t *info);

#define FMRB_S_ISDIR_M(m) (((m) & 0170000u) == 0040000u)
#define FMRB_S_ISREG_M(m) (((m) & 0170000u) == 0100000u)

static unsigned int hal_flags_from_mode(const char *mode) {
  /* small fopen-mode subset the apps use (r / w / a). FMRB_O_* bit values. */
  if (mode && mode[0] == 'w') return 0x0002u | 0x0008u | 0x0010u;  /* WRONLY|CREAT|TRUNC */
  if (mode && mode[0] == 'a') return 0x0002u | 0x0008u | 0x0020u;  /* WRONLY|CREAT|APPEND */
  return 0x0001u;                                                  /* RDONLY */
}

static void *hal_open(void *ud, const char *path, const char *mode) {
  (void)ud; void *h = 0;
  return (fmrb_hal_file_open(path, hal_flags_from_mode(mode), &h) == 0) ? h : 0;
}
static long hal_read(void *ud, void *h, char *buf, long n) {
  (void)ud; size_t br = 0; return (fmrb_hal_file_read(h, buf, (size_t)n, &br) == 0) ? (long)br : -1;
}
static long hal_write(void *ud, void *h, const char *buf, long n) {
  (void)ud; size_t bw = 0; return (fmrb_hal_file_write(h, buf, (size_t)n, &bw) == 0) ? (long)bw : -1;
}
static long hal_seek(void *ud, void *h, long off, int whence) {
  (void)ud; if (fmrb_hal_file_seek(h, (int)off, whence) != 0) return -1;
  unsigned int pos = 0; return (fmrb_hal_file_tell(h, &pos) == 0) ? (long)pos : -1;
}
static long hal_tell(void *ud, void *h) {
  (void)ud; unsigned int pos = 0; return (fmrb_hal_file_tell(h, &pos) == 0) ? (long)pos : -1;
}
static int hal_close(void *ud, void *h) { (void)ud; return fmrb_hal_file_close(h); }
static int hal_stat(void *ud, const char *path, long *size, int *is_dir, int *is_reg) {
  (void)ud; fmrb_hal_finfo_t info; memset(&info, 0, sizeof info);
  if (fmrb_hal_file_stat(path, &info) != 0) return -1;
  if (size) *size = (long)info.size;
  if (is_dir) *is_dir = (info.is_dir || FMRB_S_ISDIR_M(info.mode)) ? 1 : 0;
  if (is_reg) *is_reg = FMRB_S_ISREG_M(info.mode) ? 1 : 0;
  return 0;
}
static void *hal_opendir(void *ud, const char *path) {
  (void)ud; void *h = 0; return (fmrb_hal_file_opendir(path, &h) == 0) ? h : 0;
}
static int hal_readdir(void *ud, void *dh, char *namebuf, int cap) {
  (void)ud; fmrb_hal_finfo_t info; memset(&info, 0, sizeof info);
  if (fmrb_hal_file_readdir(dh, &info) != 0) return 0;
  strncpy(namebuf, info.name, (size_t)cap - 1); namebuf[cap - 1] = 0; return 1;
}
static int hal_closedir(void *ud, void *dh) { (void)ud; return fmrb_hal_file_closedir(dh); }

void *fmrb_spinel_instance_begin(void *pool, size_t pool_size,
                                 size_t gc_threshold, size_t str_threshold) {
    void *est = est_init(pool, (unsigned int)pool_size);
    if (!est) return NULL;
    sp_instance_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.mem_ud     = est;
    cfg.alloc      = est_alloc_hook;
    cfg.realloc_fn = est_realloc_hook;
    cfg.dealloc    = est_free_hook;
    cfg.gc_threshold  = gc_threshold;
    cfg.str_threshold = str_threshold;
    /* Route File/Dir I/O through the fmrb HAL so virtual paths resolve and the
       backing store (littlefs on ESP32, host FS on Linux) is reachable. */
    cfg.io_open     = hal_open;
    cfg.io_read     = hal_read;
    cfg.io_write    = hal_write;
    cfg.io_seek     = hal_seek;
    cfg.io_tell     = hal_tell;
    cfg.io_close    = hal_close;
    cfg.io_stat     = hal_stat;
    cfg.io_opendir  = hal_opendir;
    cfg.io_readdir  = hal_readdir;
    cfg.io_closedir = hal_closedir;
    sp_ctx *c = sp_instance_create(&cfg);
    if (!c) { est_cleanup(est); return NULL; }
    sp_ctx_set_current(c);
    return est;
}

void fmrb_spinel_instance_end(void *est) {
    sp_ctx *c = sp_ctx_current();
    if (c) sp_instance_destroy(c);
    sp_ctx_set_current(NULL);
    if (est) est_cleanup(est);
}

#else  /* !SP_MULTI_CTX: single-context build has no per-instance API. */

void *fmrb_spinel_instance_begin(void *pool, size_t pool_size,
                                 size_t gc_threshold, size_t str_threshold) {
    (void)pool; (void)pool_size; (void)gc_threshold; (void)str_threshold;
    return NULL;
}
void fmrb_spinel_instance_end(void *est) { (void)est; }

#endif /* SP_MULTI_CTX */
