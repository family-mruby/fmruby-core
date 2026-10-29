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
#include <glob.h>
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

#include <dlfcn.h>

#if NATIVE_HAVE_PNG
#include <png.h>
#endif

#include "linux_apps.h"
#include "linux_mime.h"

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
    /* For opening a file in it (L11). */
    char *exec;         /* Exec as written */
    char *iconname;     /* Icon as written (%i) */
    char *file;         /* the .desktop file (%k) */
    char *prog;         /* Exec's program, an absolute path (exe is the terminal's for Terminal=true) */
    char *mime;         /* MimeType ("a/b;c/d;"), or NULL */
    bool terminal;      /* Terminal=true: run in the terminal */
    bool listed;        /* in the launcher's list (false: kept only to open files with) */
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
    char lists[256];    /* the desktops of the mimeapps.list files, "ours:kde" */
    launch_t q[QUEUE_MAX];
    int qn;
    char term[512];     /* the terminal for Terminal=true ("" none), as last read */
    bool term_foot;
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
    free(e->exec);
    free(e->iconname);
    free(e->file);
    free(e->prog);
    free(e->mime);
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
    /* Files and URLs (when none is passed), and the deprecated codes. */
    return strchr("fFuUdDnNvm", c) != NULL;
}

static bool is_file_code(char c)
{
    return strchr("fFuU", c) != NULL;
}

/* What the split is given: the entry's own values for %c %i %k, and the
 * file to open (NULL: none). took is set when a field code took the file. */
typedef struct {
    const char *name, *icon, *file, *open;
    bool took;
} exec_ctx_t;

/* One unquoted argument with its field codes: whole-argument codes are
 * dropped or expanded to whole arguments; inside an argument they expand to
 * nothing ("%%" to "%"), except a file's code when there is a file to open
 * (L11): a local path stands for %u and %U too, which the spec allows. */
static bool push_unquoted(vec_t *a, const char *tok, exec_ctx_t *x)
{
    const char *name = x->name, *icon = x->icon, *file = x->file;
    if (tok[0] == '%' && tok[1] != '\0' && tok[2] == '\0') {
        char c = tok[1];
        if (x->open != NULL && is_file_code(c)) {
            if (x->took) return true;   /* one file: the first code takes it */
            x->took = true;
            return vec_push(a, x->open, strlen(x->open));
        }
        if (is_drop_code(c)) return true;
        if (c == 'i') {
            if (icon == NULL || icon[0] == '\0') return true;
            return vec_push(a, "--icon", 6) && vec_push(a, icon, strlen(icon));
        }
        if (c == 'c') return vec_push(a, name ? name : "", name ? strlen(name) : 0);
        if (c == 'k') return vec_push(a, file ? file : "", file ? strlen(file) : 0);
    }
    size_t n = strlen(tok);
    size_t extra = x->open != NULL ? strlen(x->open) : 0;
    char *out = malloc(n + extra + 1);
    if (out == NULL) return false;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (tok[i] == '%' && i + 1 < n) {
            i++;
            if (tok[i] == '%') {
                out[w++] = '%';
            } else if (x->open != NULL && is_file_code(tok[i]) && !x->took) {
                x->took = true;
                memcpy(out + w, x->open, extra);
                w += extra;
            }
            continue;
        }
        out[w++] = tok[i];
    }
    bool ok = vec_push(a, out, w);
    free(out);
    return ok;
}

static char **split_exec(const char *exec, exec_ctx_t *x)
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
            ok = push_unquoted(&a, tok, x);
        }
    }
    free(tok);
    if (!ok || a.n == 0) {
        linux_apps_free_argv(a.v);
        return NULL;
    }
    return a.v;
}

char **linux_apps_split_exec(const char *exec, const char *name, const char *icon, const char *file)
{
    exec_ctx_t x = { .name = name, .icon = icon, .file = file };
    return split_exec(exec, &x);
}

char **linux_apps_split_exec_open(const char *exec, const char *name, const char *icon, const char *file,
                                  const char *open)
{
    exec_ctx_t x = { .name = name, .icon = icon, .file = file, .open = open };
    char **argv = split_exec(exec, &x);
    if (argv == NULL || open == NULL || x.took) return argv;
    /* No field code for a file: the file goes last (what GLib and KIO do;
     * the spec leaves it open). */
    int n = 0;
    while (argv[n] != NULL) n++;
    char **v = realloc(argv, (size_t)(n + 2) * sizeof(char *));
    if (v == NULL) {
        linux_apps_free_argv(argv);
        return NULL;
    }
    v[n] = strdup(open);
    v[n + 1] = NULL;
    if (v[n] == NULL) {
        linux_apps_free_argv(v);
        return NULL;
    }
    return v;
}

/* -------------------------------------------------------------- icons */

#if NATIVE_HAVE_PNG
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
    char tmp[768];
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

/* RGBA (straight alpha) fitted into the icon square (aspect kept, centred):
 * each icon pixel is the alpha-weighted mean of the source pixels under it,
 * and opaque when at least half covered. */
static void rgba_to_icon(const uint8_t *rgba, int w, int h, uint8_t *px)
{
    const int n = LINUX_APPS_ICON_PX;
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
}

