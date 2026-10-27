#include "fmrb_hal.h"
#include "fmrb_hal_file.h"
#include "fmrb_rtos.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#include "fmrb_tmpfs_posix.h"
#include "fmrb_hal_file_layout.h"

const char* TAG = "file_posix";

// Internal file handle structure. Open handles are kept on a list, with the
// task that opened them, so an app's leftovers can be closed when it ends
// (fmrb_hal_file_close_owned_locked).
typedef struct fmrb_file_handle {
    struct fmrb_file_handle *prev, *next;
    void *owner;
    FILE *fp;
    // /tmp is capacity-limited on device, so the dev build accounts for it too.
    // `counted` is how many of this file's bytes are already charged against
    // the mount; a write past that mark has to buy the difference.
    bool   in_tmp;
    size_t counted;
} fmrb_file_handle_t;

// Internal directory handle structure
typedef struct fmrb_dir_handle {
    struct fmrb_dir_handle *prev, *next;
    void *owner;
    DIR *dir;
    char dir_path[512];  // Store directory path for stat() calls in readdir
} fmrb_dir_handle_t;

// Global mutex for thread safety
static fmrb_semaphore_t s_file_mutex = NULL;
#define LOCK() fmrb_semaphore_take(s_file_mutex, FMRB_TICK_MAX)
#define UNLOCK() fmrb_semaphore_give(s_file_mutex)

// Open handles, both lists guarded by s_file_mutex
static fmrb_file_handle_t *s_open_files = NULL;
static fmrb_dir_handle_t *s_open_dirs = NULL;

#define HANDLE_LIST_ADD(head, h) do {           \
        (h)->prev = NULL;                       \
        (h)->next = (head);                     \
        if (head) (head)->prev = (h);           \
        (head) = (h);                           \
    } while (0)

#define HANDLE_LIST_REMOVE(head, h) do {                    \
        if ((h)->prev) (h)->prev->next = (h)->next;         \
        else (head) = (h)->next;                            \
        if ((h)->next) (h)->next->prev = (h)->prev;         \
        (h)->prev = (h)->next = NULL;                       \
    } while (0)

// Check if handle is a standard stream
static bool is_std_stream(fmrb_file_t handle) {
    return (handle == FMRB_STDIN_HANDLE ||
            handle == FMRB_STDOUT_HANDLE ||
            handle == FMRB_STDERR_HANDLE);
}

// Get FILE* for standard stream
static FILE* get_std_stream_fp(fmrb_file_t handle) {
    if (handle == FMRB_STDIN_HANDLE) return stdin;
    if (handle == FMRB_STDOUT_HANDLE) return stdout;
    if (handle == FMRB_STDERR_HANDLE) return stderr;
    return NULL;
}

// ---------------------------------------------------------------------------
// The namespace table (fmrb_hal_file_layout.h).
//
// One resolver, two tables. Each entry names a top-level place in the Family
// mruby namespace and the host root it lives under; the host path is that
// root followed by the whole virtual path, so every root is a tree shaped
// like the namespace itself. The first entry whose top matches (at a '/'
// boundary) wins, so the more specific places come first; "" matches
// everything.
//
// What a write may touch is part of the same entry, so the checks below ask
// the table rather than spell places out.

typedef enum {
    ROOT_SYSTEM = 0,   // distribution tree (flat: the writable flash/)
    ROOT_USER,         // the user's files
    ROOT_CACHE,        // things that can be rebuilt
    ROOT_HOST_HOME,    // Linux: the user's home, passed through (/mnt/host)
    ROOT_HOST_FS,      // Linux: "/", passed through (/mnt/root)
    ROOT_COUNT
} fs_root_t;

// `host` marks a place that passes Linux's own files through: the host path
// is the root followed by what comes after `top` (not the whole virtual
// path), only regular files and directories are reachable, and a write must
// land inside the root's real location (host_write_check).
typedef struct {
    const char *top;
    fs_root_t   root;
    bool        read_only;
    bool        host;
} fs_mount_t;

// The simulation and the browser: everything in the one writable flash/.
static const fs_mount_t s_flat_table[] = {
    { "",         ROOT_SYSTEM, false, false },
};

// The native program (doc/linux_native/plan.md). /data is the cache rather
// than the distribution: nothing in it is shipped, and the desktop syncs its
// wallpaper and icons there for the graphics side, which is a rebuildable
// copy. /mnt/host and /mnt/root show Linux's own files (L7); a root left
// empty by the layout leaves its place out.
static const fs_mount_t s_layered_table[] = {
    { "/mnt/host", ROOT_HOST_HOME, false, true  },
    { "/mnt/root", ROOT_HOST_FS,   true,  true  },
    { "/app/usr",  ROOT_USER,      false, false },
    { "/home",     ROOT_USER,      false, false },
    { "/etc",      ROOT_USER,      false, false },
    { "/var",      ROOT_CACHE,     false, false },
    { "/cache",    ROOT_CACHE,     false, false },
    { "/data",     ROOT_CACHE,     false, false },
    { "/tmp",      ROOT_CACHE,     false, false },
    { "",          ROOT_SYSTEM,    true,  false },
};

