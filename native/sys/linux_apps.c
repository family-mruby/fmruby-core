/*
 * linux_apps.c - the Linux PC's applications for the launcher (linux_apps.h,
 * doc/linux_native L9b).
 *
 * Three kinds of callers, three rules:
 *   - the reader thread (one at a time, detached) builds a new list of
 *     entries and swaps it in under the lock;
 *   - Family mruby's tasks (the desktop) copy the list out and queue starts,
 *     under the lock, never waiting on anything but it;
 *   - the compositor's main thread takes the queue, forks, reaps and keeps
 *     the record of what it started (only it touches the records, so they
 *     take no lock).
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if NATIVE_HAVE_PNG
#include <png.h>
#endif

#include "linux_apps.h"

extern char **environ;

#define MAX_ENTRIES 1024
#define MAX_FILE_BYTES (64 * 1024)
#define MAX_DEPTH 3
#define QUEUE_MAX 8
#define RECORDS_MAX 64
#define ENV_MAX 512
#define NAME_MAX_BYTES 128

typedef struct {
    char *id;           /* desktop file id ("foot.desktop") */
    char *name;         /* Name, or Name[<lang>] */
    char *key;          /* StartupWMClass, else the program's file name */
    char *idkey;        /* the id without ".desktop" (Wayland app ids follow it) */
    char *exe;          /* the program, an absolute path */
    char **argv;        /* as Exec gives it (argv[0] as written) */
    char *dir;          /* Path=, or NULL */
    char *icon;         /* the icon BMP as a Family mruby path, or NULL */
} entry_t;

typedef struct {
    char **argv;
    char *exe, *dir, *name, *key, *idkey;
} launch_t;

typedef struct {
    bool used;
    bool reaped;
    pid_t pid;          /* the child; also its session id */
    char name[NAME_MAX_BYTES];
    char key[64];
    char idkey[128];
} record_t;

static struct {
    pthread_mutex_t lock;
    bool started;
    bool scanning;      /* a reader thread runs */
    bool scan_req;      /* read again when it is done */
    bool ready;         /* a list has been read */
    entry_t *e;
    int n;
    char lang[16];      /* the read list's */
    char want_lang[16];
    char icon_dir[512];
    char icon_fm_dir[256];
    char desktop[64];
    char mark_fm[300];
    launch_t q[QUEUE_MAX];
    int qn;
} s = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* The compositor's main thread only. */
static record_t s_rec[RECORDS_MAX];
static pid_t s_last_pid;
static char *s_envp[ENV_MAX + 2];
static char s_env_desktop[96];

/* --------------------------------------------------------------- helpers */

static char *dup_str(const char *a)
{
    return a ? strdup(a) : NULL;
}

static void free_entry(entry_t *e)
{
    free(e->id);
    free(e->name);
    free(e->key);
    free(e->idkey);
    free(e->exe);
    free(e->dir);
    free(e->icon);
    linux_apps_free_argv(e->argv);
    memset(e, 0, sizeof(*e));
}

static void free_list(entry_t *e, int n)
{
    for (int i = 0; i < n; i++) free_entry(&e[i]);
    free(e);
}

void linux_apps_free_argv(char **argv)
{
    if (argv == NULL) return;
    for (char **a = argv; *a != NULL; a++) free(*a);
    free(argv);
}

static char **copy_argv(char *const *argv)
{
    int n = 0;
    while (argv[n] != NULL) n++;
    char **out = calloc((size_t)n + 1, sizeof(char *));
    if (out == NULL) return NULL;
    for (int i = 0; i < n; i++) {
        out[i] = strdup(argv[i]);
        if (out[i] == NULL) {
            linux_apps_free_argv(out);
            return NULL;
        }
    }
    return out;
}

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void sleep_ms(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Copy at most cap - 1 bytes, not cutting a UTF-8 character in two. */
static void copy_utf8(char *out, size_t cap, const char *in)
{
    if (cap == 0) return;
    size_t n = strlen(in);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)in[n] & 0xC0) == 0x80) n--;
    }
    memcpy(out, in, n);
    out[n] = '\0';
}

static bool is_exec_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

/* A program by name: a path as it is, else searched for in PATH. */
static bool find_program(const char *name, char *out, size_t cap)
{
    if (name == NULL || name[0] == '\0') return false;
    if (strchr(name, '/') != NULL) {
        if (!is_exec_file(name)) return false;
        snprintf(out, cap, "%s", name);
        return true;
    }
    const char *path = getenv("PATH");
    if (path == NULL || path[0] == '\0') path = "/usr/local/bin:/usr/bin:/bin";
    const char *p = path;
    while (*p) {
        const char *end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0) {
            char cand[1024];
            snprintf(cand, sizeof(cand), "%.*s/%s", (int)len, p, name);
            if (is_exec_file(cand)) {
                snprintf(out, cap, "%s", cand);
                return true;
            }
        }
        if (end == NULL) break;
        p = end + 1;
    }
    return false;
}

/* snprintf for paths: false when cut short (the caller skips that path). */
static bool pathf(char *out, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static bool pathf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n >= 0 && (size_t)n < cap;
}