static bool png_to_icon(const char *src, uint8_t *px)
{
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
    rgba_to_icon(rgba, (int)img.width, (int)img.height, px);
    free(rgba);
    return true;
}

/* SVG through librsvg and cairo, opened when first needed (dlopen: the
 * program runs without them, and then has no SVG icons). Rendered at
 * SVG_PX square and fitted down like a PNG. */
#define SVG_PX 96

typedef struct {
    double x, y, width, height;
} svg_rect_t;    /* RsvgRectangle */

static struct {
    pthread_once_t once;
    bool ok;
    void *(*handle_new)(const char *file, void **error);
    int (*render)(void *handle, void *cr, const svg_rect_t *viewport, void **error);
    void (*unref)(void *obj);
    void (*error_free)(void *error);
    void *(*surface_create)(int format, int w, int h);
    void *(*create)(void *surface);
    void (*destroy)(void *cr);
    void (*surface_flush)(void *surface);
    unsigned char *(*surface_data)(void *surface);
    int (*surface_stride)(void *surface);
    void (*surface_destroy)(void *surface);
} s_svg = { .once = PTHREAD_ONCE_INIT };

static void svg_open(void)
{
    void *lib = dlopen("librsvg-2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        fprintf(stderr, "native: linux apps: no librsvg (%s): SVG icons are left out\n", dlerror());
        return;
    }
    /* cairo, GObject and GLib come with it (dlsym searches its dependencies). */
    *(void **)&s_svg.handle_new = dlsym(lib, "rsvg_handle_new_from_file");
    *(void **)&s_svg.render = dlsym(lib, "rsvg_handle_render_document");
    *(void **)&s_svg.unref = dlsym(lib, "g_object_unref");
    *(void **)&s_svg.error_free = dlsym(lib, "g_error_free");
    *(void **)&s_svg.surface_create = dlsym(lib, "cairo_image_surface_create");
    *(void **)&s_svg.create = dlsym(lib, "cairo_create");
    *(void **)&s_svg.destroy = dlsym(lib, "cairo_destroy");
    *(void **)&s_svg.surface_flush = dlsym(lib, "cairo_surface_flush");
    *(void **)&s_svg.surface_data = dlsym(lib, "cairo_image_surface_get_data");
    *(void **)&s_svg.surface_stride = dlsym(lib, "cairo_image_surface_get_stride");
    *(void **)&s_svg.surface_destroy = dlsym(lib, "cairo_surface_destroy");
    s_svg.ok = s_svg.handle_new && s_svg.render && s_svg.unref && s_svg.error_free && s_svg.surface_create &&
               s_svg.create && s_svg.destroy && s_svg.surface_flush && s_svg.surface_data && s_svg.surface_stride &&
               s_svg.surface_destroy;
    if (!s_svg.ok) {
        fprintf(stderr, "native: linux apps: librsvg without rsvg_handle_render_document (older than 2.46): "
                        "SVG icons are left out\n");
    }
}

bool linux_apps_svg_available(void)
{
    pthread_once(&s_svg.once, svg_open);
    return s_svg.ok;
}

static bool svg_to_icon(const char *src, uint8_t *px)
{
    if (!linux_apps_svg_available()) return false;
    void *err = NULL;
    void *h = s_svg.handle_new(src, &err);
    if (h == NULL) {
        if (err != NULL) s_svg.error_free(err);
        return false;
    }
    void *surf = s_svg.surface_create(0 /* CAIRO_FORMAT_ARGB32 */, SVG_PX, SVG_PX);
    void *cr = surf != NULL ? s_svg.create(surf) : NULL;
    const svg_rect_t vp = { 0, 0, SVG_PX, SVG_PX };
    bool ok = cr != NULL && s_svg.render(h, cr, &vp, &err);
    if (err != NULL) s_svg.error_free(err);
    if (cr != NULL) s_svg.destroy(cr);
    s_svg.unref(h);
    uint8_t *rgba = ok ? malloc((size_t)SVG_PX * SVG_PX * 4) : NULL;
    if (rgba != NULL) {
        s_svg.surface_flush(surf);
        const unsigned char *data = s_svg.surface_data(surf);
        int stride = s_svg.surface_stride(surf);
        /* Premultiplied ARGB in native-endian words, made straight RGBA. */
        for (int y = 0; y < SVG_PX; y++) {
            const uint32_t *row = (const uint32_t *)(const void *)(data + (size_t)y * (size_t)stride);
            for (int x = 0; x < SVG_PX; x++) {
                uint32_t v = row[x];
                unsigned a = v >> 24, r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                uint8_t *o = rgba + ((size_t)y * SVG_PX + (size_t)x) * 4;
                o[0] = (uint8_t)(a ? r * 255 / a : 0);
                o[1] = (uint8_t)(a ? g * 255 / a : 0);
                o[2] = (uint8_t)(a ? b * 255 / a : 0);
                o[3] = (uint8_t)a;
            }
        }
        rgba_to_icon(rgba, SVG_PX, SVG_PX, px);
        free(rgba);
    }
    if (surf != NULL) s_svg.surface_destroy(surf);
    return rgba != NULL;
}
#else
bool linux_apps_svg_available(void)
{
    return false;
}
#endif

