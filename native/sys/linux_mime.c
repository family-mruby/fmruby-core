/*
 * linux_mime.c - a file's type and the mimeapps.list files (linux_mime.h,
 * doc/linux_native L11).
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux_mime.h"

#define MAX_LIST_BYTES (256 * 1024)
#define MAX_GLOBS_BYTES (4 * 1024 * 1024)
#define DIRS_MAX 16

/* ------------------------------------------------------------ helpers */

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

/* A whole small file, NUL-terminated (malloc'd), or NULL. */
static char *slurp(const char *path, size_t max)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size > max) {
        close(fd);
        return NULL;
    }
    char *buf = malloc((size_t)st.st_size + 1);
    if (buf == NULL) {
        close(fd);
        return NULL;
    }
    size_t got = 0;
    while (got < (size_t)st.st_size) {
        ssize_t n = read(fd, buf + got, (size_t)st.st_size - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    buf[got] = '\0';
    return buf;
}

/* A colon-separated list of absolute directories into dirs (no repeats). */
static int add_dirs(char dirs[][512], int n, int max, const char *list)
{
    const char *p = list;
    while (p && *p && n < max) {
        const char *end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && len < 512 && p[0] == '/') {
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

/* $<var> when absolute, else $HOME/<rel>; false with neither. */
static bool home_dir(const char *var, const char *rel, char *out, size_t cap)
{
    const char *v = getenv(var);
    if (v != NULL && v[0] == '/') {
        snprintf(out, cap, "%s", v);
        return true;
    }
    const char *home = getenv("HOME");
    if (home == NULL || home[0] != '/') return false;
    snprintf(out, cap, "%s/%s", home, rel);
    return true;
}

int linux_xdg_data_dirs(char dirs[][512], int max)
{
    int n = 0;
    if (max <= 0) return 0;
    if (home_dir("XDG_DATA_HOME", ".local/share", dirs[0], 512)) n = 1;
    const char *sys = getenv("XDG_DATA_DIRS");
    if (sys == NULL || sys[0] == '\0') sys = "/usr/local/share:/usr/share";
    return add_dirs(dirs, n, max, sys);
}

static int config_dirs(char dirs[][512], int max)
{
    int n = 0;
    if (max <= 0) return 0;
    if (home_dir("XDG_CONFIG_HOME", ".config", dirs[0], 512)) n = 1;
    const char *sys = getenv("XDG_CONFIG_DIRS");
    if (sys == NULL || sys[0] == '\0') sys = "/etc/xdg";
    return add_dirs(dirs, n, max, sys);
}

/* ------------------------------------------------------------- the type */

/* Used only where no data directory has shared-mime-info's globs2 at all. */
static const struct {
    const char *ext;
    const char *type;
} k_ext[] = {
    { "pdf", "application/pdf" },      { "png", "image/png" },
    { "jpg", "image/jpeg" },           { "jpeg", "image/jpeg" },
    { "gif", "image/gif" },            { "bmp", "image/bmp" },
    { "svg", "image/svg+xml" },        { "webp", "image/webp" },
    { "html", "text/html" },           { "htm", "text/html" },
    { "txt", "text/plain" },           { "md", "text/markdown" },
    { "csv", "text/csv" },             { "xml", "application/xml" },
    { "json", "application/json" },    { "c", "text/x-csrc" },
    { "h", "text/x-chdr" },            { "py", "text/x-python" },
    { "rb", "application/x-ruby" },    { "sh", "application/x-shellscript" },
    { "mp3", "audio/mpeg" },           { "ogg", "audio/ogg" },
    { "wav", "audio/x-wav" },          { "flac", "audio/flac" },
    { "mp4", "video/mp4" },            { "mkv", "video/x-matroska" },
    { "webm", "video/webm" },          { "zip", "application/zip" },
    { "gz", "application/gzip" },      { "tar", "application/x-tar" },
    { "odt", "application/vnd.oasis.opendocument.text" },
    { "ods", "application/vnd.oasis.opendocument.spreadsheet" },
    { "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
    { "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
    { "pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation" },
};

static bool by_extension(const char *name, char *out, size_t cap)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0') return false;
    for (size_t i = 0; i < sizeof(k_ext) / sizeof(k_ext[0]); i++) {
        if (strcasecmp(dot + 1, k_ext[i].ext) == 0) {
            snprintf(out, cap, "%s", k_ext[i].type);
            return true;
        }
    }
    return false;
}

bool linux_mime_type(const char *path, char *out, size_t cap)
{
    if (path == NULL || cap == 0) return false;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (name[0] == '\0') return false;
    /* xdgmime's way: a pattern marked "cs" is matched against the name as
     * it is, every other one against the name in lower case (so "*.C" of
     * C++, listed twice -- marked and not -- never takes "x.c"). */
    char lower[256];
    snprintf(lower, sizeof(lower), "%s", name);
    for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);
    char dirs[DIRS_MAX][512];
    int nd = linux_xdg_data_dirs(dirs, DIRS_MAX);
    bool any = false;
    int best_w = -1;
    size_t best_len = 0;
    bool best_cs = false;
    char best[LINUX_MIME_TYPE_MAX] = "";
    for (int i = 0; i < nd; i++) {
        char gp[600];
        if (!pathf(gp, sizeof(gp), "%s/mime/globs2", dirs[i])) continue;
        char *text = slurp(gp, MAX_GLOBS_BYTES);
        if (text == NULL) continue;
        any = true;
        char *save = NULL;
        for (char *line = strtok_r(text, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
            if (line[0] == '#') continue;
            /* weight:type:glob[:flags] */
            char *c1 = strchr(line, ':');
            if (c1 == NULL) continue;
            char *c2 = strchr(c1 + 1, ':');
            if (c2 == NULL) continue;
            int w = atoi(line);
            *c2 = '\0';
            const char *type = c1 + 1;
            char *glob = c2 + 1;
            char *c3 = strchr(glob, ':');
            bool cs = false;
            if (c3 != NULL) {
                *c3 = '\0';
                cs = strstr(c3 + 1, "cs") != NULL;
            }
            if (glob[0] == '\0' || strcmp(glob, "__NOGLOBS__") == 0) continue;
            size_t gl = strlen(glob);
            /* The higher weight, then the longer pattern, then a "cs" one;
             * else the first met (the more important directory). */
            if (w < best_w) continue;
            if (w == best_w && (gl < best_len || (gl == best_len && (best_cs || !cs)))) continue;
            if (fnmatch(glob, cs ? name : lower, 0) != 0) continue;
            best_w = w;
            best_len = gl;
            best_cs = cs;
            snprintf(best, sizeof(best), "%s", type);
        }
        free(text);
    }
    if (best[0] != '\0') {
        snprintf(out, cap, "%s", best);
        return true;
    }
    if (any) return false;
    return by_extension(name, out, cap);
}

/* The second word of the line of file (in each data directory) whose first
 * word is key; for aliases (one answer) and subclasses (each answer, by
 * calling with skip). */
static int lookup_pairs(const char *file, const char *key, char out[][LINUX_MIME_TYPE_MAX], int max)
{
    char dirs[DIRS_MAX][512];
    int nd = linux_xdg_data_dirs(dirs, DIRS_MAX);
    int n = 0;
    for (int i = 0; i < nd && n < max; i++) {
        char p[600];
        if (!pathf(p, sizeof(p), "%s/mime/%s", dirs[i], file)) continue;
        char *text = slurp(p, MAX_LIST_BYTES);
        if (text == NULL) continue;
        char *save = NULL;
        for (char *line = strtok_r(text, "\n", &save); line != NULL && n < max; line = strtok_r(NULL, "\n", &save)) {
            char *sp = strchr(line, ' ');
            if (sp == NULL) continue;
            *sp = '\0';
            if (strcmp(line, key) != 0) continue;
            const char *v = sp + 1;
            bool dup = false;
            for (int k = 0; k < n; k++) dup |= strcmp(out[k], v) == 0;
            if (!dup) snprintf(out[n++], LINUX_MIME_TYPE_MAX, "%s", v);
        }
        free(text);
    }
    return n;
}

static bool chain_has(char out[][LINUX_MIME_TYPE_MAX], int n, const char *t)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i], t) == 0) return true;
    }
    return false;
}