static const char *base_name(const char *path)
{
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

static bool same_word(const char *a, const char *b)
{
    return a != NULL && b != NULL && a[0] != '\0' && strcasecmp(a, b) == 0;
}

/* ------------------------------------------------- the Exec field (spec) */

/* The string escapes of a value: \s \n \t \r \\ (in place). */
static void unescape_value(char *v)
{
    char *w = v;
    for (char *r = v; *r; r++) {
        if (r[0] == '\\' && r[1] != '\0') {
            r++;
            switch (*r) {
            case 's': *w++ = ' '; break;
            case 'n': *w++ = '\n'; break;
            case 't': *w++ = '\t'; break;
            case 'r': *w++ = '\r'; break;
            case '\\': *w++ = '\\'; break;
            default: *w++ = '\\'; *w++ = *r; break;
            }
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

typedef struct {
    char **v;
    int n, cap;
} vec_t;

static bool vec_push(vec_t *a, const char *s, size_t len)
{
    if (a->n + 2 > a->cap) {
        int cap = a->cap ? a->cap * 2 : 8;
        char **v = realloc(a->v, (size_t)cap * sizeof(char *));
        if (v == NULL) return false;
        a->v = v;
        a->cap = cap;
    }
    char *c = malloc(len + 1);
    if (c == NULL) return false;
    memcpy(c, s, len);
    c[len] = '\0';
    a->v[a->n++] = c;
    a->v[a->n] = NULL;
    return true;
}

static bool is_drop_code(char c)
{
    /* Files and URLs (none are passed), and the deprecated codes. */
    return strchr("fFuUdDnNvm", c) != NULL;
}

/* One unquoted argument with its field codes: whole-argument codes are
 * dropped or expanded to whole arguments; inside an argument they expand to
 * nothing ("%%" to "%"). */
static bool push_unquoted(vec_t *a, const char *tok, const char *name, const char *icon, const char *file)
{
    if (tok[0] == '%' && tok[1] != '\0' && tok[2] == '\0') {
        char c = tok[1];
        if (is_drop_code(c)) return true;
        if (c == 'i') {
            if (icon == NULL || icon[0] == '\0') return true;
            return vec_push(a, "--icon", 6) && vec_push(a, icon, strlen(icon));
        }
        if (c == 'c') return vec_push(a, name ? name : "", name ? strlen(name) : 0);
        if (c == 'k') return vec_push(a, file ? file : "", file ? strlen(file) : 0);
    }
    size_t n = strlen(tok);
    char *out = malloc(n + 1);
    if (out == NULL) return false;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (tok[i] == '%' && i + 1 < n) {
            i++;
            if (tok[i] == '%') out[w++] = '%';
            continue;
        }
        out[w++] = tok[i];
    }
    bool ok = vec_push(a, out, w);
    free(out);
    return ok;
}

char **linux_apps_split_exec(const char *exec, const char *name, const char *icon, const char *file)
{
    if (exec == NULL) return NULL;
    vec_t a = { 0 };
    size_t len = strlen(exec);
    char *tok = malloc(len + 1);
    if (tok == NULL) return NULL;
    size_t i = 0;
    bool ok = true;
    while (ok) {
        while (i < len && (exec[i] == ' ' || exec[i] == '\t' || exec[i] == '\n')) i++;
        if (i >= len) break;
        size_t w = 0;
        bool quoted = false;
        while (i < len && exec[i] != ' ' && exec[i] != '\t' && exec[i] != '\n') {
            if (exec[i] == '"') {
                quoted = true;
                i++;
                while (i < len && exec[i] != '"') {
                    if (exec[i] == '\\' && i + 1 < len && strchr("\"`$\\", exec[i + 1]) != NULL) i++;
                    tok[w++] = exec[i++];
                }
                if (i < len) i++;   /* the closing quote */
            } else {
                tok[w++] = exec[i++];
            }
        }
        tok[w] = '\0';
        if (quoted) {
            ok = vec_push(&a, tok, w);
        } else {
            ok = push_unquoted(&a, tok, name, icon, file);
        }
    }
    free(tok);
    if (!ok || a.n == 0) {
        linux_apps_free_argv(a.v);
        return NULL;
    }
    return a.v;
}

/* -------------------------------------------------------------- icons */

/* RGB332, black kept off 0 (the sprites' transparent colour) and 0x01 (the
 * compositor's hole colour; never a visible colour). */
static uint8_t rgb332(unsigned r, unsigned g, unsigned b)
{
    uint8_t v = (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
    if (v == 0x00) v = 0x25;
    if (v == 0x01) v = 0x02;
    return v;
}

/* An 8-bit BMP whose pixel bytes are RGB332 (the sprite loader reads them as
 * colours; the palette is there for image viewers), as tool/gen_icon_bmp.rb
 * writes them. Written to a temporary name and renamed into place. */
static bool write_bmp(const char *path, const uint8_t *px, int w, int h)
{
    int pad = (4 - (w % 4)) % 4;
    uint32_t pal_bytes = 256 * 4;
    uint32_t body = (uint32_t)((w + pad) * h);
    uint32_t off = 14 + 40 + pal_bytes;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    uint32_t total = off + body;
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(&hdr[2], &total, 4);
    memcpy(&hdr[10], &off, 4);
    uint32_t dib = 40;
    int32_t iw = w, ih = h;
    uint16_t planes = 1, bpp = 8;
    uint32_t ncol = 256, res = 2835;
    memcpy(&hdr[14], &dib, 4);
    memcpy(&hdr[18], &iw, 4);
    memcpy(&hdr[22], &ih, 4);
    memcpy(&hdr[26], &planes, 2);
    memcpy(&hdr[28], &bpp, 2);
    memcpy(&hdr[34], &body, 4);
    memcpy(&hdr[38], &res, 4);
    memcpy(&hdr[42], &res, 4);
    memcpy(&hdr[46], &ncol, 4);
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp%ld", path, (long)getpid());
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) return false;
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    for (int v = 0; v < 256 && ok; v++) {
        uint8_t e[4] = { (uint8_t)((v & 0x03) * 255 / 3), (uint8_t)(((v >> 2) & 0x07) * 255 / 7),
                         (uint8_t)(((v >> 5) & 0x07) * 255 / 7), 0 };
        ok = fwrite(e, 1, 4, f) == 4;
    }
    static const uint8_t zeros[4] = { 0 };
    for (int y = h - 1; y >= 0 && ok; y--) {
        ok = fwrite(px + (size_t)y * (size_t)w, 1, (size_t)w, f) == (size_t)w;
        if (ok && pad) ok = fwrite(zeros, 1, (size_t)pad, f) == (size_t)pad;
    }
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

/* The group's own mark: a small terminal window. */
static void draw_mark(uint8_t *px)
{
    const int n = LINUX_APPS_ICON_PX;
    memset(px, 0, (size_t)n * n);
    for (int y = 3; y <= 20; y++) {
        for (int x = 1; x <= 22; x++) {
            uint8_t c = 0x25;
            if (y <= 6) c = 0x4B;
            if (x == 1 || x == 22 || y == 3 || y == 20) c = 0x92;
            px[y * n + x] = c;
        }
    }
    /* ">" and "_" */
    static const int prompt[][2] = { { 4, 9 }, { 5, 10 }, { 6, 11 }, { 5, 12 }, { 4, 13 } };
    for (size_t i = 0; i < sizeof(prompt) / sizeof(prompt[0]); i++) {
        px[prompt[i][1] * n + prompt[i][0]] = 0x1C;
        px[prompt[i][1] * n + prompt[i][0] + 1] = 0x1C;
    }
    for (int x = 9; x <= 13; x++) px[14 * n + x] = 0xFF;
}

#if NATIVE_HAVE_PNG
/* A PNG fitted into the icon square (aspect kept, centred): each icon pixel
 * is the alpha-weighted mean of the source pixels under it, and opaque when
 * at least half covered. */
static bool png_to_icon(const char *src, uint8_t *px)
{
    const int n = LINUX_APPS_ICON_PX;
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, src)) return false;
    if (img.width == 0 || img.height == 0 || img.width > 2048 || img.height > 2048) {
        png_image_free(&img);
        return false;
    }
    img.format = PNG_FORMAT_RGBA;
    size_t size = PNG_IMAGE_SIZE(img);
    uint8_t *rgba = malloc(size);
    if (rgba == NULL) {
        png_image_free(&img);
        return false;
    }
    if (!png_image_finish_read(&img, NULL, rgba, 0, NULL)) {
        free(rgba);
        return false;
    }
    int w = (int)img.width, h = (int)img.height;
    int big = w > h ? w : h;
    int dw = w * n / big, dh = h * n / big;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (n - dw) / 2, oy = (n - dh) / 2;
    memset(px, 0, (size_t)n * n);
    for (int y = 0; y < dh; y++) {
        int sy0 = y * h / dh, sy1 = (y + 1) * h / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * w / dw, sx1 = (x + 1) * w / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint64_t r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int sy = sy0; sy < sy1 && sy < h; sy++) {
                const uint8_t *row = rgba + (size_t)sy * (size_t)w * 4;
                for (int sx = sx0; sx < sx1 && sx < w; sx++) {
                    const uint8_t *p = row + (size_t)sx * 4;
                    r += (uint64_t)p[0] * p[3];
                    g += (uint64_t)p[1] * p[3];
                    b += (uint64_t)p[2] * p[3];
                    a += p[3];
                    cnt++;
                }
            }
            if (cnt == 0 || a == 0 || a / cnt < 128) continue;
            px[(oy + y) * n + ox + x] = rgb332((unsigned)(r / a), (unsigned)(g / a), (unsigned)(b / a));
        }
    }
    free(rgba);
    return true;
}
#endif