static bool has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* --------------------------------------------------- the icon themes (L9e)
 *
 * The freedesktop icon theme lookup, for the launcher's size: the user's
 * theme, the themes it inherits (in order, each once), then hicolor; in
 * each, the theme's directories nearest that size first; a PNG, else an
 * SVG. The user's theme: Plasma's (kdeglobals [Icons] Theme), else GTK's
 * (settings.ini gtk-icon-theme-name), else breeze (Plasma's own default)
 * when it is installed. The themes' directories are read once per reading
 * of the list. */

#define THEMES_MAX 8
#define THEME_DIRS_MAX 1024
#define ICON_BASES_MAX 18

typedef struct {
    char *path;     /* <base>/<theme>/<directory> */
    int dist;       /* from the launcher's size */
} theme_dir_t;

struct linux_icon_themes {
    char name[THEMES_MAX][64];
    int n;
    theme_dir_t *dir;   /* every theme's, theme by theme, each theme's nearest first */
    int ndir;
    int start[THEMES_MAX + 1];
};

/* The value of key in [section] of an ini-like file, or false. */
static bool ini_value(const char *path, const char *section, const char *key, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    char line[1024];
    bool in = false, found = false;
    size_t kl = strlen(key);
    while (!found && fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            in = end != NULL && (size_t)(end - line - 1) == strlen(section) &&
                 strncmp(line + 1, section, strlen(section)) == 0;
            continue;
        }
        if (!in || strncmp(line, key, kl) != 0) continue;
        const char *v = line + kl;
        while (*v == ' ' || *v == '\t') v++;
        if (*v != '=') continue;
        v++;
        while (*v == ' ' || *v == '\t') v++;
        snprintf(out, cap, "%s", v);
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = '\0';
        found = out[0] != '\0';
    }
    fclose(f);
    return found;
}

static int icon_bases(char dirs[][512], int nd, char bases[][512], int max)
{
    int n = 0;
    if (getenv("HOME") != NULL && pathf(bases[n], 512, "%s/.icons", getenv("HOME"))) n++;
    for (int d = 0; d < nd && n < max; d++) {
        if (pathf(bases[n], 512, "%s/icons", dirs[d])) n++;
    }
    return n;
}

static bool theme_exists(const char *name, char bases[][512], int nb)
{
    char p[700];
    for (int b = 0; b < nb; b++) {
        if (pathf(p, sizeof(p), "%s/%s/index.theme", bases[b], name) && access(p, R_OK) == 0) return true;
    }
    return false;
}

bool linux_apps_user_icon_theme(char *out, size_t cap, char *why, size_t why_cap)
{
    char cfg[512];
    const char *xch = getenv("XDG_CONFIG_HOME");
    if (xch != NULL && xch[0] == '/') {
        snprintf(cfg, sizeof(cfg), "%s", xch);
    } else if (getenv("HOME") != NULL) {
        snprintf(cfg, sizeof(cfg), "%s/.config", getenv("HOME"));
    } else {
        cfg[0] = '\0';
    }
    char p[700];
    if (cfg[0] != '\0') {
        if (pathf(p, sizeof(p), "%s/kdeglobals", cfg) && ini_value(p, "Icons", "Theme", out, cap)) {
            snprintf(why, why_cap, "Theme in %s", p);
            return true;
        }
        static const char *const gtk[] = { "gtk-4.0", "gtk-3.0" };
        for (size_t i = 0; i < sizeof(gtk) / sizeof(gtk[0]); i++) {
            if (pathf(p, sizeof(p), "%s/%s/settings.ini", cfg, gtk[i]) &&
                ini_value(p, "Settings", "gtk-icon-theme-name", out, cap)) {
                snprintf(why, why_cap, "gtk-icon-theme-name in %s", p);
                return true;
            }
        }
    }
    snprintf(out, cap, "breeze");
    snprintf(why, why_cap, "none set: Plasma's default");
    return false;
}

static int dir_cmp(const void *a, const void *b)
{
    const theme_dir_t *x = a, *y = b;
    return x->dist - y->dist;
}

/* One directory section's distance from the launcher's size (the spec's
 * DirectorySizeDistance at scale 1); -1 for a scaled one. */
static int section_dist(const char *type, int size, int min, int max, int threshold, int scale)
{
    const int want = LINUX_APPS_ICON_PX;
    if (scale > 1) return -1;
    if (strcmp(type, "Fixed") == 0) return abs(size - want);
    if (strcmp(type, "Scalable") == 0) return want < min ? min - want : (want > max ? want - max : 0);
    int lo = size - threshold, hi = size + threshold;
    return want < lo ? lo - want : (want > hi ? want - hi : 0);
}

/* Add a theme's directories (from its index.theme) to th; its Inherits into
 * inherits. False when it has no index.theme. */