static int chain_add(char out[][LINUX_MIME_TYPE_MAX], int n, int max, const char *t)
{
    if (n >= max || t[0] == '\0' || chain_has(out, n, t)) return n;
    snprintf(out[n], LINUX_MIME_TYPE_MAX, "%s", t);
    return n + 1;
}

int linux_mime_chain(const char *type, char out[][LINUX_MIME_TYPE_MAX], int max)
{
    if (type == NULL || type[0] == '\0' || max <= 0) return 0;
    int n = chain_add(out, 0, max, type);
    char one[1][LINUX_MIME_TYPE_MAX];
    if (lookup_pairs("aliases", type, one, 1) == 1) n = chain_add(out, n, max, one[0]);
    /* Breadth first through the parents of each type met so far. */
    for (int i = 0; i < n; i++) {
        char parents[8][LINUX_MIME_TYPE_MAX];
        int np = lookup_pairs("subclasses", out[i], parents, 8);
        for (int k = 0; k < np; k++) n = chain_add(out, n, max, parents[k]);
    }
    if (strncmp(type, "text/", 5) == 0) n = chain_add(out, n, max, "text/plain");
    return n;
}

/* ------------------------------------------------- the mimeapps.list files */

typedef struct {
    int group;
    char *key;
    char *val;
} pair_t;

typedef struct {
    char *path;
    bool defaults_only;     /* defaults.list */
    pair_t *p;
    int n;
} list_file_t;

struct linux_mime_lists {
    list_file_t *f;
    int n, cap;
};

