/*
 * linux_apps_util.c - the small helpers of the launcher's Linux applications
 * (strings, argument vectors, time, programs on PATH, paths).
 * Split from linux_apps.c (L16), which has the overview; what the files
 * share is in linux_apps_internal.h.
 */

#include "linux_apps_internal.h"

/* --------------------------------------------------------------- helpers */

char *dup_str(const char *a)
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

void free_list(entry_t *e, int n)
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

char **copy_argv(char *const *argv)
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

uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void sleep_ms(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Copy at most cap - 1 bytes, not cutting a UTF-8 character in two. */
void copy_utf8(char *out, size_t cap, const char *in)
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
bool find_program(const char *name, char *out, size_t cap)
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
bool pathf(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n >= 0 && (size_t)n < cap;
}

const char *base_name(const char *path)
{
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

bool same_word(const char *a, const char *b)
{
    return a != NULL && b != NULL && a[0] != '\0' && strcasecmp(a, b) == 0;
}