static bool has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* The data directories, most important first: $XDG_DATA_HOME (or
 * ~/.local/share), then $XDG_DATA_DIRS (or /usr/local/share:/usr/share). */
static int data_dirs(char dirs[][512], int max)
{
    int n = 0;
    const char *home = getenv("XDG_DATA_HOME");
    if (home != NULL && home[0] == '/') {
        snprintf(dirs[n++], 512, "%s", home);
    } else if (getenv("HOME") != NULL) {
        snprintf(dirs[n++], 512, "%s/.local/share", getenv("HOME"));
    }
    const char *sys = getenv("XDG_DATA_DIRS");
    if (sys == NULL || sys[0] == '\0') sys = "/usr/local/share:/usr/share";
    const char *p = sys;
    while (*p && n < max) {
        const char *end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && p[0] == '/') {
            snprintf(dirs[n], 512, "%.*s", (int)len, p);
            bool dup = false;
            for (int i = 0; i < n; i++) dup |= strcmp(dirs[i], dirs[n]) == 0;
            if (!dup) n++;
        }
        if (end == NULL) break;
        p = end + 1;
    }
    return n;
}

/* The PNG for an Icon value, or false. */
static bool find_icon(const char *icon, char dirs[][512], int nd, char *out, size_t cap)
{
    if (icon == NULL || icon[0] == '\0') return false;
    if (icon[0] == '/') {
        if (!has_suffix(icon, ".png") || access(icon, R_OK) != 0) return false;
        snprintf(out, cap, "%s", icon);
        return true;
    }
    char name[256];
    snprintf(name, sizeof(name), "%s", icon);
    if (has_suffix(name, ".png")) name[strlen(name) - 4] = '\0';
    if (strchr(name, '/') != NULL) return false;
    static const char *const sizes[] = { "24x24", "32x32", "22x22", "48x48", "64x64", "16x16",
                                         "96x96", "128x128", "256x256", "512x512" };
    for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
        for (int d = 0; d < nd; d++) {
            if (pathf(out, cap, "%s/icons/hicolor/%s/apps/%s.png", dirs[d], sizes[z], name) &&
                access(out, R_OK) == 0) {
                return true;
            }
        }
    }
    for (int d = 0; d < nd; d++) {
        if (pathf(out, cap, "%s/pixmaps/%s.png", dirs[d], name) && access(out, R_OK) == 0) return true;
    }
    return false;
}

