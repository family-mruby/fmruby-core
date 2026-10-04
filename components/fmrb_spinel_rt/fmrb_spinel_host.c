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
 * the host FS on Linux, and resolves virtual paths ("/app" -> flash/app). Every
 * path op the runtime makes under SP_MULTI_CTX lands on one of the hooks below
 * (the ones the backend cannot express raise NotImplementedError instead), so
 * all of a Spinel program's file access takes the HAL's lock -- which is what
 * lets a forced app kill wait for the lock instead of deleting a task that is
 * inside littlefs (doc/fs_kill_hang). It is
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
extern int fmrb_hal_file_remove(const char *path);
extern int fmrb_hal_file_rename(const char *old_path, const char *new_path);
extern int fmrb_hal_file_mkdir(const char *path);
extern int fmrb_hal_file_rmdir(const char *path);

#define FMRB_S_ISDIR_M(m) (((m) & 0170000u) == 0040000u)
#define FMRB_S_ISREG_M(m) (((m) & 0170000u) == 0100000u)

static unsigned int hal_flags_from_mode(const char *mode) {
  /* fopen modes as FMRB_O_* bits (RDONLY 0x1, WRONLY 0x2, RDWR 0x4, CREAT 0x8,
     TRUNC 0x10, APPEND 0x20). "r+" / "w+" read and write; "a+" stays
     write-only append, the HAL having no read-append open. */
  int plus = mode && mode[0] && (mode[1] == '+' || (mode[1] && mode[2] == '+'));
  if (mode && mode[0] == 'w') return (plus ? 0x0004u : 0x0002u) | 0x0008u | 0x0010u;
  if (mode && mode[0] == 'a') return 0x0002u | 0x0008u | 0x0020u;
  return plus ? 0x0004u : 0x0001u;
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
static int hal_remove(void *ud, const char *path) { (void)ud; return fmrb_hal_file_remove(path); }
static int hal_rename(void *ud, const char *from, const char *to) { (void)ud; return fmrb_hal_file_rename(from, to); }
static int hal_mkdir(void *ud, const char *path) { (void)ud; return fmrb_hal_file_mkdir(path); }
static int hal_rmdir(void *ud, const char *path) { (void)ud; return fmrb_hal_file_rmdir(path); }

/* est -> sp_ctx map so a foreign task (the stats dump) can reach an instance
 * it did not create. Slots are few (kernel + desktop + spare); linear scan. */
#define FMRB_SPINEL_MAX_INSTANCES 4
static struct { void *est; sp_ctx *ctx; } s_instances[FMRB_SPINEL_MAX_INSTANCES];

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
    /* Generational-collector sets (remembered old objects, pinned String
       holders), carved from this pool at instance creation. 0 takes the
       runtime defaults, 1024 / 256 entries -- 4 KB + 1 KB on a 32-bit target,
       under 4% of the smallest pool (the 128 KB gem instances). Overflow is
       safe: it only turns the next minor collection into a full mark. */
    cfg.remembered_entries = 0;
    cfg.pinned_entries     = 0;
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
    cfg.io_remove   = hal_remove;
    cfg.io_rename   = hal_rename;
    cfg.io_mkdir    = hal_mkdir;
    cfg.io_rmdir    = hal_rmdir;
    sp_ctx *c = sp_instance_create(&cfg);
    if (!c) { est_cleanup(est); return NULL; }
    sp_ctx_set_current(c);
    for (int i = 0; i < FMRB_SPINEL_MAX_INSTANCES; i++) {
        if (!s_instances[i].est) { s_instances[i].est = est; s_instances[i].ctx = c; break; }
    }
    return est;
}

void fmrb_spinel_instance_end(void *est) {
    sp_ctx *c = sp_ctx_current();
    if (c) sp_instance_destroy(c);
    sp_ctx_set_current(NULL);
    for (int i = 0; i < FMRB_SPINEL_MAX_INSTANCES; i++) {
        if (est && s_instances[i].est == est) { s_instances[i].est = NULL; s_instances[i].ctx = NULL; }
    }
    if (est) est_cleanup(est);
}

int fmrb_spinel_instance_exc_hw(void *est, int *exc_hw, int *catch_hw) {
    for (int i = 0; i < FMRB_SPINEL_MAX_INSTANCES; i++) {
        if (est && s_instances[i].est == est) {
            sp_instance_exc_hw(s_instances[i].ctx, exc_hw, catch_hw);
            return 0;
        }
    }
    if (exc_hw)   *exc_hw = 0;
    if (catch_hw) *catch_hw = 0;
    return -1;
}

