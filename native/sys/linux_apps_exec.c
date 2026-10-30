/*
 * linux_apps_exec.c - the Exec field of a desktop entry (the spec's quoting and
 * field codes), for the launcher's Linux applications.
 * Split from linux_apps.c (L16), which has the overview; what the files
 * share is in linux_apps_internal.h.
 */

#include "linux_apps_internal.h"

/* ------------------------------------------------- the Exec field (spec) */

/* The string escapes of a value: \s \n \t \r \\ (in place). */
void unescape_value(char *v)
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