/* The BMP for a PNG (made once per source: the name follows its path, size
 * and time), as a Family mruby path; NULL when it cannot be made. */
static char *icon_for(const char *icon, char dirs[][512], int nd, const char *icon_dir, const char *fm_dir)
{
#if NATIVE_HAVE_PNG
    char src[1024];
    if (!find_icon(icon, dirs, nd, src, sizeof(src))) return NULL;
    struct stat st;
    if (stat(src, &st) != 0) return NULL;
    char key[1200];
    snprintf(key, sizeof(key), "%s|%lld|%lld", src, (long long)st.st_size, (long long)st.st_mtime);
    uint64_t hsh = 1469598103934665603ULL;
    for (const char *k = key; *k; k++) {
        hsh ^= (uint8_t)*k;
        hsh *= 1099511628211ULL;
    }
    char file[64];
    snprintf(file, sizeof(file), "lx%016llx.bmp", (unsigned long long)hsh);
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", icon_dir, file);
    if (access(path, R_OK) != 0) {
        uint8_t px[LINUX_APPS_ICON_PX * LINUX_APPS_ICON_PX];
        if (!png_to_icon(src, px) || !write_bmp(path, px, LINUX_APPS_ICON_PX, LINUX_APPS_ICON_PX)) {
            return NULL;
        }
    }
    char fm[400];
    snprintf(fm, sizeof(fm), "%s/%s", fm_dir, file);
    return strdup(fm);
#else
    (void)icon;
    (void)dirs;
    (void)nd;
    (void)icon_dir;
    (void)fm_dir;
    return NULL;
#endif
}

/* ------------------------------------------------- reading the entries */

static bool list_has(const char *list, const char *word)
{
    if (list == NULL || word == NULL) return false;
    const char *p = list;
    size_t wl = strlen(word);
    while (*p) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == wl && strncasecmp(p, word, wl) == 0) return true;
        if (end == NULL) break;
        p = end + 1;
    }
    return false;
}

static bool is_true(const char *v)
{
    return v != NULL && strcmp(v, "true") == 0;
}

typedef struct {
    char *type, *name, *name_l, *exec, *tryexec, *icon, *nodisplay, *hidden, *onlyshow,
        *notshow, *terminal, *wmclass, *path;
    int name_l_rank;
} fields_t;

static void free_fields(fields_t *f)
{
    free(f->type);
    free(f->name);
    free(f->name_l);
    free(f->exec);
    free(f->tryexec);
    free(f->icon);
    free(f->nodisplay);
    free(f->hidden);
    free(f->onlyshow);
    free(f->notshow);
    free(f->terminal);
    free(f->wmclass);
    free(f->path);
}

static void set_field(char **slot, const char *v)
{
    if (*slot != NULL) return;   /* the first wins */
    *slot = strdup(v);
}

/* The [Desktop Entry] group of a file into f. */
static bool read_fields(const char *path, const char *lang, fields_t *f)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > MAX_FILE_BYTES) {
        close(fd);
        return false;
    }
    char *buf = malloc((size_t)st.st_size + 1);
    if (buf == NULL) {
        close(fd);
        return false;
    }
    ssize_t got = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (got < 0) {
        free(buf);
        return false;
    }
    buf[got] = '\0';
    bool in_group = false;
    size_t ll = strlen(lang);
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = '\0';
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0' || *line == '#') continue;
        if (*line == '[') {
            in_group = strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_group) continue;
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        char *k_end = eq;
        while (k_end > line && (k_end[-1] == ' ' || k_end[-1] == '\t')) k_end--;
        *k_end = '\0';
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        const char *k = line;
        if (strncmp(k, "Name[", 5) == 0) {
            const char *loc = k + 5;
            size_t locn = strcspn(loc, "]");
            int rank = 0;
            if (ll > 0 && locn == ll && strncmp(loc, lang, ll) == 0) rank = 2;
            else if (ll > 0 && locn > ll && strncmp(loc, lang, ll) == 0 && (loc[ll] == '_' || loc[ll] == '@')) rank = 1;
            if (rank > f->name_l_rank) {
                free(f->name_l);
                f->name_l = strdup(v);
                f->name_l_rank = rank;
            }
            continue;
        }
        if (strchr(k, '[') != NULL) continue;
        if (strcmp(k, "Type") == 0) set_field(&f->type, v);
        else if (strcmp(k, "Name") == 0) set_field(&f->name, v);
        else if (strcmp(k, "Exec") == 0) set_field(&f->exec, v);
        else if (strcmp(k, "TryExec") == 0) set_field(&f->tryexec, v);
        else if (strcmp(k, "Icon") == 0) set_field(&f->icon, v);
        else if (strcmp(k, "NoDisplay") == 0) set_field(&f->nodisplay, v);
        else if (strcmp(k, "Hidden") == 0) set_field(&f->hidden, v);
        else if (strcmp(k, "OnlyShowIn") == 0) set_field(&f->onlyshow, v);
        else if (strcmp(k, "NotShowIn") == 0) set_field(&f->notshow, v);
        else if (strcmp(k, "Terminal") == 0) set_field(&f->terminal, v);
        else if (strcmp(k, "StartupWMClass") == 0) set_field(&f->wmclass, v);
        else if (strcmp(k, "Path") == 0) set_field(&f->path, v);
    }
    free(buf);
    char **all[] = { &f->name, &f->name_l, &f->exec, &f->tryexec, &f->icon, &f->wmclass, &f->path };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (*all[i] != NULL) unescape_value(*all[i]);
    }
    return true;
}