static bool add_list_file(linux_mime_lists_t *l, const char *path, bool defaults_only)
{
    for (int i = 0; i < l->n; i++) {
        if (strcmp(l->f[i].path, path) == 0) return true;
    }
    char *text = slurp(path, MAX_LIST_BYTES);
    if (text == NULL) return true;
    if (l->n + 1 > l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        list_file_t *v = realloc(l->f, (size_t)cap * sizeof(list_file_t));
        if (v == NULL) {
            free(text);
            return false;
        }
        l->f = v;
        l->cap = cap;
    }
    list_file_t *f = &l->f[l->n++];
    memset(f, 0, sizeof(*f));
    f->path = strdup(path);
    f->defaults_only = defaults_only;
    int group = -1, cap = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = '\0';
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0' || *line == '#') continue;
        if (*line == '[') {
            if (strcmp(line, "[Default Applications]") == 0) group = LINUX_MIME_DEFAULT;
            else if (strcmp(line, "[Added Associations]") == 0 && !defaults_only) group = LINUX_MIME_ADDED;
            else if (strcmp(line, "[Removed Associations]") == 0 && !defaults_only) group = LINUX_MIME_REMOVED;
            else group = -1;
            continue;
        }
        if (group < 0) continue;
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        char *ke = eq;
        while (ke > line && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
        *ke = '\0';
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        if (f->n + 1 > cap) {
            int nc = cap ? cap * 2 : 32;
            pair_t *pv = realloc(f->p, (size_t)nc * sizeof(pair_t));
            if (pv == NULL) break;
            f->p = pv;
            cap = nc;
        }
        f->p[f->n].group = group;
        f->p[f->n].key = strdup(line);
        f->p[f->n].val = strdup(v);
        f->n++;
    }
    free(text);
    return true;
}

#define LIST_DESKTOPS_MAX 4

linux_mime_lists_t *linux_mime_lists_load(const char *desktops)
{
    linux_mime_lists_t *l = calloc(1, sizeof(*l));
    if (l == NULL) return NULL;
    /* <name>-mimeapps.list for each name, in order, lower case (the spec). */
    char own[LIST_DESKTOPS_MAX][96];
    int no = 0;
    const char *d = desktops;
    while (d != NULL && *d && no < LIST_DESKTOPS_MAX) {
        const char *end = strchr(d, ':');
        size_t len = end ? (size_t)(end - d) : strlen(d);
        if (len > 0 && len < 64) {
            snprintf(own[no], sizeof(own[no]), "%.*s-mimeapps.list", (int)len, d);
            for (char *p = own[no]; *p; p++) *p = (char)tolower((unsigned char)*p);
            no++;
        }
        if (end == NULL) break;
        d = end + 1;
    }
    char cfg[DIRS_MAX][512], data[DIRS_MAX][512];
    int nc = config_dirs(cfg, DIRS_MAX);
    int nd = linux_xdg_data_dirs(data, DIRS_MAX);
    char p[700];
    for (int i = 0; i < nc; i++) {
        for (int k = 0; k < no; k++) {
            if (pathf(p, sizeof(p), "%s/%s", cfg[i], own[k])) add_list_file(l, p, false);
        }
        if (pathf(p, sizeof(p), "%s/mimeapps.list", cfg[i])) add_list_file(l, p, false);
    }
    for (int i = 0; i < nd; i++) {
        for (int k = 0; k < no; k++) {
            if (pathf(p, sizeof(p), "%s/applications/%s", data[i], own[k])) add_list_file(l, p, false);
        }
        if (pathf(p, sizeof(p), "%s/applications/mimeapps.list", data[i])) add_list_file(l, p, false);
    }
    for (int i = 0; i < nd; i++) {
        if (pathf(p, sizeof(p), "%s/applications/defaults.list", data[i])) add_list_file(l, p, true);
    }
    return l;
}

void linux_mime_lists_free(linux_mime_lists_t *l)
{
    if (l == NULL) return;
    for (int i = 0; i < l->n; i++) {
        for (int k = 0; k < l->f[i].n; k++) {
            free(l->f[i].p[k].key);
            free(l->f[i].p[k].val);
        }
        free(l->f[i].p);
        free(l->f[i].path);
    }
    free(l->f);
    free(l);
}

int linux_mime_lists_count(const linux_mime_lists_t *l)
{
    return l ? l->n : 0;
}

const char *linux_mime_lists_path(const linux_mime_lists_t *l, int file)
{
    return (l && file >= 0 && file < l->n) ? l->f[file].path : NULL;
}

const char *linux_mime_lists_get(const linux_mime_lists_t *l, int file, int group, const char *type)
{
    if (l == NULL || file < 0 || file >= l->n || type == NULL) return NULL;
    const list_file_t *f = &l->f[file];
    for (int k = 0; k < f->n; k++) {
        if (f->p[k].group == group && strcmp(f->p[k].key, type) == 0) return f->p[k].val;
    }
    return NULL;
}
