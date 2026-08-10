/*
 * /tmp: a RAM filesystem backed by POOL_ID_TMPFS, registered as an ESP-IDF VFS.
 *
 * Why a VFS rather than a special case inside fmrb_hal_file: everything that
 * reaches the filesystem here -- Ruby File/IO, Dir, the app spawner, the editor,
 * transfer_file -- goes through libc on a resolved path. Registering at /tmp
 * means all of it works without knowing this store exists, and a path is the
 * only thing that ever has to cross between VMs (doc/multivm_app/plan.md 3.1).
 *
 * v1 is deliberately flat: /tmp holds files, no subdirectories. That keeps the
 * store to one name table with no traversal, and matches what the POSIX side
 * enforces, so an app tested in the simulator behaves the same on device.
 *
 * Capacity is checked against FMRB_TMPFS_CAPACITY_BYTES before the allocator is
 * asked, so a full /tmp reports ENOSPC to the caller instead of failing halfway
 * through a write.
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>

#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "fmrb_tmpfs.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"

static const char *TAG = "fmrb_tmpfs";

#define TMPFS_MAX_FILES   24
#define TMPFS_MAX_OPEN    8
#define TMPFS_MAX_NAME    63
/* Files grow a block at a time so an append loop does not realloc per byte. */
#define TMPFS_GROW_CHUNK  512

typedef struct {
    char     name[TMPFS_MAX_NAME + 1];
    uint8_t *data;
    size_t   size;
    size_t   cap;
    uint32_t mtime;
    bool     used;
} tmpfs_file_t;

typedef struct {
    bool   used;
    int    file;      /* index into s_files */
    size_t pos;
    int    flags;
} tmpfs_fd_t;

/* The VFS layer stamps dd_vfs_idx on whatever opendir returns, so the DIR must
   be the first member (same shape esp_littlefs uses). */
typedef struct {
    DIR           dir;
    int           idx;
    struct dirent ent;
} tmpfs_dir_t;

static tmpfs_file_t      s_files[TMPFS_MAX_FILES];
static tmpfs_fd_t        s_fds[TMPFS_MAX_OPEN];
static fmrb_mem_handle_t s_handle = -1;
static size_t            s_used = 0;   /* bytes of capacity handed out */
static SemaphoreHandle_t s_mutex = NULL;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

/* ---- helpers ------------------------------------------------------------ */

static uint32_t tmpfs_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}

/* Strip the leading slashes the VFS leaves on and reject anything this flat
   store cannot represent. Returns NULL for the mount root itself. */
static const char *tmpfs_name(const char *path)
{
    if (path == NULL) {
        return NULL;
    }
    while (*path == '/') {
        path++;
    }
    if (*path == '\0') {
        return NULL;
    }
    if (strchr(path, '/') != NULL) {
        return NULL;
    }
    if (strlen(path) > TMPFS_MAX_NAME) {
        return NULL;
    }
    return path;
}

static bool tmpfs_is_root(const char *path)
{
    if (path == NULL) {
        return false;
    }
    while (*path == '/') {
        path++;
    }
    return (*path == '\0');
}