#else  /* !SP_MULTI_CTX: single-context build has no per-instance API. */

void *fmrb_spinel_instance_begin(void *pool, size_t pool_size,
                                 size_t gc_threshold, size_t str_threshold) {
    (void)pool; (void)pool_size; (void)gc_threshold; (void)str_threshold;
    return NULL;
}
void fmrb_spinel_instance_end(void *est) { (void)est; }
int fmrb_spinel_instance_exc_hw(void *est, int *exc_hw, int *catch_hw) {
    (void)est;
    if (exc_hw)   *exc_hw = 0;
    if (catch_hw) *catch_hw = 0;
    return -1;
}

#endif /* SP_MULTI_CTX */

/* ---- Spinel gem ownership (fmrb_spinel_host.h) ---- */

/* The list fmrb_spinel_gem_task_ended walks. One pointer; on the device it
   goes to PSRAM with the runtime's other cold statics (fmrb_sp_tu_bss.h),
   since nothing here is on a hot path and internal RAM is the scarce one. */
#ifdef SP_RT_COLD
#define FMRB_GEM_BSS SP_RT_COLD
#else
#define FMRB_GEM_BSS
#endif
FMRB_GEM_BSS static fmrb_spinel_gem_t *s_gems;

/* Drop the instance without making it current: it belongs to a task that is
   gone (or is going), and the caller may be a Spinel task itself -- the kernel
   kills apps, and fmrb_spinel_instance_end would destroy ITS instance. Every
   allocation of the instance lives in its pool, so releasing the pool is the
   whole teardown, as with a killed app's mruby VM. */
static void gem_drop(fmrb_spinel_gem_t *g)
{
    void *est = g->est;
    void *pool = g->pool;
#ifdef SP_MULTI_CTX
    for (int i = 0; i < FMRB_SPINEL_MAX_INSTANCES; i++) {
        if (est && s_instances[i].est == est) { s_instances[i].est = NULL; s_instances[i].ctx = NULL; }
    }
    if (est) est_cleanup(est);
#endif
    if (pool && g->pool_free) g->pool_free(pool);
    fmrb_spinel_gem_release(g);
}

int fmrb_spinel_gem_claim(fmrb_spinel_gem_t *g, void *task, int pid, const char *app)
{
    void *expected = NULL;
    if (!__atomic_compare_exchange_n(&g->owner, &expected, task, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return (expected == task) ? FMRB_SPINEL_GEM_MINE : FMRB_SPINEL_GEM_BUSY;
    }
    g->owner_pid = pid;
    g->owner_app[0] = '\0';
    if (app) {
        strncpy(g->owner_app, app, sizeof g->owner_app - 1);
        g->owner_app[sizeof g->owner_app - 1] = '\0';
    }
    /* Linked once, by its first owner; only one task owns a gem at a time,
       so `linked` has one writer. The head is shared by all gems. */
    if (!g->linked) {
        fmrb_spinel_gem_t *head = __atomic_load_n(&s_gems, __ATOMIC_ACQUIRE);
        do {
            g->next = head;
        } while (!__atomic_compare_exchange_n(&s_gems, &head, g, 0,
                                              __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
        g->linked = 1;
    }
    return FMRB_SPINEL_GEM_CLAIMED;
}

int fmrb_spinel_gem_is_open_on(const fmrb_spinel_gem_t *g, void *task)
{
    return task && g->est && __atomic_load_n(&g->owner, __ATOMIC_ACQUIRE) == task;
}

void fmrb_spinel_gem_release(fmrb_spinel_gem_t *g)
{
    g->est = NULL;
    g->pool = NULL;
    g->owner_pid = -1;
    g->owner_app[0] = '\0';
    __atomic_store_n(&g->owner, NULL, __ATOMIC_RELEASE);
}

int fmrb_spinel_gem_task_ended(void *task)
{
    int dropped = 0;
    if (!task) return 0;
    for (fmrb_spinel_gem_t *g = __atomic_load_n(&s_gems, __ATOMIC_ACQUIRE); g; g = g->next) {
        if (__atomic_load_n(&g->owner, __ATOMIC_ACQUIRE) != task) continue;
        gem_drop(g);
        dropped++;
    }
    return dropped;
}