/* Names may hold neither tabs nor line ends (the list is lines of tabs). */
static void flatten(char *v)
{
    for (; v != NULL && *v; v++) {
        if (*v == '\t' || *v == '\n' || *v == '\r') *v = ' ';
    }
}

typedef struct {
    entry_t *e;
    int n, cap;
    char **seen;        /* desktop file ids met so far (the first wins) */
    int nseen, capseen;
    char term[512];     /* the terminal for Terminal=true ("" none) */
    bool term_foot;
} build_t;

static bool seen(build_t *b, const char *id)
{
    for (int i = 0; i < b->nseen; i++) {
        if (strcmp(b->seen[i], id) == 0) return true;
    }
    if (b->nseen + 1 > b->capseen) {
        int cap = b->capseen ? b->capseen * 2 : 64;
        char **v = realloc(b->seen, (size_t)cap * sizeof(char *));
        if (v == NULL) return true;
        b->seen = v;
        b->capseen = cap;
    }
    b->seen[b->nseen++] = strdup(id);
    return false;
}

/* argv run in the terminal: "foot -- argv..." or "x-terminal-emulator -e argv...". */
static char **in_terminal(build_t *b, char **argv)
{
    int n = 0;
    while (argv[n] != NULL) n++;
    char **out = calloc((size_t)n + 3, sizeof(char *));
    if (out == NULL) return NULL;
    out[0] = strdup(b->term_foot ? "foot" : "x-terminal-emulator");
    out[1] = strdup(b->term_foot ? "--" : "-e");
    for (int i = 0; i < n; i++) out[i + 2] = strdup(argv[i]);
    return out;
}

static void add_file(build_t *b, const char *path, const char *id, const char *lang, const char *desktop)
{
    if (seen(b, id) || b->n >= MAX_ENTRIES) return;
    fields_t f = { 0 };
    if (!read_fields(path, lang, &f)) goto out;
    if (f.type == NULL || strcmp(f.type, "Application") != 0) goto out;
    if (is_true(f.nodisplay) || is_true(f.hidden)) goto out;
    if (f.onlyshow != NULL && !list_has(f.onlyshow, desktop)) goto out;
    if (f.notshow != NULL && list_has(f.notshow, desktop)) goto out;
    if (f.exec == NULL || f.name == NULL) goto out;
    char found[1024];
    if (f.tryexec != NULL && !find_program(f.tryexec, found, sizeof(found))) goto out;
    const char *shown = f.name_l ? f.name_l : f.name;
    char **argv = linux_apps_split_exec(f.exec, shown, f.icon, path);
    if (argv == NULL) goto out;
    char exe[1024];
    if (!find_program(argv[0], exe, sizeof(exe))) {
        linux_apps_free_argv(argv);
        goto out;
    }
    const char *prog_name = base_name(argv[0]);
    char key[128];
    snprintf(key, sizeof(key), "%s", f.wmclass ? f.wmclass : prog_name);
    if (is_true(f.terminal)) {
        if (b->term[0] == '\0') {
            linux_apps_free_argv(argv);
            goto out;
        }
        char **t = in_terminal(b, argv);
        linux_apps_free_argv(argv);
        if (t == NULL) goto out;
        argv = t;
        snprintf(exe, sizeof(exe), "%s", b->term);
    }
    if (b->n + 1 > b->cap) {
        int cap = b->cap ? b->cap * 2 : 64;
        entry_t *v = realloc(b->e, (size_t)cap * sizeof(entry_t));
        if (v == NULL) {
            linux_apps_free_argv(argv);
            goto out;
        }
        b->e = v;
        b->cap = cap;
    }
    entry_t *e = &b->e[b->n++];
    memset(e, 0, sizeof(*e));
    e->id = strdup(id);
    e->name = strdup(shown);
    flatten(e->name);
    e->key = strdup(key);
    e->idkey = strdup(id);
    if (has_suffix(e->idkey, ".desktop")) e->idkey[strlen(e->idkey) - 8] = '\0';
    e->exe = strdup(exe);
    e->argv = argv;
    e->dir = (f.path && f.path[0] == '/') ? strdup(f.path) : NULL;
    e->icon = f.icon ? strdup(f.icon) : NULL;   /* the Icon value for now; the BMP below */
out:
    free_fields(&f);
}

static void walk(build_t *b, const char *dir, const char *prefix, int depth, const char *lang, const char *desktop)
{
    DIR *d = opendir(dir);
    if (d == NULL) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_DEPTH) {
                char sub[256];
                if (pathf(sub, sizeof(sub), "%s%s-", prefix, de->d_name)) {
                    walk(b, path, sub, depth + 1, lang, desktop);
                }
            }
        } else if (S_ISREG(st.st_mode) && has_suffix(de->d_name, ".desktop")) {
            char id[300];
            snprintf(id, sizeof(id), "%s%s", prefix, de->d_name);
            if (strchr(id, '\t') == NULL && strchr(id, '\n') == NULL) add_file(b, path, id, lang, desktop);
        }
    }
    closedir(d);
}