// The top-level names the layered table puts outside the distribution tree:
// a listing of "/" (or "/app", "/mnt") has to show them even where the
// distribution has no directory of that name
// (fmrb_hal_file_virtual_children). "mnt" and its children are there only
// when the layout shows a host place.
static const char *s_layered_root_children[8];
static size_t s_layered_root_count;
static const char *const s_layered_app_children[] = { "usr" };
static const char *s_layered_mnt_children[2];
static size_t s_layered_mnt_count;

#define FS_ROOT_MAX 384

static const fs_mount_t *s_table = s_flat_table;
static size_t s_table_len = sizeof(s_flat_table) / sizeof(s_flat_table[0]);
static char s_roots[ROOT_COUNT][FS_ROOT_MAX] = { "flash", "flash", "flash", "", "" };
static char s_etc_seed[FS_ROOT_MAX + 8];
static bool s_layered = false;

static fmrb_err_t copy_root(char *dst, const char *src) {
    if (src == NULL || src[0] == '\0') {
        return FMRB_ERR_INVALID_PARAM;
    }
    int n = snprintf(dst, FS_ROOT_MAX, "%s", src);
    if (n < 0 || n >= FS_ROOT_MAX) {
        return FMRB_ERR_INVALID_PARAM;
    }
    // A trailing '/' would double up with the virtual path's leading one.
    while (n > 1 && dst[n - 1] == '/') {
        dst[--n] = '\0';
    }
    return FMRB_OK;
}

// A host place is optional: NULL or "" leaves it out (an empty root).
static fmrb_err_t copy_host_root(char *dst, const char *src) {
    if (src == NULL || src[0] == '\0') {
        dst[0] = '\0';
        return FMRB_OK;
    }
    if (src[0] != '/') {
        return FMRB_ERR_INVALID_PARAM;
    }
    return copy_root(dst, src);
}

fmrb_err_t fmrb_hal_file_set_layout(const fmrb_hal_file_layout_t *layout) {
    if (layout == NULL ||
        copy_root(s_roots[ROOT_SYSTEM], layout->system_root) != FMRB_OK ||
        copy_root(s_roots[ROOT_USER], layout->user_root) != FMRB_OK ||
        copy_root(s_roots[ROOT_CACHE], layout->cache_root) != FMRB_OK ||
        copy_host_root(s_roots[ROOT_HOST_HOME], layout->host_home) != FMRB_OK ||
        copy_host_root(s_roots[ROOT_HOST_FS], layout->host_root) != FMRB_OK) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (layout->etc_seed != NULL) {
        if (copy_root(s_etc_seed, layout->etc_seed) != FMRB_OK) {
            return FMRB_ERR_INVALID_PARAM;
        }
    } else {
        snprintf(s_etc_seed, sizeof(s_etc_seed), "%s/etc", s_roots[ROOT_SYSTEM]);
    }
    s_layered_mnt_count = 0;
    if (s_roots[ROOT_HOST_HOME][0] != '\0') s_layered_mnt_children[s_layered_mnt_count++] = "host";
    if (s_roots[ROOT_HOST_FS][0] != '\0') s_layered_mnt_children[s_layered_mnt_count++] = "root";
    static const char *const base[] = { "home", "etc", "var", "cache", "data", "tmp" };
    s_layered_root_count = 0;
    for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); i++) {
        s_layered_root_children[s_layered_root_count++] = base[i];
    }
    if (s_layered_mnt_count > 0) s_layered_root_children[s_layered_root_count++] = "mnt";
    s_table = s_layered_table;
    s_table_len = sizeof(s_layered_table) / sizeof(s_layered_table[0]);
    s_layered = true;
    return FMRB_OK;
}

// Collapse "//", "." and ".." in a virtual path, making it absolute. ".."
// stops at the root, so no virtual path can name a host path above the root
// it resolves under. Only the layered table normalizes: the flat one keeps
// passing paths through exactly as it always has.
static bool normalize_virtual(const char *in, char *out, size_t out_len) {
    if (out_len < 2) {
        return false;
    }
    size_t o = 0;
    out[o++] = '/';
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - seg);
        if (len == 1 && seg[0] == '.') {
            continue;
        }
        if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            while (o > 1 && out[o - 1] != '/') o--;   // drop the last segment
            if (o > 1) o--;                           // and its slash
            continue;
        }
        if (o > 1) {
            if (o + 1 >= out_len) return false;
            out[o++] = '/';
        }
        if (o + len >= out_len) return false;
        memcpy(out + o, seg, len);
        o += len;
    }
    out[o] = '\0';
    return true;
}