static bool theme_load(linux_icon_themes_t *th, const char *name, char bases[][512], int nb, char *inherits,
                       size_t icap)
{
    char idx[700];
    FILE *f = NULL;
    for (int b = 0; b < nb && f == NULL; b++) {
        if (pathf(idx, sizeof(idx), "%s/%s/index.theme", bases[b], name)) f = fopen(idx, "r");
    }
    if (f == NULL) return false;
    inherits[0] = '\0';
    char line[4096];
    char section[256] = "";
    char type[16] = "Threshold";
    int size = 0, min = -1, max = -1, threshold = 2, scale = 1;
    bool in_dir = false;
    /* A section is complete at the next header (and at the end). */
    for (bool more = true; more;) {
        more = fgets(line, sizeof(line), f) != NULL;
        if (more) line[strcspn(line, "\r\n")] = '\0';
        if (!more || line[0] == '[') {
            if (in_dir && size > 0) {
                int dist = section_dist(type, size, min < 0 ? size : min, max < 0 ? size : max, threshold, scale);
                for (int b = 0; b < nb && dist >= 0 && th->ndir < THEME_DIRS_MAX; b++) {
                    char p[900];
                    struct stat st;
                    if (!pathf(p, sizeof(p), "%s/%s/%s", bases[b], name, section) || stat(p, &st) != 0 ||
                        !S_ISDIR(st.st_mode)) {
                        continue;
                    }
                    th->dir[th->ndir].path = strdup(p);
                    th->dir[th->ndir].dist = dist;
                    if (th->dir[th->ndir].path != NULL) th->ndir++;
                }
            }
            if (!more) break;
            char *end = strchr(line, ']');
            if (end == NULL) continue;
            *end = '\0';
            snprintf(section, sizeof(section), "%.255s", line + 1);
            in_dir = strcmp(section, "Icon Theme") != 0;
            snprintf(type, sizeof(type), "Threshold");
            size = 0;
            min = max = -1;
            threshold = 2;
            scale = 1;
            continue;
        }
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        if (!in_dir) {
            if (strcmp(k, "Inherits") == 0) snprintf(inherits, icap, "%s", v);
        } else if (strcmp(k, "Size") == 0) {
            size = atoi(v);
        } else if (strcmp(k, "MinSize") == 0) {
            min = atoi(v);
        } else if (strcmp(k, "MaxSize") == 0) {
            max = atoi(v);
        } else if (strcmp(k, "Threshold") == 0) {
            threshold = atoi(v);
        } else if (strcmp(k, "Scale") == 0) {
            scale = atoi(v);
        } else if (strcmp(k, "Type") == 0) {
            snprintf(type, sizeof(type), "%s", v);
        }
    }
    fclose(f);
    return true;
}

static bool theme_listed(const linux_icon_themes_t *th, const char *name)
{
    for (int i = 0; i < th->n; i++) {
        if (strcmp(th->name[i], name) == 0) return true;
    }
    return false;
}

linux_icon_themes_t *linux_apps_icon_themes(const char *user_theme)
{
    char dirs[16][512];
    int nd = linux_xdg_data_dirs(dirs, 16);
    char bases[ICON_BASES_MAX][512];
    int nb = icon_bases(dirs, nd, bases, ICON_BASES_MAX);
    linux_icon_themes_t *th = calloc(1, sizeof(*th));
    if (th == NULL) return NULL;
    th->dir = calloc(THEME_DIRS_MAX, sizeof(theme_dir_t));
    if (th->dir == NULL) {
        free(th);
        return NULL;
    }
    /* The queue of themes to read: the user's, what each inherits (breadth
     * first), and hicolor last whatever happens. */
    char queue[THEMES_MAX][64];
    int qn = 0;
    if (user_theme != NULL && user_theme[0] != '\0' && strcmp(user_theme, "hicolor") != 0 &&
        theme_exists(user_theme, bases, nb)) {
        snprintf(queue[qn++], 64, "%.63s", user_theme);
    }
    for (int qi = 0; qi < qn && th->n < THEMES_MAX - 1; qi++) {
        if (theme_listed(th, queue[qi])) continue;
        char inherits[512];
        int before = th->ndir;
        if (!theme_load(th, queue[qi], bases, nb, inherits, sizeof(inherits))) continue;
        qsort(th->dir + before, (size_t)(th->ndir - before), sizeof(theme_dir_t), dir_cmp);
        th->start[th->n] = before;
        snprintf(th->name[th->n++], 64, "%.63s", queue[qi]);
        th->start[th->n] = th->ndir;
        char *save = NULL;
        for (char *t = strtok_r(inherits, ",", &save); t != NULL && qn < THEMES_MAX; t = strtok_r(NULL, ",", &save)) {
            while (*t == ' ') t++;
            if (t[0] != '\0' && strcmp(t, "hicolor") != 0) snprintf(queue[qn++], 64, "%.63s", t);
        }
    }
    char inherits[512];
    int before = th->ndir;
    if (!theme_load(th, "hicolor", bases, nb, inherits, sizeof(inherits))) {
        /* No index.theme for hicolor: its usual application directories. */
        static const char *const sizes[] = { "24x24", "32x32", "22x22", "48x48", "64x64", "16x16",
                                             "96x96", "128x128", "256x256", "512x512", "scalable" };
        for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
            for (int b = 0; b < nb && th->ndir < THEME_DIRS_MAX; b++) {
                char p[900];
                if (!pathf(p, sizeof(p), "%s/hicolor/%s/apps", bases[b], sizes[z])) continue;
                th->dir[th->ndir].path = strdup(p);
                th->dir[th->ndir].dist = (int)z;
                if (th->dir[th->ndir].path != NULL) th->ndir++;
            }
        }
    }
    qsort(th->dir + before, (size_t)(th->ndir - before), sizeof(theme_dir_t), dir_cmp);
    th->start[th->n] = before;
    snprintf(th->name[th->n++], 64, "hicolor");
    th->start[th->n] = th->ndir;
    return th;
}