static int by_name(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    int c = strcasecmp(x->name, y->name);
    return c != 0 ? c : strcmp(x->id, y->id);
}

static void *reader(void *arg)
{
    (void)arg;
    for (;;) {
        char lang[16], desktop[64], icon_dir[512], fm_dir[256];
        pthread_mutex_lock(&s.lock);
        if (!s.scan_req) {
            s.scanning = false;
            pthread_mutex_unlock(&s.lock);
            return NULL;
        }
        s.scan_req = false;
        snprintf(lang, sizeof(lang), "%s", s.want_lang);
        snprintf(desktop, sizeof(desktop), "%s", s.desktop);
        snprintf(icon_dir, sizeof(icon_dir), "%s", s.icon_dir);
        snprintf(fm_dir, sizeof(fm_dir), "%s", s.icon_fm_dir);
        pthread_mutex_unlock(&s.lock);

        uint64_t t0 = ms_now();
        build_t b = { 0 };
        if (find_program("foot", b.term, sizeof(b.term))) {
            b.term_foot = true;
        } else if (!find_program("x-terminal-emulator", b.term, sizeof(b.term))) {
            b.term[0] = '\0';
        }
        char dirs[16][512];
        int nd = data_dirs(dirs, 16);
        for (int i = 0; i < nd; i++) {
            char apps[600];
            if (pathf(apps, sizeof(apps), "%s/applications", dirs[i])) walk(&b, apps, "", 0, lang, desktop);
        }
        uint64_t t1 = ms_now();
        int icons = 0;
        for (int i = 0; i < b.n; i++) {
            char *v = b.e[i].icon;
            b.e[i].icon = v ? icon_for(v, dirs, nd, icon_dir, fm_dir) : NULL;
            if (b.e[i].icon != NULL) icons++;
            free(v);
        }
        qsort(b.e, (size_t)b.n, sizeof(entry_t), by_name);
        for (int i = 0; i < b.nseen; i++) free(b.seen[i]);
        free(b.seen);
        fprintf(stderr, "native: linux apps: %d entries of %d desktop files (%s), %d with icons; "
                        "read in %llu ms, icons in %llu ms; terminal %s\n",
                b.n, b.nseen, lang, icons, (unsigned long long)(t1 - t0), (unsigned long long)(ms_now() - t1),
                b.term[0] ? b.term : "none (Terminal=true entries left out)");

        pthread_mutex_lock(&s.lock);
        entry_t *old = s.e;
        int old_n = s.n;
        s.e = b.e;
        s.n = b.n;
        snprintf(s.lang, sizeof(s.lang), "%s", lang);
        s.ready = true;
        pthread_mutex_unlock(&s.lock);
        free_list(old, old_n);
    }
}

/* Under the lock. */
static void kick_locked(void)
{
    s.scan_req = true;
    if (s.scanning) return;
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &a, reader, NULL) == 0) s.scanning = true;
    pthread_attr_destroy(&a);
}

/* ------------------------------------------------------- the interface */

static bool mkdirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    return mkdir(tmp, 0700) == 0 || errno == EEXIST;
}

void linux_apps_start(const linux_apps_config_t *cfg)
{
    pthread_mutex_lock(&s.lock);
    snprintf(s.icon_dir, sizeof(s.icon_dir), "%s", cfg->icon_dir);
    snprintf(s.icon_fm_dir, sizeof(s.icon_fm_dir), "%s", cfg->icon_fm_dir);
    snprintf(s.desktop, sizeof(s.desktop), "%s", cfg->desktop_name ? cfg->desktop_name : "");
    if (s.want_lang[0] == '\0') snprintf(s.want_lang, sizeof(s.want_lang), "en");
    pthread_mutex_unlock(&s.lock);

    bool dir_ok = mkdirs(cfg->icon_dir);
    char mark[600];
    snprintf(mark, sizeof(mark), "%s/linux.bmp", cfg->icon_dir);
    uint8_t px[LINUX_APPS_ICON_PX * LINUX_APPS_ICON_PX];
    draw_mark(px);
    bool mark_ok = dir_ok && write_bmp(mark, px, LINUX_APPS_ICON_PX, LINUX_APPS_ICON_PX);
    if (!mark_ok) {
        fprintf(stderr, "native: linux apps: cannot write icons to %s (%s); no icons\n", cfg->icon_dir,
                strerror(errno));
    }
    snprintf(s_env_desktop, sizeof(s_env_desktop), "XDG_CURRENT_DESKTOP=%s", cfg->desktop_name);

    pthread_mutex_lock(&s.lock);
    if (mark_ok) snprintf(s.mark_fm, sizeof(s.mark_fm), "%s/linux.bmp", cfg->icon_fm_dir);
    s.started = true;
    kick_locked();
    pthread_mutex_unlock(&s.lock);
}

bool linux_apps_available(void)
{
    pthread_mutex_lock(&s.lock);
    bool on = s.started;
    pthread_mutex_unlock(&s.lock);
    return on;
}

/* Append to buf when it fits; the length counts either way. */
static void put(char *buf, size_t cap, size_t *len, const char *str)
{
    size_t n = strlen(str);
    if (buf != NULL && *len + n <= cap) memcpy(buf + *len, str, n);
    *len += n;
}