static bool top_matches(const char *path, const char *top) {
    size_t n = strlen(top);
    return strncmp(path, top, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

// The table entry a normalized virtual path falls under.
static const fs_mount_t *lookup_mount(const char *norm) {
    for (size_t i = 0; i < s_table_len; i++) {
        if (s_table[i].top[0] == '\0' || top_matches(norm, s_table[i].top)) {
            return &s_table[i];
        }
    }
    return &s_table[s_table_len - 1];
}

// The host path of a normalized virtual path under its entry. A host place
// drops its own top ("/mnt/host/a" -> "<home>/a"); every other root is a tree
// shaped like the namespace and takes the whole path.
static fmrb_err_t layered_host_path(const fs_mount_t *m, const char *norm,
                                    char *out, size_t out_len) {
    const char *root = s_roots[m->root];
    if (root[0] == '\0') {
        return FMRB_ERR_NOT_FOUND;
    }
    const char *rest = strcmp(norm, "/") == 0 ? "" : norm;
    if (m->host) {
        rest = norm + strlen(m->top);           // "" or "/..."
        if (strcmp(root, "/") == 0 && rest[0] != '\0') {
            root = "";                          // "/" + "/etc", not "//etc"
        }
    }
    int n = snprintf(out, out_len, "%s%s", root, rest);
    if (n < 0 || (size_t)n >= out_len) {
        return FMRB_ERR_INVALID_PARAM;
    }
    return FMRB_OK;
}

// Resolve a virtual path to the host path. The flat table keeps the shape the
// simulation has always had ("flash" + path, relative paths under flash/);
// the layered one normalizes first and then picks the root from the table.
// Under a host place, a path naming something other than a regular file or a
// directory (a device, a FIFO, a socket) does not resolve: opening one can
// block the whole cooperative machine or have effects of its own.
fmrb_err_t fmrb_hal_file_resolve_path(const char *virtual_path,
                                      char *out, size_t out_len) {
    if (virtual_path == NULL || out == NULL || out_len == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (!s_layered) {
        const char *base = s_roots[ROOT_SYSTEM];
        int n;
        if (virtual_path[0] == '/') {
            n = snprintf(out, out_len, "%s%s", base, virtual_path);
        } else {
            n = snprintf(out, out_len, "%s/%s", base, virtual_path);
        }
        if (n < 0 || (size_t)n >= out_len) {
            return FMRB_ERR_INVALID_PARAM;
        }
        return FMRB_OK;
    }
    char norm[512];
    if (!normalize_virtual(virtual_path, norm, sizeof(norm))) {
        return FMRB_ERR_INVALID_PARAM;
    }
    const fs_mount_t *m = lookup_mount(norm);
    fmrb_err_t err = layered_host_path(m, norm, out, out_len);
    if (err != FMRB_OK) {
        out[0] = '\0';
        return err;
    }
    if (m->host) {
        struct stat st;
        if (stat(out, &st) == 0 && !S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            out[0] = '\0';
            return FMRB_ERR_NOT_SUPPORTED;
        }
    }
    return FMRB_OK;
}

// Whether a real (symlink-free) path lies in or at the real root.
static bool real_inside(const char *real, const char *real_root) {
    size_t n = strlen(real_root);
    if (n == 1 && real_root[0] == '/') {
        return true;
    }
    return strncmp(real, real_root, n) == 0 && (real[n] == '\0' || real[n] == '/');
}

// Drop the last component of a host path in place ("/a/b" -> "/a",
// "/a" -> "/"). False when there is nothing left to drop.
static bool host_parent(char *path) {
    char *slash = strrchr(path, '/');
    if (slash == NULL || (slash == path && path[1] == '\0')) {
        return false;
    }
    if (slash == path) {
        path[1] = '\0';
    } else {
        *slash = '\0';
    }
    return true;
}

// A write under /mnt/host must land inside the home, wherever symbolic links
// point: the real location of what is written (follow: the file itself, so a
// link to a file outside is refused; otherwise its directory, for a delete,
// a rename or a new directory, which change the entry and not what a link
// points at) has to be inside the real home. A name that does not exist yet
// is judged by the nearest ancestor that does (the parents a create makes
// can only come below it). ".." never gets here: the virtual path is
// normalized first. Reading is not limited: a link may point anywhere,
// since "/" is readable at /mnt/root anyway.
static fmrb_err_t host_write_check(const fs_mount_t *m, const char *host, bool follow,
                                   const char **why) {
    char real_root[PATH_MAX];
    if (realpath(s_roots[m->root], real_root) == NULL) {
        *why = "the home directory is not there";
        return FMRB_ERR_NOT_FOUND;
    }
    char target[512];
    snprintf(target, sizeof(target), "%s", host);
    if (!follow) {
        if (strcmp(host, s_roots[m->root]) == 0) {
            *why = "the home directory itself cannot be removed or renamed";
            return FMRB_ERR_READ_ONLY;
        }
        if (!host_parent(target)) {
            *why = "no directory to write in";
            return FMRB_ERR_READ_ONLY;
        }
    }
    for (;;) {
        struct stat st;
        if (lstat(target, &st) == 0) {
            char real[PATH_MAX];
            if (realpath(target, real) == NULL) {
                *why = "a symbolic link that points nowhere";
                return FMRB_ERR_READ_ONLY;
            }
            if (!real_inside(real, real_root)) {
                *why = "it would land outside the home directory";
                return FMRB_ERR_READ_ONLY;
            }
            return FMRB_OK;
        }
        if (errno != ENOENT || !host_parent(target)) {
            *why = "cannot find where it would land";
            return FMRB_ERR_READ_ONLY;
        }
    }
}

// What a change to a virtual path may do: FMRB_OK, or the reason it is
// refused (FMRB_ERR_READ_ONLY for the distribution tree, /mnt/root and a
// write that would leave the home; why says which).
static fmrb_err_t check_write(const char *virtual_path, bool follow, const char **why) {
    *why = "";
    if (virtual_path == NULL || !s_layered) {
        return FMRB_OK;
    }
    char norm[512];
    if (!normalize_virtual(virtual_path, norm, sizeof(norm))) {
        *why = "path too long";
        return FMRB_ERR_INVALID_PARAM;
    }
    const fs_mount_t *m = lookup_mount(norm);
    if (m->read_only) {
        *why = m->host ? "Linux's files under /mnt/root are read-only" : "the distribution tree";
        return FMRB_ERR_READ_ONLY;
    }
    if (!m->host) {
        return FMRB_OK;
    }
    char host[512];
    fmrb_err_t err = layered_host_path(m, norm, host, sizeof(host));
    if (err != FMRB_OK) {
        *why = "not shown on this machine";
        return err == FMRB_ERR_NOT_FOUND ? FMRB_ERR_READ_ONLY : err;
    }
    return host_write_check(m, host, follow, why);
}

bool fmrb_hal_file_path_is_read_only(const char *virtual_path) {
    const char *why;
    return check_write(virtual_path, true, &why) != FMRB_OK;
}

// Whether a virtual path is under a host place (/mnt/host, /mnt/root).
static bool path_is_host(const char *virtual_path) {
    if (!s_layered || virtual_path == NULL) {
        return false;
    }
    char norm[512];
    return normalize_virtual(virtual_path, norm, sizeof(norm)) && lookup_mount(norm)->host;
}

// Internal alias for legacy callsites in this file.
static inline void build_path(const char *path, char *full_path, size_t max_len) {
    if (fmrb_hal_file_resolve_path(path, full_path, max_len) != FMRB_OK && max_len > 0) {
        full_path[0] = '\0';
    }
}

// Refuse a change the namespace does not allow (the read-only places, and a
// write that would leave the home under /mnt/host), and say why. follow:
// the change goes through a symbolic link at the path (writing a file,
// utime, chmod) rather than changing the entry itself (remove, rename,
// mkdir, rmdir).
#define REFUSE_CHANGE(path, follow) do {                                \
        const char *why_;                                               \
        fmrb_err_t err_ = check_write((path), (follow), &why_);         \
        if (err_ != FMRB_OK) {                                          \
            FMRB_LOGW(TAG, "%s: read-only (%s)", (path), why_);         \
            return err_;                                                \
        }                                                               \
    } while (0)

// The flat table has no SD card and no virtual mount points. The layered one
// lists the places it keeps outside the distribution tree under "/", "/app"
// and "/mnt", so a directory listing shows them whichever tree holds the rest.
const char *const *fmrb_hal_file_virtual_children(const char *virtual_path,
                                                  size_t *out_count) {
    if (s_layered && virtual_path != NULL) {
        char norm[512];
        if (normalize_virtual(virtual_path, norm, sizeof(norm))) {
            if (strcmp(norm, "/") == 0) {
                if (out_count) *out_count = s_layered_root_count;
                return s_layered_root_children;
            }
            if (strcmp(norm, "/app") == 0) {
                if (out_count) {
                    *out_count = sizeof(s_layered_app_children) / sizeof(s_layered_app_children[0]);
                }
                return s_layered_app_children;
            }
            if (strcmp(norm, "/mnt") == 0 && s_layered_mnt_count > 0) {
                if (out_count) *out_count = s_layered_mnt_count;
                return s_layered_mnt_children;
            }
        }
    }
    if (out_count) *out_count = 0;
    return NULL;
}

// Convert flags to POSIX mode string
static void flags_to_mode(uint32_t flags, char *mode) {
    if (flags & FMRB_O_RDWR) {
        if (flags & FMRB_O_CREAT) {
            if (flags & FMRB_O_TRUNC) {
                strcpy(mode, "w+");
            } else {
                strcpy(mode, "r+");
            }
        } else {
            strcpy(mode, "r+");
        }
    } else if (flags & FMRB_O_WRONLY) {
        if (flags & FMRB_O_APPEND) {
            strcpy(mode, "a");
        } else if (flags & FMRB_O_TRUNC) {
            strcpy(mode, "w");
        } else {
            strcpy(mode, "r+");
        }
    } else {
        strcpy(mode, "r");
    }

    // Add binary mode
    strcat(mode, "b");
}

// fopen for Linux's own files: the same modes, but the descriptor is opened
// with O_NONBLOCK and O_NOCTTY and has to be a regular file once open (the
// resolver already turned devices, FIFOs and sockets away by name; this
// closes the gap of one swapped in between). O_NONBLOCK also keeps a procfs
// file that waits for data (kmsg) from stopping the cooperative machine.
static FILE *host_fopen(const char *host, const char *mode) {
    int oflags;
    bool plus = strchr(mode, '+') != NULL;
    switch (mode[0]) {
    case 'r': oflags = plus ? O_RDWR : O_RDONLY; break;
    case 'w': oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
    case 'a': oflags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
    default: return NULL;
    }
    int fd = open(host, oflags | O_NONBLOCK | O_NOCTTY | O_CLOEXEC, 0666);
    if (fd < 0) {
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return NULL;
    }
    FILE *fp = fdopen(fd, mode);
    if (fp == NULL) {
        close(fd);
    }
    return fp;
}

// Helper function to create directory recursively
static int mkdir_recursive(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

// Copy one host directory tree into another (the first start's /etc).
// Bounded depth; regular files and directories only.
static int copy_tree(const char *src, const char *dst, int depth) {
    if (depth > 8) {
        return -1;
    }
    if (mkdir_recursive(dst) != 0) {
        return -1;
    }
    DIR *dir = opendir(src);
    if (dir == NULL) {
        return -1;
    }
    int err = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char from[1024];
        char to[1024];
        snprintf(from, sizeof(from), "%s/%s", src, ent->d_name);
        snprintf(to, sizeof(to), "%s/%s", dst, ent->d_name);
        struct stat st;
        if (stat(from, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_tree(from, to, depth + 1) != 0) err = -1;
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        FILE *in = fopen(from, "rb");
        FILE *out = in ? fopen(to, "wb") : NULL;
        if (in == NULL || out == NULL) {
            err = -1;
        } else {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                if (fwrite(buf, 1, n, out) != n) {
                    err = -1;
                    break;
                }
            }
        }
        if (in) fclose(in);
        if (out) fclose(out);
    }
    closedir(dir);
    return err;
}

// Make the directory a virtual path resolves to (an empty store's /home).
static void ensure_virtual_dir(const char *virtual_path) {
    char host[512];
    build_path(virtual_path, host, sizeof(host));
    if (host[0] != '\0' && mkdir_recursive(host) != 0) {
        FMRB_LOGW(TAG, "cannot create %s: %s", host, strerror(errno));
    }
}

// The layered table's first start: the roots outside the distribution tree
// are made, and /etc is filled from the seed when the user has none yet.
// Settings added by a later release are not merged in here (the packaging
// stage does that, from system_conf.factory.toml).
static fmrb_err_t init_layered_roots(void) {
    if (mkdir_recursive(s_roots[ROOT_USER]) != 0 || mkdir_recursive(s_roots[ROOT_CACHE]) != 0) {
        FMRB_LOGE(TAG, "cannot create the user (%s) or cache (%s) root: %s",
                  s_roots[ROOT_USER], s_roots[ROOT_CACHE], strerror(errno));
        return FMRB_ERR_FAILED;
    }
    char etc[512];
    build_path("/etc", etc, sizeof(etc));
    struct stat st;
    if (stat(etc, &st) != 0) {
        if (copy_tree(s_etc_seed, etc, 0) == 0) {
            FMRB_LOGI(TAG, "first start: %s copied from %s", etc, s_etc_seed);
        } else {
            FMRB_LOGW(TAG, "first start: copying %s to %s failed", s_etc_seed, etc);
        }
    }
    ensure_virtual_dir("/app/usr");
    ensure_virtual_dir("/var");
    ensure_virtual_dir("/cache");
    ensure_virtual_dir("/data");
    FMRB_LOGI(TAG, "namespace: system=%s (read-only) user=%s cache=%s",
              s_roots[ROOT_SYSTEM], s_roots[ROOT_USER], s_roots[ROOT_CACHE]);
    FMRB_LOGI(TAG, "namespace: /mnt/host=%s /mnt/root=%s (read-only)",
              s_roots[ROOT_HOST_HOME][0] ? s_roots[ROOT_HOST_HOME] : "(not shown)",
              s_roots[ROOT_HOST_FS][0] ? s_roots[ROOT_HOST_FS] : "(not shown)");
    return FMRB_OK;
}

// Initialize file system
fmrb_err_t fmrb_hal_file_init(void) {
    // Create mutex for file operations
    s_file_mutex = fmrb_semaphore_create_mutex();
    if (s_file_mutex == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }

    if (s_layered) {
        if (init_layered_roots() != FMRB_OK) {
            fmrb_semaphore_delete(s_file_mutex);
            s_file_mutex = NULL;
            return FMRB_ERR_FAILED;
        }
    } else if (mkdir_recursive(s_roots[ROOT_SYSTEM]) != 0 && errno != EEXIST) {
        // Create base directory if it doesn't exist
        fmrb_semaphore_delete(s_file_mutex);
        s_file_mutex = NULL;
        return FMRB_ERR_FAILED;
    }

    // /home is the user's own directory and ships empty, so nothing in the
    // storage image creates it. Make it here rather than at each writer: a
    // user who deletes it gets it back on the next boot too.
    char home[512];
    build_path("/home", home, sizeof(home));
    if (mkdir(home, 0755) != 0 && errno != EEXIST) {
        FMRB_LOGW(TAG, "cannot create %s: %s", home, strerror(errno));
    }

    // /tmp: same mount the device keeps in RAM, emptied here to match.
    fmrb_tmpfs_init();
    return FMRB_OK;
}

// Deinitialize file system
void fmrb_hal_file_deinit(void) {
    if (s_file_mutex != NULL) {
        fmrb_semaphore_delete(s_file_mutex);
        s_file_mutex = NULL;
    }
}

// Open a file
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (flags & (FMRB_O_WRONLY | FMRB_O_RDWR | FMRB_O_CREAT | FMRB_O_TRUNC | FMRB_O_APPEND)) {
        REFUSE_CHANGE(path, true);
    }

    LOCK();

    fmrb_file_handle_t *handle = fmrb_sys_calloc(1, sizeof(fmrb_file_handle_t));
    if (handle == NULL) {
        UNLOCK();
        return FMRB_ERR_NO_MEMORY;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    // The device's /tmp is a flat store, so refuse a subdirectory here too
    // rather than let the simulator accept what hardware cannot.
    if (fmrb_tmpfs_path_too_deep(full_path)) {
        fmrb_sys_free(handle);
        UNLOCK();
        FMRB_LOGW(TAG, "%s: /tmp holds files, not subdirectories", path);
        return FMRB_ERR_INVALID_PARAM;
    }
    handle->in_tmp = fmrb_tmpfs_owns_path(full_path);
    handle->counted = handle->in_tmp ? fmrb_tmpfs_file_size(full_path) : 0;

    // Create parent directory if needed
    if (flags & FMRB_O_CREAT) {
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "%s", full_path);
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            mkdir_recursive(dir_path);
        }
    }

    char mode[8];
    flags_to_mode(flags, mode);

    handle->fp = path_is_host(path) ? host_fopen(full_path, mode) : fopen(full_path, mode);
    if (handle->fp == NULL) {
        fmrb_sys_free(handle);
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    // Truncating hands the old contents back to the mount.
    if (handle->in_tmp && (flags & FMRB_O_TRUNC) && handle->counted > 0) {
        fmrb_tmpfs_release(handle->counted);
        handle->counted = 0;
    }

    handle->owner = fmrb_task_get_current();
    HANDLE_LIST_ADD(s_open_files, handle);
    *out_handle = handle;
    UNLOCK();
    return FMRB_OK;
}

// Close a file
fmrb_err_t fmrb_hal_file_close(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Standard streams cannot be closed
    if (is_std_stream(handle)) {
        return FMRB_OK;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    HANDLE_LIST_REMOVE(s_open_files, fh);
    fclose(fh->fp);
    fmrb_sys_free(fh);
    UNLOCK();
    return FMRB_OK;
}

// Read from file
fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Handle standard streams directly
    if (is_std_stream(handle)) {
        FILE *fp = get_std_stream_fp(handle);
        if (fp == NULL) {
            return FMRB_ERR_INVALID_PARAM;
        }
        size_t n = fread(buffer, 1, size, fp);
        if (bytes_read != NULL) {
            *bytes_read = n;
        }
        return FMRB_OK;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    size_t n = fread(buffer, 1, size, fh->fp);
    if (bytes_read != NULL) {
        *bytes_read = n;
    }
    UNLOCK();
    return FMRB_OK;
}

// Write to file
fmrb_err_t fmrb_hal_file_write(fmrb_file_t handle, const void *buffer, size_t size, size_t *bytes_written) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Handle standard streams directly
    if (is_std_stream(handle)) {
        FILE *fp = get_std_stream_fp(handle);
        if (fp == NULL) {
            return FMRB_ERR_INVALID_PARAM;
        }
        size_t n = fwrite(buffer, 1, size, fp);
        fflush(fp);  // Flush immediately for standard streams
        if (bytes_written != NULL) {
            *bytes_written = n;
        }
        return (n == size) ? FMRB_OK : FMRB_ERR_FAILED;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;

    // /tmp is capped on device; refuse the write here at the same point rather
    // than let it succeed in the simulator and fail on hardware.
    if (fh->in_tmp && size > 0) {
        long pos = ftell(fh->fp);
        size_t end = (pos < 0 ? 0 : (size_t)pos) + size;
        if (end > fh->counted) {
            size_t delta = end - fh->counted;
            if (!fmrb_tmpfs_charge(delta)) {
                if (bytes_written != NULL) {
                    *bytes_written = 0;
                }
                UNLOCK();
                FMRB_LOGW(TAG, "%s is full: %zu more bytes refused",
                          FMRB_TMPFS_MOUNT, delta);
                return FMRB_ERR_NO_MEMORY;
            }
            fh->counted = end;
        }
    }

    size_t n = fwrite(buffer, 1, size, fh->fp);
    if (bytes_written != NULL) {
        *bytes_written = n;
    }
    UNLOCK();
    return (n == size) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Seek to position in file
fmrb_err_t fmrb_hal_file_seek(fmrb_file_t handle, int32_t offset, fmrb_seek_mode_t mode) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    int whence;
    switch (mode) {
        case FMRB_SEEK_SET: whence = SEEK_SET; break;
        case FMRB_SEEK_CUR: whence = SEEK_CUR; break;
        case FMRB_SEEK_END: whence = SEEK_END; break;
        default: return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    int ret = fseek(fh->fp, offset, whence);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current position in file
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t handle, uint32_t *position) {
    if (handle == NULL || position == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    long pos = ftell(fh->fp);
    UNLOCK();

    if (pos < 0) {
        return FMRB_ERR_FAILED;
    }

    *position = (uint32_t)pos;
    return FMRB_OK;
}

// Delete a file
fmrb_err_t fmrb_hal_file_remove(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    REFUSE_CHANGE(path, false);

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    size_t freed = fmrb_tmpfs_owns_path(full_path) ? fmrb_tmpfs_file_size(full_path) : 0;
    int ret = unlink(full_path);
    if (ret == 0 && freed > 0) {
        fmrb_tmpfs_release(freed);
    }
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Rename/move a file
fmrb_err_t fmrb_hal_file_rename(const char *old_path, const char *new_path) {
    if (old_path == NULL || new_path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    REFUSE_CHANGE(old_path, false);
    REFUSE_CHANGE(new_path, false);

    char old_full_path[512];
    char new_full_path[512];
    build_path(old_path, old_full_path, sizeof(old_full_path));
    build_path(new_path, new_full_path, sizeof(new_full_path));

    if (fmrb_tmpfs_path_too_deep(new_full_path)) {
        FMRB_LOGW(TAG, "%s: /tmp holds files, not subdirectories", new_path);
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    // Moving in or out of /tmp moves the bytes on and off the mount's books.
    bool from_tmp = fmrb_tmpfs_owns_path(old_full_path);
    bool to_tmp = fmrb_tmpfs_owns_path(new_full_path);
    size_t bytes = (from_tmp || to_tmp) ? fmrb_tmpfs_file_size(old_full_path) : 0;
    if (to_tmp && !from_tmp && !fmrb_tmpfs_charge(bytes)) {
        UNLOCK();
        FMRB_LOGW(TAG, "%s is full: cannot move %s in", FMRB_TMPFS_MOUNT, old_path);
        return FMRB_ERR_NO_MEMORY;
    }
    size_t overwritten = to_tmp ? fmrb_tmpfs_file_size(new_full_path) : 0;
    int ret = rename(old_full_path, new_full_path);
    if (ret == 0) {
        if (overwritten > 0) {
            fmrb_tmpfs_release(overwritten);
        }
        if (from_tmp && !to_tmp) {
            fmrb_tmpfs_release(bytes);
        }
    } else if (to_tmp && !from_tmp) {
        fmrb_tmpfs_release(bytes);
    }
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get file information
fmrb_err_t fmrb_hal_file_stat(const char *path, fmrb_file_info_t *info) {
    if (path == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    struct stat st;
    int ret = stat(full_path, &st);
    UNLOCK();

    if (ret != 0) {
        return FMRB_ERR_FAILED;
    }

    memset(info, 0, sizeof(fmrb_file_info_t));

    // Extract basename from path
    const char *basename = strrchr(path, '/');
    if (basename != NULL) {
        basename++;
    } else {
        basename = path;
    }
    snprintf(info->name, sizeof(info->name), "%s", basename);

    // Copy POSIX st_mode directly (FMRB_S_* macros are POSIX-compatible)
    info->mode = st.st_mode;
    info->size = st.st_size;
    info->is_dir = S_ISDIR(st.st_mode);  // Keep for backward compatibility
    info->mtime = st.st_mtime;

    return FMRB_OK;
}

// Create a directory
fmrb_err_t fmrb_hal_file_mkdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    REFUSE_CHANGE(path, false);

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    // The device's /tmp is flat, so nothing may be created inside it.
    if (fmrb_tmpfs_owns_path(full_path) && strcmp(full_path, fmrb_tmpfs_host_dir()) != 0) {
        FMRB_LOGW(TAG, "%s: /tmp holds files, not subdirectories", path);
        return FMRB_ERR_NOT_SUPPORTED;
    }

    LOCK();
    int ret = mkdir_recursive(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Remove a directory
fmrb_err_t fmrb_hal_file_rmdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    REFUSE_CHANGE(path, false);

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = rmdir(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Open a directory for reading
fmrb_err_t fmrb_hal_file_opendir(const char *path, fmrb_dir_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    fmrb_dir_handle_t *handle = fmrb_sys_malloc(sizeof(fmrb_dir_handle_t));
    if (handle == NULL) {
        UNLOCK();
        return FMRB_ERR_NO_MEMORY;
    }

    handle->dir = opendir(full_path);
    if (handle->dir == NULL) {
        fmrb_sys_free(handle);
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    // Store directory path for use in readdir
    strncpy(handle->dir_path, full_path, sizeof(handle->dir_path) - 1);
    handle->dir_path[sizeof(handle->dir_path) - 1] = '\0';

    handle->owner = fmrb_task_get_current();
    HANDLE_LIST_ADD(s_open_dirs, handle);
    *out_handle = handle;
    UNLOCK();
    return FMRB_OK;
}

// Close a directory
fmrb_err_t fmrb_hal_file_closedir(fmrb_dir_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_dir_handle_t *dh = (fmrb_dir_handle_t *)handle;
    HANDLE_LIST_REMOVE(s_open_dirs, dh);
    closedir(dh->dir);
    fmrb_sys_free(dh);
    UNLOCK();
    return FMRB_OK;
}

// Read next entry from directory
fmrb_err_t fmrb_hal_file_readdir(fmrb_dir_t handle, fmrb_file_info_t *info) {
    if (handle == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_dir_handle_t *dh = (fmrb_dir_handle_t *)handle;
    struct dirent *entry = readdir(dh->dir);
    UNLOCK();

    if (entry == NULL) {
        return FMRB_ERR_NOT_SUPPORTED;  // No more entries
    }

    memset(info, 0, sizeof(fmrb_file_info_t));
    snprintf(info->name, sizeof(info->name), "%s", entry->d_name);
    info->is_dir = (entry->d_type == DT_DIR);

    // Get file size, mtime, and mode using stat()
    char entry_path[1024];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", dh->dir_path, entry->d_name);

    struct stat st;
    if (stat(entry_path, &st) == 0) {
        info->mode = st.st_mode;
        // For directories, show size as 0 (directory metadata size is not meaningful for users)
        info->size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
        info->mtime = st.st_mtime;
    } else {
        info->mode = 0;
        info->size = 0;
        info->mtime = 0;
    }

    return FMRB_OK;
}

// Flush file buffers to storage
fmrb_err_t fmrb_hal_file_sync(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    int ret = fflush(fh->fp);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get file size
fmrb_err_t fmrb_hal_file_size(fmrb_file_t handle, uint32_t *size) {
    if (handle == NULL || size == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    long current = ftell(fh->fp);
    if (current < 0) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    if (fseek(fh->fp, 0, SEEK_END) != 0) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    long file_size = ftell(fh->fp);
    fseek(fh->fp, current, SEEK_SET);  // Restore position
    UNLOCK();

    if (file_size < 0) {
        return FMRB_ERR_FAILED;
    }

    *size = (uint32_t)file_size;
    return FMRB_OK;
}

// Change current working directory
fmrb_err_t fmrb_hal_file_chdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = chdir(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current working directory
fmrb_err_t fmrb_hal_file_getcwd(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    char *ret = getcwd(buffer, size);
    UNLOCK();
    return (ret != NULL) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Change file modification time
fmrb_err_t fmrb_hal_file_utime(const char *path, uint32_t mtime) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    REFUSE_CHANGE(path, true);

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    struct utimbuf times;
    times.actime = mtime;
    times.modtime = mtime;

    LOCK();
    int ret = utime(full_path, &times);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Change file attributes/permissions
fmrb_err_t fmrb_hal_file_chmod(const char *path, uint32_t attr) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    REFUSE_CHANGE(path, true);

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    // On POSIX, attr is mode_t
    LOCK();
    int ret = chmod(full_path, (mode_t)attr);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get filesystem statistics
fmrb_err_t fmrb_hal_file_statfs(const char *path, uint64_t *total_bytes, uint64_t *free_bytes) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    struct statvfs stat;
    LOCK();
    int ret = statvfs(full_path, &stat);
    UNLOCK();

    if (ret != 0) {
        return FMRB_ERR_FAILED;
    }

    if (total_bytes != NULL) {
        *total_bytes = (uint64_t)stat.f_blocks * stat.f_frsize;
    }
    if (free_bytes != NULL) {
        *free_bytes = (uint64_t)stat.f_bavail * stat.f_frsize;
    }

    return FMRB_OK;
}

// Format filesystem - not supported on POSIX
fmrb_err_t fmrb_hal_file_mkfs(const char *path) {
    (void)path;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Get volume label - not supported on POSIX
fmrb_err_t fmrb_hal_file_getlabel(const char *path, char *label) {
    (void)path;
    (void)label;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Set volume label - not supported on POSIX
fmrb_err_t fmrb_hal_file_setlabel(const char *path, const char *label) {
    (void)path;
    (void)label;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Get sector size - return 512 as default
uint32_t fmrb_hal_file_sector_size(void) {
    return 512;  // Standard sector size
}

// Get physical address - not supported on POSIX
fmrb_err_t fmrb_hal_file_physical_address(fmrb_file_t handle, uintptr_t *addr) {
    (void)handle;
    (void)addr;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Erase storage volume - not supported on POSIX
fmrb_err_t fmrb_hal_file_erase(const char *volume) {
    (void)volume;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Check if file is stored contiguously - not supported on POSIX
fmrb_err_t fmrb_hal_file_is_contiguous(const char *path, bool *is_contiguous) {
    (void)path;
    (void)is_contiguous;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Mount filesystem - not needed on POSIX
fmrb_err_t fmrb_hal_file_mount(const char *path) {
    (void)path;
    return FMRB_ERR_NOT_SUPPORTED;  // Auto-mounted
}

// Unmount filesystem - not needed on POSIX
fmrb_err_t fmrb_hal_file_unmount(const char *path) {
    (void)path;
    return FMRB_ERR_NOT_SUPPORTED;  // Auto-mounted
}

bool fmrb_hal_file_lock(uint32_t timeout_ms) {
    if (s_file_mutex == NULL) {
        return false;
    }
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);
    return fmrb_semaphore_take(s_file_mutex, ticks) == FMRB_TRUE;
}

void fmrb_hal_file_unlock(void) {
    UNLOCK();
}

size_t fmrb_hal_file_close_owned_locked(void *owner) {
    size_t closed = 0;
    if (owner == NULL) {
        return 0;
    }
    fmrb_file_handle_t *fh = s_open_files;
    while (fh != NULL) {
        fmrb_file_handle_t *next = fh->next;
        if (fh->owner == owner) {
            HANDLE_LIST_REMOVE(s_open_files, fh);
            fclose(fh->fp);
            fmrb_sys_free(fh);
            closed++;
        }
        fh = next;
    }
    fmrb_dir_handle_t *dh = s_open_dirs;
    while (dh != NULL) {
        fmrb_dir_handle_t *next = dh->next;
        if (dh->owner == owner) {
            HANDLE_LIST_REMOVE(s_open_dirs, dh);
            closedir(dh->dir);
            fmrb_sys_free(dh);
            closed++;
        }
        dh = next;
    }
    return closed;
}