static int find_file(const char *name)
{
    for (int i = 0; i < TMPFS_MAX_FILES; i++) {
        if (s_files[i].used && strcmp(s_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int alloc_file(const char *name)
{
    for (int i = 0; i < TMPFS_MAX_FILES; i++) {
        if (!s_files[i].used) {
            memset(&s_files[i], 0, sizeof(s_files[i]));
            snprintf(s_files[i].name, sizeof(s_files[i].name), "%s", name);
            s_files[i].used = true;
            s_files[i].mtime = tmpfs_now();
            return i;
        }
    }
    return -1;
}

static void release_file(int idx)
{
    tmpfs_file_t *f = &s_files[idx];
    if (f->data != NULL) {
        fmrb_free(s_handle, f->data);
        s_used -= f->cap;
    }
    memset(f, 0, sizeof(*f));
}

/* Grow a file's buffer to hold at least `need` bytes. Returns 0 or -1 with
   errno set (ENOSPC when the mount is full, ENOMEM when the arena is). */
static int ensure_capacity(tmpfs_file_t *f, size_t need)
{
    if (need <= f->cap) {
        return 0;
    }
    size_t new_cap = ((need + TMPFS_GROW_CHUNK - 1) / TMPFS_GROW_CHUNK) * TMPFS_GROW_CHUNK;
    if (s_used - f->cap + new_cap > FMRB_TMPFS_CAPACITY_BYTES) {
        errno = ENOSPC;
        return -1;
    }
    uint8_t *p = (uint8_t *)fmrb_realloc(s_handle, f->data, new_cap);
    if (p == NULL) {
        errno = ENOSPC;
        return -1;
    }
    s_used = s_used - f->cap + new_cap;
    f->data = p;
    f->cap = new_cap;
    return 0;
}

static int alloc_fd(void)
{
    for (int i = 0; i < TMPFS_MAX_OPEN; i++) {
        if (!s_fds[i].used) {
            return i;
        }
    }
    return -1;
}

static bool fd_valid(int fd)
{
    return (fd >= 0 && fd < TMPFS_MAX_OPEN && s_fds[fd].used);
}

/* ---- VFS entry points --------------------------------------------------- */

static int tmpfs_open(const char *path, int flags, int mode)
{
    (void)mode;
    const char *name = tmpfs_name(path);
    if (name == NULL) {
        errno = tmpfs_is_root(path) ? EISDIR : ENOENT;
        return -1;
    }

    LOCK();
    int idx = find_file(name);
    if (idx < 0) {
        if (!(flags & O_CREAT)) {
            UNLOCK();
            errno = ENOENT;
            return -1;
        }
        idx = alloc_file(name);
        if (idx < 0) {
            UNLOCK();
            errno = ENOSPC;
            return -1;
        }
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        UNLOCK();
        errno = EEXIST;
        return -1;
    }

    if (flags & O_TRUNC) {
        s_files[idx].size = 0;
        s_files[idx].mtime = tmpfs_now();
    }

    int fd = alloc_fd();
    if (fd < 0) {
        UNLOCK();
        errno = EMFILE;
        return -1;
    }
    s_fds[fd].used = true;
    s_fds[fd].file = idx;
    s_fds[fd].flags = flags;
    s_fds[fd].pos = (flags & O_APPEND) ? s_files[idx].size : 0;
    UNLOCK();
    return fd;
}

static int tmpfs_close(int fd)
{
    LOCK();
    if (!fd_valid(fd)) {
        UNLOCK();
        errno = EBADF;
        return -1;
    }
    memset(&s_fds[fd], 0, sizeof(s_fds[fd]));
    UNLOCK();
    return 0;
}

static ssize_t tmpfs_read(int fd, void *dst, size_t size)
{
    LOCK();
    if (!fd_valid(fd)) {
        UNLOCK();
        errno = EBADF;
        return -1;
    }
    tmpfs_file_t *f = &s_files[s_fds[fd].file];
    size_t pos = s_fds[fd].pos;
    if (pos >= f->size) {
        UNLOCK();
        return 0;
    }
    size_t n = f->size - pos;
    if (n > size) {
        n = size;
    }
    memcpy(dst, f->data + pos, n);
    s_fds[fd].pos = pos + n;
    UNLOCK();
    return (ssize_t)n;
}

static ssize_t tmpfs_write(int fd, const void *data, size_t size)
{
    if (size == 0) {
        return 0;
    }
    LOCK();
    if (!fd_valid(fd)) {
        UNLOCK();
        errno = EBADF;
        return -1;
    }
    tmpfs_file_t *f = &s_files[s_fds[fd].file];
    if (s_fds[fd].flags & O_APPEND) {
        s_fds[fd].pos = f->size;
    }
    size_t end = s_fds[fd].pos + size;
    if (ensure_capacity(f, end) != 0) {
        UNLOCK();
        return -1;   /* errno set by ensure_capacity */
    }
    /* A seek past the end leaves a hole; zero it so reads are deterministic. */
    if (s_fds[fd].pos > f->size) {
        memset(f->data + f->size, 0, s_fds[fd].pos - f->size);
    }
    memcpy(f->data + s_fds[fd].pos, data, size);
    s_fds[fd].pos = end;
    if (end > f->size) {
        f->size = end;
    }
    f->mtime = tmpfs_now();
    UNLOCK();
    return (ssize_t)size;
}

static off_t tmpfs_lseek(int fd, off_t offset, int whence)
{
    LOCK();
    if (!fd_valid(fd)) {
        UNLOCK();
        errno = EBADF;
        return -1;
    }
    tmpfs_file_t *f = &s_files[s_fds[fd].file];
    off_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (off_t)s_fds[fd].pos; break;
        case SEEK_END: base = (off_t)f->size; break;
        default:
            UNLOCK();
            errno = EINVAL;
            return -1;
    }
    off_t pos = base + offset;
    if (pos < 0) {
        UNLOCK();
        errno = EINVAL;
        return -1;
    }
    s_fds[fd].pos = (size_t)pos;
    UNLOCK();
    return pos;
}

static void fill_stat_file(struct stat *st, const tmpfs_file_t *f)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0666;
    st->st_size = (off_t)f->size;
    st->st_mtime = (time_t)f->mtime;
    st->st_atime = (time_t)f->mtime;
    st->st_ctime = (time_t)f->mtime;
}

static void fill_stat_dir(struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0777;
}

static int tmpfs_fstat(int fd, struct stat *st)
{
    LOCK();
    if (!fd_valid(fd)) {
        UNLOCK();
        errno = EBADF;
        return -1;
    }
    fill_stat_file(st, &s_files[s_fds[fd].file]);
    UNLOCK();
    return 0;
}

static int tmpfs_stat(const char *path, struct stat *st)
{
    if (tmpfs_is_root(path)) {
        fill_stat_dir(st);
        return 0;
    }
    const char *name = tmpfs_name(path);
    if (name == NULL) {
        errno = ENOENT;
        return -1;
    }
    LOCK();
    int idx = find_file(name);
    if (idx < 0) {
        UNLOCK();
        errno = ENOENT;
        return -1;
    }
    fill_stat_file(st, &s_files[idx]);
    UNLOCK();
    return 0;
}

static int tmpfs_unlink(const char *path)
{
    const char *name = tmpfs_name(path);
    if (name == NULL) {
        errno = tmpfs_is_root(path) ? EISDIR : ENOENT;
        return -1;
    }
    LOCK();
    int idx = find_file(name);
    if (idx < 0) {
        UNLOCK();
        errno = ENOENT;
        return -1;
    }
    release_file(idx);
    UNLOCK();
    return 0;
}

static int tmpfs_rename(const char *src, const char *dst)
{
    const char *from = tmpfs_name(src);
    const char *to = tmpfs_name(dst);
    if (from == NULL || to == NULL) {
        errno = ENOENT;
        return -1;
    }
    LOCK();
    int idx = find_file(from);
    if (idx < 0) {
        UNLOCK();
        errno = ENOENT;
        return -1;
    }
    int existing = find_file(to);
    if (existing >= 0 && existing != idx) {
        release_file(existing);
    }
    snprintf(s_files[idx].name, sizeof(s_files[idx].name), "%s", to);
    UNLOCK();
    return 0;
}

static int tmpfs_truncate(const char *path, off_t length)
{
    const char *name = tmpfs_name(path);
    if (name == NULL || length < 0) {
        errno = ENOENT;
        return -1;
    }
    LOCK();
    int idx = find_file(name);
    if (idx < 0) {
        UNLOCK();
        errno = ENOENT;
        return -1;
    }
    tmpfs_file_t *f = &s_files[idx];
    if ((size_t)length > f->size) {
        if (ensure_capacity(f, (size_t)length) != 0) {
            UNLOCK();
            return -1;
        }
        memset(f->data + f->size, 0, (size_t)length - f->size);
    }
    f->size = (size_t)length;
    f->mtime = tmpfs_now();
    UNLOCK();
    return 0;
}

static int tmpfs_ftruncate(int fd, off_t length)
{
    LOCK();
    if (!fd_valid(fd) || length < 0) {
        UNLOCK();
        errno = EBADF;
        return -1;
    }
    tmpfs_file_t *f = &s_files[s_fds[fd].file];
    if ((size_t)length > f->size) {
        if (ensure_capacity(f, (size_t)length) != 0) {
            UNLOCK();
            return -1;
        }
        memset(f->data + f->size, 0, (size_t)length - f->size);
    }
    f->size = (size_t)length;
    f->mtime = tmpfs_now();
    UNLOCK();
    return 0;
}

static int tmpfs_fsync(int fd)
{
    /* Nothing to flush: the store is the RAM the caller just wrote to. */
    LOCK();
    bool ok = fd_valid(fd);
    UNLOCK();
    if (!ok) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

static int tmpfs_access(const char *path, int amode)
{
    (void)amode;
    if (tmpfs_is_root(path)) {
        return 0;
    }
    const char *name = tmpfs_name(path);
    if (name == NULL) {
        errno = ENOENT;
        return -1;
    }
    LOCK();
    int idx = find_file(name);
    UNLOCK();
    if (idx < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static DIR *tmpfs_opendir(const char *name)
{
    if (!tmpfs_is_root(name)) {
        errno = ENOENT;
        return NULL;
    }
    tmpfs_dir_t *d = (tmpfs_dir_t *)fmrb_sys_calloc(1, sizeof(tmpfs_dir_t));
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    d->idx = 0;
    return (DIR *)d;
}

static int tmpfs_readdir_r(DIR *pdir, struct dirent *entry, struct dirent **out)
{
    tmpfs_dir_t *d = (tmpfs_dir_t *)pdir;
    if (d == NULL || entry == NULL || out == NULL) {
        return EBADF;
    }
    LOCK();
    while (d->idx < TMPFS_MAX_FILES && !s_files[d->idx].used) {
        d->idx++;
    }
    if (d->idx >= TMPFS_MAX_FILES) {
        UNLOCK();
        *out = NULL;
        return 0;
    }
    memset(entry, 0, sizeof(*entry));
    entry->d_ino = (ino_t)(d->idx + 1);
    entry->d_type = DT_REG;
    snprintf(entry->d_name, sizeof(entry->d_name), "%s", s_files[d->idx].name);
    d->idx++;
    UNLOCK();
    *out = entry;
    return 0;
}

static struct dirent *tmpfs_readdir(DIR *pdir)
{
    tmpfs_dir_t *d = (tmpfs_dir_t *)pdir;
    if (d == NULL) {
        errno = EBADF;
        return NULL;
    }
    struct dirent *out = NULL;
    int err = tmpfs_readdir_r(pdir, &d->ent, &out);
    if (err != 0) {
        errno = err;
        return NULL;
    }
    return out;
}

static long tmpfs_telldir(DIR *pdir)
{
    tmpfs_dir_t *d = (tmpfs_dir_t *)pdir;
    return d ? (long)d->idx : -1;
}

static void tmpfs_seekdir(DIR *pdir, long offset)
{
    tmpfs_dir_t *d = (tmpfs_dir_t *)pdir;
    if (d != NULL && offset >= 0 && offset <= TMPFS_MAX_FILES) {
        d->idx = (int)offset;
    }
}

static int tmpfs_closedir(DIR *pdir)
{
    if (pdir == NULL) {
        errno = EBADF;
        return -1;
    }
    fmrb_sys_free(pdir);
    return 0;
}

/* Flat store: no directories to make or remove. */
static int tmpfs_mkdir(const char *name, mode_t mode)
{
    (void)name; (void)mode;
    errno = ENOTSUP;
    return -1;
}

static int tmpfs_rmdir(const char *name)
{
    (void)name;
    errno = ENOTSUP;
    return -1;
}

/* ---- registration ------------------------------------------------------- */

fmrb_err_t fmrb_tmpfs_init(void)
{
    if (s_mutex != NULL) {
        return FMRB_OK;   /* already up */
    }

    void  *pool = fmrb_get_mempool_ptr(POOL_ID_TMPFS);
    size_t size = fmrb_get_mempool_size(POOL_ID_TMPFS);
    if (pool == NULL || size == 0) {
        FMRB_LOGE(TAG, "tmpfs pool unavailable");
        return FMRB_ERR_NO_MEMORY;
    }
    s_handle = fmrb_mem_create_handle(pool, size, POOL_ID_TMPFS);
    if (s_handle < 0) {
        FMRB_LOGE(TAG, "tmpfs pool handle failed");
        return FMRB_ERR_NO_MEMORY;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }
    memset(s_files, 0, sizeof(s_files));
    memset(s_fds, 0, sizeof(s_fds));
    s_used = 0;

    esp_vfs_t vfs = {
        .flags     = ESP_VFS_FLAG_DEFAULT,
        .open      = &tmpfs_open,
        .close     = &tmpfs_close,
        .read      = &tmpfs_read,
        .write     = &tmpfs_write,
        .lseek     = &tmpfs_lseek,
        .fstat     = &tmpfs_fstat,
        .stat      = &tmpfs_stat,
        .unlink    = &tmpfs_unlink,
        .rename    = &tmpfs_rename,
        .truncate  = &tmpfs_truncate,
        .ftruncate = &tmpfs_ftruncate,
        .fsync     = &tmpfs_fsync,
        .access    = &tmpfs_access,
        .opendir   = &tmpfs_opendir,
        .readdir   = &tmpfs_readdir,
        .readdir_r = &tmpfs_readdir_r,
        .telldir   = &tmpfs_telldir,
        .seekdir   = &tmpfs_seekdir,
        .closedir  = &tmpfs_closedir,
        .mkdir     = &tmpfs_mkdir,
        .rmdir     = &tmpfs_rmdir,
    };
    esp_err_t err = esp_vfs_register(FMRB_TMPFS_MOUNT, &vfs, NULL);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "esp_vfs_register(%s) failed: %d", FMRB_TMPFS_MOUNT, (int)err);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "%s ready: %d KB of %d KB pool, %d files max",
              FMRB_TMPFS_MOUNT, (int)(FMRB_TMPFS_CAPACITY_BYTES / 1024),
              (int)(size / 1024), TMPFS_MAX_FILES);
    return FMRB_OK;
}

void fmrb_tmpfs_usage(size_t *used, size_t *capacity)
{
    if (used != NULL) {
        if (s_mutex != NULL) {
            LOCK();
            *used = s_used;
            UNLOCK();
        } else {
            *used = 0;
        }
    }
    if (capacity != NULL) {
        *capacity = FMRB_TMPFS_CAPACITY_BYTES;
    }
}