int32_t linux_apps_list(int mode, const char *lang, char *buf, size_t cap)
{
    pthread_mutex_lock(&s.lock);
    if (!s.started) {
        pthread_mutex_unlock(&s.lock);
        return -1;
    }
    if (lang != NULL && lang[0] != '\0' && strcmp(lang, s.want_lang) != 0) {
        snprintf(s.want_lang, sizeof(s.want_lang), "%s", lang);
        if (strcmp(s.lang, lang) != 0) mode = LINUX_APPS_RESCAN;
    }
    if (mode == LINUX_APPS_RESCAN) {
        s.ready = false;
        kick_locked();
    }
    bool ready = s.ready && !s.scanning;
    size_t len = 0;
    put(buf, cap, &len, ready ? "ready\t" : "loading\t");
    put(buf, cap, &len, s.mark_fm);
    put(buf, cap, &len, "\n");
    if (ready && mode == LINUX_APPS_LIST) {
        for (int i = 0; i < s.n; i++) {
            put(buf, cap, &len, s.e[i].name);
            put(buf, cap, &len, "\t");
            put(buf, cap, &len, s.e[i].id);
            put(buf, cap, &len, "\t");
            put(buf, cap, &len, s.e[i].icon ? s.e[i].icon : "");
            put(buf, cap, &len, "\n");
        }
    }
    pthread_mutex_unlock(&s.lock);
    return len > 0x7fffffff ? -1 : (int32_t)len;
}

int linux_apps_launch(const char *id)
{
    if (id == NULL) return -1;
    pthread_mutex_lock(&s.lock);
    int rc = -1;
    if (s.started && s.qn < QUEUE_MAX) {
        for (int i = 0; i < s.n; i++) {
            entry_t *e = &s.e[i];
            if (strcmp(e->id, id) != 0) continue;
            launch_t *l = &s.q[s.qn];
            memset(l, 0, sizeof(*l));
            l->argv = copy_argv(e->argv);
            l->exe = dup_str(e->exe);
            l->dir = dup_str(e->dir);
            l->name = dup_str(e->name);
            l->key = dup_str(e->key);
            l->idkey = dup_str(e->idkey);
            if (l->argv != NULL && l->exe != NULL && l->name != NULL) {
                s.qn++;
                rc = 0;
            } else {
                linux_apps_free_argv(l->argv);
                free(l->exe);
                free(l->dir);
                free(l->name);
                free(l->key);
                free(l->idkey);
            }
            break;
        }
    }
    pthread_mutex_unlock(&s.lock);
    return rc;
}

/* ------------------------------------------- starting and ending them */

/* The session id of a process, -1 when it is gone. */
static pid_t session_of(pid_t pid)
{
    char path[64], line[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t len = read(fd, line, sizeof(line) - 1);
    close(fd);
    if (len <= 0) return -1;
    line[len] = '\0';
    char *p = strrchr(line, ')');
    char state;
    long ppid, pgrp, session;
    if (p == NULL || sscanf(p + 1, " %c %ld %ld %ld", &state, &ppid, &pgrp, &session) != 4) return -1;
    return state == 'Z' ? -1 : (pid_t)session;
}

static bool recorded_session(pid_t sid)
{
    for (int i = 0; i < RECORDS_MAX; i++) {
        if (s_rec[i].used && s_rec[i].pid == sid) return true;
    }
    return false;
}

/* Every live process in a session started here: signalled with sig (0:
 * only counted). Returns how many. */
static int each_started_process(int sig)
{
    DIR *d = opendir("/proc");
    if (d == NULL) return 0;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        char *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 0 || pid == getpid()) continue;
        pid_t sid = session_of((pid_t)pid);
        if (sid <= 0 || !recorded_session(sid)) continue;
        if (sig == 0 || kill((pid_t)pid, sig) == 0) n++;
    }
    closedir(d);
    return n;
}

static void reap(void)
{
    for (int i = 0; i < RECORDS_MAX; i++) {
        record_t *r = &s_rec[i];
        if (!r->used || r->reaped) continue;
        int st = 0;
        pid_t w = waitpid(r->pid, &st, WNOHANG);
        if (w == r->pid || (w < 0 && errno == ECHILD)) {
            r->reaped = true;
            fprintf(stderr, "native: linux apps: \"%s\" (pid %d) ended (%d)\n", r->name, (int)r->pid,
                    w == r->pid ? (WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st)) : -1);
        }
    }
}

/* A record for a new start: a free one, else one whose program and session
 * have all gone. */
static record_t *free_record(void)
{
    for (int i = 0; i < RECORDS_MAX; i++) {
        if (!s_rec[i].used) return &s_rec[i];
    }
    int counts[RECORDS_MAX] = { 0 };
    DIR *d = opendir("/proc");
    if (d != NULL) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            char *end;
            long pid = strtol(de->d_name, &end, 10);
            if (*end != '\0' || pid <= 0) continue;
            pid_t sid = session_of((pid_t)pid);
            for (int i = 0; i < RECORDS_MAX; i++) {
                if (s_rec[i].pid == sid) counts[i]++;
            }
        }
        closedir(d);
    }
    for (int i = 0; i < RECORDS_MAX; i++) {
        if (s_rec[i].reaped && counts[i] == 0) return &s_rec[i];
    }
    return NULL;
}

static void build_env(void)
{
    int n = 0;
    for (char **e = environ; e != NULL && *e != NULL && n < ENV_MAX; e++) {
        if (strncmp(*e, "XDG_CURRENT_DESKTOP=", 20) == 0) continue;
        s_envp[n++] = *e;
    }
    if (s_env_desktop[0] != '\0' && strchr(s_env_desktop, '=')[1] != '\0') s_envp[n++] = s_env_desktop;
    s_envp[n] = NULL;
}