void linux_apps_icon_themes_free(linux_icon_themes_t *th)
{
    if (th == NULL) return;
    for (int i = 0; i < th->ndir; i++) free(th->dir[i].path);
    free(th->dir);
    free(th);
}

const char *linux_apps_icon_theme_names(const linux_icon_themes_t *th, char *out, size_t cap)
{
    size_t len = 0;
    out[0] = '\0';
    for (int i = 0; th != NULL && i < th->n && len < cap; i++) {
        int n = snprintf(out + len, cap - len, "%s%s", i ? " > " : "", th->name[i]);
        if (n < 0) break;
        len += (size_t)n;
    }
    return out;
}

/* The image file for an Icon value (a PNG or an SVG), or false. */
bool linux_apps_find_icon(const linux_icon_themes_t *th, const char *icon, char *out, size_t cap)
{
    if (icon == NULL || icon[0] == '\0') return false;
    if (icon[0] == '/') {
        if (!(has_suffix(icon, ".png") || has_suffix(icon, ".svg") || has_suffix(icon, ".svgz")) ||
            access(icon, R_OK) != 0) {
            return false;
        }
        snprintf(out, cap, "%s", icon);
        return true;
    }
    char name[256];
    snprintf(name, sizeof(name), "%s", icon);
    if (has_suffix(name, ".png") || has_suffix(name, ".svg")) name[strlen(name) - 4] = '\0';
    if (strchr(name, '/') != NULL) return false;
    static const char *const exts[] = { "png", "svg", "svgz" };
    for (int t = 0; th != NULL && t < th->n; t++) {
        for (int i = th->start[t]; i < th->start[t + 1]; i++) {
            for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
                if (pathf(out, cap, "%s/%s.%s", th->dir[i].path, name, exts[e]) && access(out, R_OK) == 0) {
                    return true;
                }
            }
        }
    }
    char dirs[16][512];
    int nd = linux_xdg_data_dirs(dirs, 16);
    for (int d = 0; d < nd; d++) {
        for (size_t e = 0; e < 2; e++) {
            if (pathf(out, cap, "%s/pixmaps/%s.%s", dirs[d], name, exts[e]) && access(out, R_OK) == 0) return true;
        }
    }
    return false;
}

/* The BMP for an image (made once per source: the name follows its path,
 * size and time), as a Family mruby path; NULL when it cannot be made.
 * *svg counts the SVGs converted. */
static char *icon_for(const char *icon, const linux_icon_themes_t *th, const char *icon_dir, const char *fm_dir,
                      int *svg)
{
#if NATIVE_HAVE_PNG
    char src[1024];
    if (!linux_apps_find_icon(th, icon, src, sizeof(src))) return NULL;
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
        bool is_svg = !has_suffix(src, ".png");
        bool made = is_svg ? svg_to_icon(src, px) : png_to_icon(src, px);
        if (!made || !write_bmp(path, px, LINUX_APPS_ICON_PX, LINUX_APPS_ICON_PX)) return NULL;
        if (is_svg) (*svg)++;
    }
    char fm[400];
    snprintf(fm, sizeof(fm), "%s/%s", fm_dir, file);
    return strdup(fm);
#else
    (void)icon;
    (void)th;
    (void)icon_dir;
    (void)fm_dir;
    (void)svg;
    return NULL;
#endif
}

/* ---------------------------------------------------- Qt's look (L9e) */

bool linux_apps_plasma_qt(const char *root, char *found, size_t cap)
{
    /* Plasma's platform theme for Qt 6 and Qt 5 (plasma-integration),
     * wherever the distribution keeps Qt's plugins. */
    static const char *const pats[] = {
        "%s/usr/lib/*/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
        "%s/usr/lib/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
        "%s/usr/lib*/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
        "%s/usr/lib/*/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme5.so",
        "%s/usr/lib/*/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme.so",
        "%s/usr/lib/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme5.so",
    };
    for (size_t i = 0; i < sizeof(pats) / sizeof(pats[0]); i++) {
        char pat[512];
        if (!pathf(pat, sizeof(pat), pats[i], root ? root : "")) continue;
        glob_t g;
        if (glob(pat, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            snprintf(found, cap, "%s", g.gl_pathv[0]);
            globfree(&g);
            return true;
        }
        globfree(&g);
    }
    return false;
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
        *notshow, *terminal, *wmclass, *path, *mime;
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
    free(f->mime);
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
        else if (strcmp(k, "MimeType") == 0) set_field(&f->mime, v);
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
    if (is_true(f.hidden)) goto out;
    /* NoDisplay and OnlyShowIn / NotShowIn keep an entry out of the
     * launcher, not out of opening files: a type's handler is often an
     * entry made for nothing else (NoDisplay=true). Such an entry is kept
     * when it says which types it opens (L11). */
    bool listed = !is_true(f.nodisplay) && !(f.onlyshow != NULL && !list_has(f.onlyshow, desktop)) &&
                  !(f.notshow != NULL && list_has(f.notshow, desktop));
    bool opens = f.mime != NULL && f.mime[0] != '\0';
    if (!listed && !opens) goto out;
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
    char prog[1024];
    snprintf(prog, sizeof(prog), "%s", exe);
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
    e->exec = strdup(f.exec);
    e->iconname = f.icon ? strdup(f.icon) : NULL;
    e->file = strdup(path);
    e->prog = strdup(prog);
    e->mime = opens ? strdup(f.mime) : NULL;
    e->terminal = is_true(f.terminal);
    e->listed = listed;
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
        int nd = linux_xdg_data_dirs(dirs, 16);
        for (int i = 0; i < nd; i++) {
            char apps[600];
            if (pathf(apps, sizeof(apps), "%s/applications", dirs[i])) walk(&b, apps, "", 0, lang, desktop);
        }
        uint64_t t1 = ms_now();
        qsort(b.e, (size_t)b.n, sizeof(entry_t), by_name);
        for (int i = 0; i < b.nseen; i++) free(b.seen[i]);
        free(b.seen);
        /* The list goes out at once, without its icons: windows are named
         * from it, and an icon (an SVG to convert, L9e) may take a moment.
         * The icons follow into the same list; only this thread replaces
         * the list, so it is still this one meanwhile. */
        char **raw = calloc((size_t)(b.n > 0 ? b.n : 1), sizeof(char *));
        int shown = 0;
        for (int i = 0; i < b.n; i++) {
            /* Only the launcher shows icons: none for an entry kept to open
             * files with (L11). */
            if (raw != NULL && b.e[i].listed) raw[i] = b.e[i].icon;
            else free(b.e[i].icon);
            b.e[i].icon = NULL;
            shown += b.e[i].listed;
        }
        pthread_mutex_lock(&s.lock);
        entry_t *old = s.e;
        int old_n = s.n;
        s.e = b.e;
        s.n = b.n;
        snprintf(s.lang, sizeof(s.lang), "%s", lang);
        snprintf(s.term, sizeof(s.term), "%s", b.term);
        s.term_foot = b.term_foot;
        s.ready = true;
        pthread_mutex_unlock(&s.lock);
        free_list(old, old_n);

        int icons = 0, svgs = 0;
        char theme[64], why[600], chain[600];
        linux_apps_user_icon_theme(theme, sizeof(theme), why, sizeof(why));
        linux_icon_themes_t *th = linux_apps_icon_themes(theme);
        uint64_t t2 = ms_now();
        for (int i = 0; raw != NULL && i < b.n; i++) {
            /* An Icon value already looked up for an earlier entry is not
             * looked up again (a theme has hundreds of directories). */
            int same = -1;
            for (int j = 0; raw[i] != NULL && j < i && same < 0; j++) {
                if (raw[j] != NULL && strcmp(raw[j], raw[i]) == 0) same = j;
            }
            char *icon = NULL;
            if (same >= 0) {
                pthread_mutex_lock(&s.lock);
                icon = b.e[same].icon != NULL ? strdup(b.e[same].icon) : NULL;
                pthread_mutex_unlock(&s.lock);
            } else if (raw[i] != NULL) {
                icon = icon_for(raw[i], th, icon_dir, fm_dir, &svgs);
            }
            if (icon == NULL) continue;
            icons++;
            pthread_mutex_lock(&s.lock);
            b.e[i].icon = icon;
            pthread_mutex_unlock(&s.lock);
        }
        for (int i = 0; raw != NULL && i < b.n; i++) free(raw[i]);
        free(raw);
        fprintf(stderr, "native: linux apps: icon themes %s (%s; %d directories read in %llu ms), "
                        "%d SVG%s converted\n",
                linux_apps_icon_theme_names(th, chain, sizeof(chain)), why, th ? th->ndir : 0,
                (unsigned long long)(t2 - t1), svgs, svgs == 1 ? "" : "s");
        linux_apps_icon_themes_free(th);
        fprintf(stderr, "native: linux apps: %d entries of %d desktop files (%s), %d with icons; "
                        "read in %llu ms, icons in %llu ms; terminal %s; %d more to open files with\n",
                shown, b.nseen, lang, icons, (unsigned long long)(t1 - t0), (unsigned long long)(ms_now() - t1),
                b.term[0] ? b.term : "none (Terminal=true entries left out)", b.n - shown);
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
    /* Ours first, then the machine's own desktop's (comma- or colon-separated
     * in the config, colon-separated like XDG_CURRENT_DESKTOP here). */
    snprintf(s.lists, sizeof(s.lists), "%s%s%s", s.desktop,
             (cfg->mime_desktops && cfg->mime_desktops[0]) ? ":" : "", cfg->mime_desktops ? cfg->mime_desktops : "");
    for (char *p = s.lists; *p; p++) {
        if (*p == ',') *p = ':';
    }
    if (s.want_lang[0] == '\0') snprintf(s.want_lang, sizeof(s.want_lang), "en");
    pthread_mutex_unlock(&s.lock);

    if (!mkdirs(cfg->icon_dir)) {
        fprintf(stderr, "native: linux apps: cannot write icons to %s (%s); no icons\n", cfg->icon_dir,
                strerror(errno));
    }
    snprintf(s_env_desktop, sizeof(s_env_desktop), "XDG_CURRENT_DESKTOP=%s", cfg->desktop_name);

    pthread_mutex_lock(&s.lock);
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
    put(buf, cap, &len, ready ? "ready\n" : "loading\n");
    if (ready && mode == LINUX_APPS_LIST) {
        for (int i = 0; i < s.n; i++) {
            if (!s.e[i].listed) continue;
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

/* ------------------------------------------------ opening files (L11) */

#define CANDS_MAX 64
#define IDS_MAX 32

/* What a file is and what the lists say, read outside the lock. */
typedef struct {
    char type[LINUX_MIME_TYPE_MAX];
    char chain[LINUX_MIME_CHAIN_MAX][LINUX_MIME_TYPE_MAX];
    int nchain;
    linux_mime_lists_t *lists;
} open_query_t;

static void query_load(open_query_t *q, const char *path, const char *desktop)
{
    memset(q, 0, sizeof(*q));
    if (!linux_mime_type(path, q->type, sizeof(q->type))) q->type[0] = '\0';
    q->nchain = linux_mime_chain(q->type, q->chain, LINUX_MIME_CHAIN_MAX);
    q->lists = q->nchain > 0 ? linux_mime_lists_load(desktop) : NULL;
}

/* Under the lock. The entry with this desktop file id ("x" finds
 * "x.desktop" too), or -1. */
static int entry_index_locked(const char *id)
{
    if (id == NULL || id[0] == '\0') return -1;
    for (int i = 0; i < s.n; i++) {
        if (strcmp(s.e[i].id, id) == 0) return i;
    }
    if (!has_suffix(id, ".desktop")) {
        char with[300];
        snprintf(with, sizeof(with), "%s.desktop", id);
        for (int i = 0; i < s.n; i++) {
            if (strcmp(s.e[i].id, with) == 0) return i;
        }
    }
    return -1;
}

/* The ids of a "a.desktop;b.desktop;" value, in order. */
static int split_ids(const char *v, char ids[][256], int max)
{
    int n = 0;
    while (v != NULL && *v && n < max) {
        const char *end = strchr(v, ';');
        size_t len = end ? (size_t)(end - v) : strlen(v);
        while (len > 0 && (*v == ' ')) {
            v++;
            len--;
        }
        if (len > 0 && len < 256) snprintf(ids[n++], 256, "%.*s", (int)len, v);
        if (end == NULL) break;
        v = end + 1;
    }
    return n;
}

static bool in_ids(char ids[][256], int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(ids[i], id) == 0) return true;
    }
    return false;
}

static int add_cand(int *out, int n, int max, int idx)
{
    if (idx < 0 || n >= max) return n;
    for (int i = 0; i < n; i++) {
        if (out[i] == idx) return n;
    }
    out[n] = idx;
    return n + 1;
}

/* Under the lock: the entries that open the query's type, the default
 * first. For each of the type's kinds in turn (the type, then what it is a
 * kind of), through the lists in their order: the first installed entry of
 * a [Default Applications] line is the default (the first found wins);
 * [Added Associations] add entries, [Removed Associations] take entries out
 * of what the lists further down and the desktop entries add; then the
 * entries whose MimeType has the kind, by name. No default line: the first
 * of the rest is the default. Returns how many. */
static int candidates_locked(const open_query_t *q, int *out, int max)
{
    static char removed[IDS_MAX * 4][256];
    int nremoved = 0;
    int def = -1;
    int n = 0;
    int nfiles = linux_mime_lists_count(q->lists);
    for (int c = 0; c < q->nchain; c++) {
        const char *t = q->chain[c];
        for (int f = 0; f < nfiles; f++) {
            char ids[IDS_MAX][256];
            int k;
            if (def < 0) {
                k = split_ids(linux_mime_lists_get(q->lists, f, LINUX_MIME_DEFAULT, t), ids, IDS_MAX);
                for (int i = 0; i < k && def < 0; i++) def = entry_index_locked(ids[i]);
            }
            k = split_ids(linux_mime_lists_get(q->lists, f, LINUX_MIME_ADDED, t), ids, IDS_MAX);
            for (int i = 0; i < k; i++) {
                if (!in_ids(removed, nremoved, ids[i])) n = add_cand(out, n, max, entry_index_locked(ids[i]));
            }
            k = split_ids(linux_mime_lists_get(q->lists, f, LINUX_MIME_REMOVED, t), ids, IDS_MAX);
            for (int i = 0; i < k && nremoved < IDS_MAX * 4; i++) {
                memcpy(removed[nremoved++], ids[i], 256);
            }
        }
        for (int i = 0; i < s.n; i++) {
            if (s.e[i].mime != NULL && list_has(s.e[i].mime, t) && !in_ids(removed, nremoved, s.e[i].id)) {
                n = add_cand(out, n, max, i);
            }
        }
    }
    if (def < 0) return n;
    int m = 0;
    int tmp[CANDS_MAX];
    tmp[m++] = def;
    for (int i = 0; i < n && m < CANDS_MAX && m < max; i++) {
        if (out[i] != def) tmp[m++] = out[i];
    }
    memcpy(out, tmp, (size_t)m * sizeof(int));
    return m;
}

int32_t linux_apps_handlers(const char *path, char *buf, size_t cap)
{
    if (!linux_apps_available() || path == NULL) return -1;
    char desktop[sizeof(s.lists)];
    pthread_mutex_lock(&s.lock);
    snprintf(desktop, sizeof(desktop), "%s", s.lists);
    pthread_mutex_unlock(&s.lock);
    open_query_t q;
    query_load(&q, path, desktop);
    size_t len = 0;
    put(buf, cap, &len, "type\t");
    put(buf, cap, &len, q.type);
    put(buf, cap, &len, "\n");
    pthread_mutex_lock(&s.lock);
    if (s.ready) {
        int idx[CANDS_MAX];
        int n = candidates_locked(&q, idx, CANDS_MAX);
        for (int i = 0; i < n; i++) {
            put(buf, cap, &len, s.e[idx[i]].name);
            put(buf, cap, &len, "\t");
            put(buf, cap, &len, s.e[idx[i]].id);
            put(buf, cap, &len, "\n");
        }
    }
    pthread_mutex_unlock(&s.lock);
    linux_mime_lists_free(q.lists);
    return len > 0x7fffffff ? -1 : (int32_t)len;
}

/* Under the lock: entry e opening path, queued. 0, or -1 with why. */
static int queue_open_locked(const entry_t *e, const char *path, char *why, size_t why_cap)
{
    if (s.qn >= QUEUE_MAX) {
        snprintf(why, why_cap, "too many programs waiting to start");
        return -1;
    }
    char **argv = linux_apps_split_exec_open(e->exec, e->name, e->iconname, e->file, path);
    if (argv == NULL) {
        snprintf(why, why_cap, "%s: its Exec cannot be read", e->id);
        return -1;
    }
    const char *exe = e->prog;
    if (e->terminal) {
        if (s.term[0] == '\0') {
            linux_apps_free_argv(argv);
            snprintf(why, why_cap, "%s runs in a terminal, and there is none", e->id);
            return -1;
        }
        int n = 0;
        while (argv[n] != NULL) n++;
        char **t = calloc((size_t)n + 3, sizeof(char *));
        if (t == NULL) {
            linux_apps_free_argv(argv);
            snprintf(why, why_cap, "out of memory");
            return -1;
        }
        t[0] = strdup(s.term_foot ? "foot" : "x-terminal-emulator");
        t[1] = strdup(s.term_foot ? "--" : "-e");
        for (int i = 0; i < n; i++) t[i + 2] = argv[i];
        free(argv);
        argv = t;
        exe = s.term;
    }
    launch_t *l = &s.q[s.qn];
    memset(l, 0, sizeof(*l));
    l->argv = argv;
    l->exe = dup_str(exe);
    l->dir = dup_str(e->dir);
    l->name = dup_str(e->name);
    l->key = dup_str(e->key);
    l->idkey = dup_str(e->idkey);
    if (l->exe == NULL || l->name == NULL) {
        linux_apps_free_argv(l->argv);
        free(l->exe);
        free(l->dir);
        free(l->name);
        free(l->key);
        free(l->idkey);
        snprintf(why, why_cap, "out of memory");
        return -1;
    }
    s.qn++;
    return 0;
}

int linux_apps_open(const char *id, const char *path, char *name, size_t name_cap, char *why, size_t why_cap)
{
    char dummy[8];
    if (why == NULL || why_cap == 0) {
        why = dummy;
        why_cap = sizeof(dummy);
    }
    why[0] = '\0';
    if (name != NULL && name_cap > 0) name[0] = '\0';
    if (!linux_apps_available()) {
        snprintf(why, why_cap, "no Linux applications here");
        return -1;
    }
    struct stat st;
    if (path == NULL || path[0] != '/' || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(why, why_cap, "%s is not a file", path ? path : "(none)");
        return -1;
    }
    char desktop[sizeof(s.lists)];
    pthread_mutex_lock(&s.lock);
    snprintf(desktop, sizeof(desktop), "%s", s.lists);
    pthread_mutex_unlock(&s.lock);
    bool by_default = id == NULL || id[0] == '\0';
    open_query_t q;
    if (by_default) query_load(&q, path, desktop);
    pthread_mutex_lock(&s.lock);
    int rc = -1;
    int idx = -1;
    if (!s.ready) {
        snprintf(why, why_cap, "the applications are still being read");
    } else if (by_default) {
        int c[CANDS_MAX];
        int n = candidates_locked(&q, c, CANDS_MAX);
        if (n > 0) idx = c[0];
        else snprintf(why, why_cap, "no application opens %s", q.type[0] ? q.type : "files of this kind");
    } else {
        idx = entry_index_locked(id);
        if (idx < 0) snprintf(why, why_cap, "no application %s", id);
    }
    if (idx >= 0) {
        rc = queue_open_locked(&s.e[idx], path, why, why_cap);
        if (name != NULL && name_cap > 0) copy_utf8(name, name_cap, s.e[idx].name);
    }
    pthread_mutex_unlock(&s.lock);
    if (by_default) linux_mime_lists_free(q.lists);
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