/* The child, between fork and exec: async-signal-safe calls only. */
static void child_exec(const launch_t *l, const char *dir, pid_t parent)
{
    sigset_t all;
    sigemptyset(&all);
    sigprocmask(SIG_SETMASK, &all, NULL);
    for (int sig = 1; sig < NSIG; sig++) signal(sig, SIG_DFL);
    if (setsid() < 0) _exit(126);
    /* The program going away (a crash included) ends what it started. */
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() != parent) _exit(126);
    int null = open("/dev/null", O_RDONLY);
    if (null >= 0 && null != 0) {
        dup2(null, 0);
        close(null);
    }
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 3u, ~0u, 0) != 0)
#endif
    {
        for (int fd = 3; fd < 4096; fd++) close(fd);
    }
    if (chdir(dir) != 0 && chdir("/") != 0) _exit(126);
    execve(l->exe, l->argv, s_envp);
    _exit(127);
}

static void start_one(launch_t *l)
{
    record_t *r = free_record();
    if (r == NULL) {
        fprintf(stderr, "native: linux apps: \"%s\" not started: %d programs are running already\n", l->name,
                RECORDS_MAX);
        return;
    }
    const char *home = getenv("HOME");
    const char *dir = l->dir ? l->dir : (home && home[0] == '/' ? home : "/");
    build_env();
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "native: linux apps: \"%s\": fork: %s\n", l->name, strerror(errno));
        return;
    }
    if (pid == 0) child_exec(l, dir, parent);
    memset(r, 0, sizeof(*r));
    r->used = true;
    r->pid = pid;
    copy_utf8(r->name, sizeof(r->name), l->name);
    snprintf(r->key, sizeof(r->key), "%s", l->key ? l->key : "");
    snprintf(r->idkey, sizeof(r->idkey), "%s", l->idkey ? l->idkey : "");
    s_last_pid = pid;
    fprintf(stderr, "native: linux apps: started \"%s\" (pid %d): %s\n", l->name, (int)pid, l->exe);
}

void linux_apps_pump(void)
{
    launch_t q[QUEUE_MAX];
    int n = 0;
    pthread_mutex_lock(&s.lock);
    if (s.qn > 0) {
        n = s.qn;
        memcpy(q, s.q, sizeof(launch_t) * (size_t)n);
        s.qn = 0;
    }
    pthread_mutex_unlock(&s.lock);
    for (int i = 0; i < n; i++) {
        start_one(&q[i]);
        linux_apps_free_argv(q[i].argv);
        free(q[i].exe);
        free(q[i].dir);
        free(q[i].name);
        free(q[i].key);
        free(q[i].idkey);
    }
    reap();
}

int linux_apps_running(void)
{
    return each_started_process(0);
}

int linux_apps_end_all(int grace_ms)
{
    reap();
    int n = each_started_process(SIGTERM);
    if (n == 0) return 0;
    uint64_t t0 = ms_now();
    while (ms_now() - t0 < (uint64_t)grace_ms) {
        reap();
        if (each_started_process(0) == 0) break;
        sleep_ms(10);
    }
    int left = each_started_process(SIGKILL);
    for (int i = 0; i < RECORDS_MAX && left > 0; i++) {
        record_t *r = &s_rec[i];
        if (r->used && !r->reaped && waitpid(r->pid, NULL, 0) == r->pid) r->reaped = true;
    }
    reap();
    if (left > 0) fprintf(stderr, "native: linux apps: SIGKILL to %d process%s\n", left, left == 1 ? "" : "es");
    return n;
}

bool linux_apps_window_name(const char *app_id, const char *alt, pid_t pid, char *out, size_t cap)
{
    if (pid > 0) {
        pid_t sid = session_of(pid);
        for (int i = 0; sid > 0 && i < RECORDS_MAX; i++) {
            if (s_rec[i].used && s_rec[i].pid == sid) {
                copy_utf8(out, cap, s_rec[i].name);
                return true;
            }
        }
    }
    const char *ids[2] = { app_id, alt };
    for (int k = 0; k < 2; k++) {
        if (ids[k] == NULL || ids[k][0] == '\0') continue;
        for (int i = 0; i < RECORDS_MAX; i++) {
            record_t *r = &s_rec[i];
            if (r->used && (same_word(r->key, ids[k]) || same_word(r->idkey, ids[k]))) {
                copy_utf8(out, cap, r->name);
                return true;
            }
        }
    }
    bool found = false;
    pthread_mutex_lock(&s.lock);
    for (int k = 0; k < 2 && !found; k++) {
        if (ids[k] == NULL || ids[k][0] == '\0') continue;
        for (int i = 0; i < s.n; i++) {
            if (same_word(s.e[i].key, ids[k]) || same_word(s.e[i].idkey, ids[k])) {
                copy_utf8(out, cap, s.e[i].name);
                found = true;
                break;
            }
        }
    }
    pthread_mutex_unlock(&s.lock);
    return found;
}

bool linux_apps_wait_scan(int timeout_ms)
{
    uint64_t t0 = ms_now();
    for (;;) {
        pthread_mutex_lock(&s.lock);
        bool busy = s.scanning || s.scan_req;
        pthread_mutex_unlock(&s.lock);
        if (!busy) return true;
        if (ms_now() - t0 > (uint64_t)timeout_ms) return false;
        sleep_ms(5);
    }
}

pid_t linux_apps_last_pid(void)
{
    return s_last_pid;
}
