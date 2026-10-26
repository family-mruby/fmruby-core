/*
 * etc_update.c - see etc_update.h.
 *
 * The TOML files are read twice: tomlc99 answers "does the user have this
 * key / table / service", and a small line scanner finds where each
 * statement of the text begins and ends (a key with its multi-line value, a
 * header, the comments above them), so what is missing can be copied from
 * the distribution's text and inserted into the user's without touching
 * anything else. Static buffers: the files are a few kilobytes, and this
 * runs once, before the machine exists.
 */

#include "etc_update.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "toml.h"

#define ETC_FILE_MAX   (64 * 1024)
#define ETC_STMT_MAX   1024
#define ETC_NAME_MAX   128
#define ETC_INSERT_MAX 256
#define ETC_PATH_MAX   1024
#define ETC_DEPTH_MAX  4

typedef enum {
    ST_BLANK,
    ST_COMMENT,
    ST_TABLE,        /* [name] */
    ST_ARRAY_TABLE,  /* [[name]] */
    ST_KEYVAL,
} st_kind_t;

typedef struct {
    st_kind_t kind;
    size_t start, end;          /* bytes; end is past the last newline */
    char table[ETC_NAME_MAX];   /* the header in effect ("" = the root), or the header's own name */
    bool in_array;              /* that header is an array of tables */
    char key[ETC_NAME_MAX];     /* ST_KEYVAL: the key as written */
} stmt_t;

typedef struct {
    const char *text;
    size_t len;
    stmt_t *st;
    int n;
} doc_t;

/* Text to put into the user's file: from the seed [from, to) at `at`, with
 * an optional header line first (a key whose table the user's text never
 * names) and a blank line before it. */
typedef struct {
    size_t at;
    size_t from, to;
    int seq;
    bool blank_before;
    char label[ETC_NAME_MAX * 2]; /* for the log: the key or section, "" for a header */
} insert_t;

static char s_seed_text[ETC_FILE_MAX + 1];
static char s_user_text[ETC_FILE_MAX + 1];
static char s_parse_buf[ETC_FILE_MAX * 2 + 1];
static char s_out[ETC_FILE_MAX * 2 + 1];
static stmt_t s_seed_st[ETC_STMT_MAX];
static stmt_t s_user_st[ETC_STMT_MAX];
static insert_t s_ins[ETC_INSERT_MAX];

static native_etc_log_fn s_log;
static void *s_log_ctx;

static void logf_(const char *fmt, ...)
{
    if (s_log == NULL) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    s_log(s_log_ctx, line);
}

/* ------------------------------------------------------------- reading */

/* 0 ok, 1 too large, -1 cannot read. */
static int read_file(const char *path, char *buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    size_t n = fread(buf, 1, ETC_FILE_MAX + 1, f);
    int err = ferror(f);
    fclose(f);
    if (err) return -1;
    if (n > ETC_FILE_MAX) return 1;
    buf[n] = '\0';
    *len = n;
    return 0;
}

static toml_table_t *parse_copy(const char *text, size_t len, char *err, int errsz)
{
    if (len >= sizeof(s_parse_buf)) {
        snprintf(err, (size_t)errsz, "too large");
        return NULL;
    }
    memcpy(s_parse_buf, text, len);
    s_parse_buf[len] = '\0';
    return toml_parse(s_parse_buf, err, errsz);
}

/* ------------------------------------------------------------- scanning */

static size_t line_end(const char *t, size_t len, size_t p)
{
    while (p < len && t[p] != '\n') p++;
    return p < len ? p + 1 : p;
}

/* A header or key with the blanks outside quotes taken out: "[ a . b ]"
 * and "[a.b]" name the same table. */
static void squeeze(const char *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    char q = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char c = s[i];
        if (q) {
            if (c == q) q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == ' ' || c == '\t') {
            continue;
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

/* Past a value that starts at p (after the '='), across lines while a
 * bracket is open or a multi-line string has not ended. */
static size_t value_end(const char *t, size_t len, size_t p)
{
    int depth = 0;
    while (p < len) {
        char c = t[p];
        if (c == '"' && p + 2 < len && t[p + 1] == '"' && t[p + 2] == '"') {
            p += 3;
            while (p < len && !(t[p] == '"' && p + 2 < len && t[p + 1] == '"' && t[p + 2] == '"')) {
                p += (t[p] == '\\' && p + 1 < len) ? 2 : 1;
            }
            p = p + 3 <= len ? p + 3 : len;
            continue;
        }
        if (c == '\'' && p + 2 < len && t[p + 1] == '\'' && t[p + 2] == '\'') {
            p += 3;
            while (p < len && !(t[p] == '\'' && p + 2 < len && t[p + 1] == '\'' && t[p + 2] == '\'')) p++;
            p = p + 3 <= len ? p + 3 : len;
            continue;
        }
        if (c == '"') {
            p++;
            while (p < len && t[p] != '"' && t[p] != '\n') p += (t[p] == '\\' && p + 1 < len) ? 2 : 1;
            if (p < len && t[p] == '"') p++;
            continue;
        }
        if (c == '\'') {
            p++;
            while (p < len && t[p] != '\'' && t[p] != '\n') p++;
            if (p < len && t[p] == '\'') p++;
            continue;
        }
        if (c == '#') {
            while (p < len && t[p] != '\n') p++;
            continue;
        }
        if (c == '[' || c == '{') depth++;
        if ((c == ']' || c == '}') && depth > 0) depth--;
        if (c == '\n' && depth == 0) return p + 1;
        p++;
    }
    return p;
}

static int scan(doc_t *d)
{
    const char *t = d->text;
    size_t len = d->len;
    size_t p = 0;
    char table[ETC_NAME_MAX] = "";
    bool in_array = false;
    d->n = 0;
    while (p < len) {
        if (d->n >= ETC_STMT_MAX) return -1;
        stmt_t *s = &d->st[d->n];
        memset(s, 0, sizeof(*s));
        s->start = p;
        size_t q = p;
        while (q < len && (t[q] == ' ' || t[q] == '\t' || t[q] == '\r')) q++;
        if (q >= len || t[q] == '\n') {
            s->kind = ST_BLANK;
            s->end = line_end(t, len, q);
        } else if (t[q] == '#') {
            s->kind = ST_COMMENT;
            s->end = line_end(t, len, q);
        } else if (t[q] == '[') {
            bool arr = q + 1 < len && t[q + 1] == '[';
            size_t a = q + (arr ? 2 : 1);
            size_t b = a;
            while (b < len && t[b] != ']' && t[b] != '\n') b++;
            if (b >= len || t[b] != ']') return -1;
            s->kind = arr ? ST_ARRAY_TABLE : ST_TABLE;
            squeeze(t + a, b - a, s->table, sizeof(s->table));
            s->in_array = arr;
            s->end = line_end(t, len, b);
            snprintf(table, sizeof(table), "%s", s->table);
            in_array = arr;
        } else {
            size_t eq = q;
            char qch = 0;
            while (eq < len && t[eq] != '\n') {
                if (qch) {
                    if (t[eq] == qch) qch = 0;
                } else if (t[eq] == '"' || t[eq] == '\'') {
                    qch = t[eq];
                } else if (t[eq] == '=') {
                    break;
                }
                eq++;
            }
            if (eq >= len || t[eq] != '=') return -1;
            s->kind = ST_KEYVAL;
            squeeze(t + q, eq - q, s->key, sizeof(s->key));
            snprintf(s->table, sizeof(s->table), "%s", table);
            s->in_array = in_array;
            s->end = value_end(t, len, eq + 1);
        }
        p = s->end;
        d->n++;
    }
    return 0;
}

/* The first statement of the comment lines right above statement i (no
 * blank line between), or i itself. */
static int lead(const doc_t *d, int i)
{
    int j = i;
    while (j > 0 && d->st[j - 1].kind == ST_COMMENT) j--;
    return j;
}

/* ------------------------------------------------------------- the tree */

/* The next component of a dotted name, quotes taken off. */
static const char *next_part(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    char q = 0;
    while (*s) {
        char c = *s;
        if (q) {
            if (c == q) {
                q = 0;
            } else if (o + 1 < cap) {
                out[o++] = c;
            }
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '.') {
            s++;
            break;
        } else if (o + 1 < cap) {
            out[o++] = c;
        }
        s++;
    }
    out[o] = '\0';
    return s;
}

/* The table a header names, or NULL. "" is the root. */
static toml_table_t *table_at(toml_table_t *root, const char *name)
{
    toml_table_t *t = root;
    const char *s = name;
    char part[ETC_NAME_MAX];
    while (t != NULL && *s) {
        s = next_part(s, part, sizeof(part));
        t = toml_table_in(t, part);
    }
    return t;
}

/* Whether a (possibly dotted) key is in table t. */
static bool has_key(toml_table_t *t, const char *key)
{
    const char *s = key;
    char part[ETC_NAME_MAX];
    while (t != NULL) {
        s = next_part(s, part, sizeof(part));
        if (*s == '\0') return toml_key_exists(t, part) != 0;
        t = toml_table_in(t, part);
    }
    return false;
}

/* The parent table of a header and its last component. */
static toml_table_t *parent_of(toml_table_t *root, const char *name, char *last, size_t cap)
{
    char parent[ETC_NAME_MAX];
    snprintf(parent, sizeof(parent), "%s", name);
    /* the last '.' outside quotes */
    char q = 0;
    int cut = -1;
    for (int i = 0; parent[i]; i++) {
        if (q) {
            if (parent[i] == q) q = 0;
        } else if (parent[i] == '"' || parent[i] == '\'') {
            q = parent[i];
        } else if (parent[i] == '.') {
            cut = i;
        }
    }
    const char *tail = name;
    if (cut >= 0) {
        parent[cut] = '\0';
        tail = name + cut + 1;
    } else {
        parent[0] = '\0';
    }
    next_part(tail, last, cap);
    return table_at(root, parent);
}

static bool header_exists(toml_table_t *root, const char *name)
{
    char last[ETC_NAME_MAX];
    toml_table_t *p = parent_of(root, name, last, sizeof(last));
    return p != NULL && toml_key_exists(p, last);
}

static void first_part(const char *name, char *out, size_t cap)
{
    next_part(name, out, cap);
}

/* ------------------------------------------------------------- writing */

static int n_ins;

static int add_insert(size_t at, size_t from, size_t to, bool blank_before, const char *label)
{
    if (n_ins >= ETC_INSERT_MAX) return -1;
    insert_t *x = &s_ins[n_ins];
    x->at = at;
    x->from = from;
    x->to = to;
    x->seq = n_ins;
    x->blank_before = blank_before;
    snprintf(x->label, sizeof(x->label), "%s", label);
    n_ins++;
    return 0;
}

static int cmp_insert(const void *a, const void *b)
{
    const insert_t *x = a, *y = b;
    if (x->at != y->at) return x->at < y->at ? -1 : 1;
    return x->seq - y->seq;
}

static bool out_put(size_t *o, const char *s, size_t n)
{
    if (*o + n >= sizeof(s_out)) return false;
    memcpy(s_out + *o, s, n);
    *o += n;
    return true;
}

/* The user's text with every insertion in place. */
static bool build_output(const doc_t *user, const doc_t *seed, size_t *out_len)
{
    qsort(s_ins, (size_t)n_ins, sizeof(s_ins[0]), cmp_insert);
    size_t o = 0;
    size_t p = 0;
    for (int i = 0; i < n_ins; i++) {
        const insert_t *x = &s_ins[i];
        if (!out_put(&o, user->text + p, x->at - p)) return false;
        p = x->at;
        if (o > 0 && s_out[o - 1] != '\n' && !out_put(&o, "\n", 1)) return false;
        if (x->blank_before && o > 1 && !(s_out[o - 1] == '\n' && s_out[o - 2] == '\n') &&
            !out_put(&o, "\n", 1)) {
            return false;
        }
        size_t to = x->to;
        /* No blank lines at the end of what is inserted. */
        while (to > x->from && (seed->text[to - 1] == '\n' || seed->text[to - 1] == ' ' ||
                                seed->text[to - 1] == '\t' || seed->text[to - 1] == '\r')) {
            to--;
        }
        if (!out_put(&o, seed->text + x->from, to - x->from) || !out_put(&o, "\n", 1)) return false;
    }
    if (!out_put(&o, user->text + p, user->len - p)) return false;
    s_out[o] = '\0';
    *out_len = o;
    return true;
}

static int write_replace(const char *path, const char *data, size_t len)
{
    char tmp[ETC_PATH_MAX + 16];
    snprintf(tmp, sizeof(tmp), "%s.fmrb-new", path);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) return -1;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------- the rules */

/* Where a key of table `table` goes in the user's text: after the table's
 * last key (or its header). *found is false when the user's text has no
 * header of that name (the table exists only implicitly). */
static size_t key_insert_point(const doc_t *u, const char *table, bool *found)
{
    *found = true;
    if (table[0] == '\0') {
        int last = -1, first_header = -1;
        for (int i = 0; i < u->n; i++) {
            const stmt_t *s = &u->st[i];
            if (s->kind == ST_TABLE || s->kind == ST_ARRAY_TABLE) {
                first_header = i;
                break;
            }
            if (s->kind == ST_KEYVAL) last = i;
        }
        if (last >= 0) return u->st[last].end;
        if (first_header >= 0) return u->st[lead(u, first_header)].start;
        return u->len;
    }
    int header = -1, last = -1;
    for (int i = 0; i < u->n; i++) {
        const stmt_t *s = &u->st[i];
        if (s->kind == ST_TABLE && strcmp(s->table, table) == 0) {
            header = i;
            last = i;
        } else if (header >= 0 && s->kind == ST_KEYVAL && !s->in_array && strcmp(s->table, table) == 0) {
            last = i;
        }
    }
    if (header < 0) {
        *found = false;
        return u->len;
    }
    return u->st[last].end;
}

/* The end of the block a header starts: up to the lead comments of the next
 * header (keys and comments of the block itself included). */
static size_t block_end(const doc_t *d, int header, bool same_service, const char *service)
{
    for (int j = header + 1; j < d->n; j++) {
        const stmt_t *s = &d->st[j];
        if (s->kind != ST_TABLE && s->kind != ST_ARRAY_TABLE) continue;
        if (same_service) {
            char first[ETC_NAME_MAX];
            first_part(s->table, first, sizeof(first));
            if (strcmp(first, service) == 0) continue;
        }
        return d->st[lead(d, j)].start;
    }
    return d->len;
}

/* system_conf.toml: the keys, tables and arrays of tables the user lacks. */
static int plan_keys(const doc_t *seed, toml_table_t *seed_root, const doc_t *user, toml_table_t *user_root)
{
    (void)seed_root;
    int added = 0;
    char done_arrays[16][ETC_NAME_MAX];
    int n_done = 0;
    for (int i = 0; i < seed->n; i++) {
        const stmt_t *s = &seed->st[i];
        char label[ETC_NAME_MAX * 2];
        if (s->kind == ST_KEYVAL && !s->in_array) {
            toml_table_t *t = table_at(user_root, s->table);
            if (t == NULL) continue;          /* the whole table is added below */
            if (has_key(t, s->key)) continue;
            bool found;
            size_t at = key_insert_point(user, s->table, &found);
            int from = lead(seed, i);
            if (s->table[0]) {
                snprintf(label, sizeof(label), "%s.%s", s->table, s->key);
            } else {
                snprintf(label, sizeof(label), "%s", s->key);
            }
            if (!found) {
                /* The table exists only implicitly in the user's text
                 * ([a.b] without [a]): name it before the key, at the end. */
                int h = -1;
                for (int j = i; j >= 0; j--) {
                    if (seed->st[j].kind == ST_TABLE && strcmp(seed->st[j].table, s->table) == 0) {
                        h = j;
                        break;
                    }
                }
                if (h < 0) continue;
                if (add_insert(at, seed->st[h].start, seed->st[h].end, true, "") != 0) return -1;
                if (add_insert(at, seed->st[from].start, s->end, false, label) != 0) return -1;
            } else if (add_insert(at, seed->st[from].start, s->end, from != i, label) != 0) {
                return -1;
            }
            added++;
        } else if (s->kind == ST_TABLE) {
            if (header_exists(user_root, s->table)) continue;
            snprintf(label, sizeof(label), "[%s]", s->table);
            if (add_insert(user->len, seed->st[lead(seed, i)].start, block_end(seed, i, false, ""), true,
                           label) != 0) {
                return -1;
            }
            added++;
        } else if (s->kind == ST_ARRAY_TABLE) {
            bool done = false;
            for (int k = 0; k < n_done; k++) done |= strcmp(done_arrays[k], s->table) == 0;
            /* The user's list is the user's: only an array the user has
             * none of is brought over, all of its entries. */
            if (!done && header_exists(user_root, s->table)) continue;
            if (!done) {
                if (n_done >= 16) return -1;
                snprintf(done_arrays[n_done++], ETC_NAME_MAX, "%s", s->table);
            }
            snprintf(label, sizeof(label), "[[%s]]", s->table);
            if (add_insert(user->len, seed->st[lead(seed, i)].start, block_end(seed, i, false, ""), true,
                           label) != 0) {
                return -1;
            }
            added++;
        }
    }
    return added;
}

/* services.toml: whole service sections ([name] and its [name.*]). */
static int plan_services(const doc_t *seed, const doc_t *user, toml_table_t *user_root)
{
    int added = 0;
    char done[64][ETC_NAME_MAX];
    int n_done = 0;
    for (int i = 0; i < seed->n; i++) {
        const stmt_t *s = &seed->st[i];
        if (s->kind != ST_TABLE && s->kind != ST_ARRAY_TABLE) continue;
        char service[ETC_NAME_MAX];
        first_part(s->table, service, sizeof(service));
        bool seen = false;
        for (int k = 0; k < n_done; k++) seen |= strcmp(done[k], service) == 0;
        if (seen) continue;
        if (n_done >= 64) return -1;
        snprintf(done[n_done++], ETC_NAME_MAX, "%s", service);
        if (toml_key_exists(user_root, service)) continue;
        char label[ETC_NAME_MAX + 4];
        snprintf(label, sizeof(label), "[%s]", service);
        if (add_insert(user->len, seed->st[lead(seed, i)].start, block_end(seed, i, true, service), true,
                       label) != 0) {
            return -1;
        }
        added++;
    }
    return added;
}

typedef enum { MERGE_KEYS, MERGE_SERVICES } merge_kind_t;

static int merge_file(const char *name, const char *seed_path, const char *user_path, merge_kind_t kind)
{
    size_t seed_len = 0, user_len = 0;
    int r = read_file(seed_path, s_seed_text, &seed_len);
    if (r != 0) {
        logf_("%s: left alone: the distribution's copy %s", name, r > 0 ? "is too large" : "cannot be read");
        return r > 0 ? 0 : -1;
    }
    r = read_file(user_path, s_user_text, &user_len);
    if (r != 0) {
        logf_("%s: left alone: %s", name, r > 0 ? "too large" : strerror(errno));
        return r > 0 ? 0 : -1;
    }
    doc_t seed = { s_seed_text, seed_len, s_seed_st, 0 };
    doc_t user = { s_user_text, user_len, s_user_st, 0 };
    char err[200];
    toml_table_t *seed_root = parse_copy(seed.text, seed.len, err, sizeof(err));
    if (seed_root == NULL) {
        logf_("%s: left alone: the distribution's copy does not parse (%s)", name, err);
        return 0;
    }
    toml_table_t *user_root = parse_copy(user.text, user.len, err, sizeof(err));
    if (user_root == NULL) {
        toml_free(seed_root);
        logf_("%s: left alone: it does not parse (%s)", name, err);
        return 0;
    }
    if (scan(&seed) != 0 || scan(&user) != 0) {
        toml_free(seed_root);
        toml_free(user_root);
        logf_("%s: left alone: a line this updater cannot follow", name);
        return 0;
    }
    n_ins = 0;
    int added = kind == MERGE_KEYS ? plan_keys(&seed, seed_root, &user, user_root)
                                   : plan_services(&seed, &user, user_root);
    toml_free(seed_root);
    toml_free(user_root);
    if (added < 0) {
        logf_("%s: left alone: too much to add", name);
        return 0;
    }
    if (added == 0) return 0;
    size_t out_len = 0;
    if (!build_output(&user, &seed, &out_len)) {
        logf_("%s: left alone: the result would be too large", name);
        return 0;
    }
    toml_table_t *check = parse_copy(s_out, out_len, err, sizeof(err));
    if (check == NULL) {
        logf_("%s: left alone: the result would not parse (%s)", name, err);
        return 0;
    }
    toml_free(check);
    if (write_replace(user_path, s_out, out_len) != 0) {
        logf_("%s: cannot write: %s", name, strerror(errno));
        return -1;
    }
    for (int i = 0; i < n_ins; i++) {
        if (s_ins[i].label[0]) logf_("%s: added %s", name, s_ins[i].label);
    }
    return added;
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (in == NULL) return -1;
    FILE *out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    int rc = 0;
    size_t n;
    while ((n = fread(s_out, 1, sizeof(s_out), in)) > 0) {
        if (fwrite(s_out, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    if (rc != 0) remove(to);
    return rc;
}

static bool is_name(const char *rel, const char *name)
{
    return strcmp(rel, name) == 0;
}

static int walk(const char *seed_dir, const char *user_dir, const char *rel_dir, int depth)
{
    if (depth > ETC_DEPTH_MAX) return 0;
    DIR *d = opendir(seed_dir);
    if (d == NULL) {
        logf_("cannot read %s: %s", seed_dir, strerror(errno));
        return -1;
    }
    int total = 0;
    bool failed = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char from[ETC_PATH_MAX], to[ETC_PATH_MAX], rel[ETC_PATH_MAX];
        if (snprintf(from, sizeof(from), "%s/%s", seed_dir, e->d_name) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", user_dir, e->d_name) >= (int)sizeof(to) ||
            snprintf(rel, sizeof(rel), "%s%s", rel_dir, e->d_name) >= (int)sizeof(rel) - 1) {
            logf_("left alone: a path longer than %d bytes under %s", ETC_PATH_MAX, seed_dir);
            continue;
        }
        struct stat ss, us;
        if (stat(from, &ss) != 0) continue;
        bool user_has = stat(to, &us) == 0;
        if (S_ISDIR(ss.st_mode)) {
            if (!user_has) {
                if (mkdir(to, 0755) != 0) {
                    logf_("cannot create %s: %s", rel, strerror(errno));
                    failed = true;
                    continue;
                }
            } else if (!S_ISDIR(us.st_mode)) {
                continue;
            }
            char sub[ETC_PATH_MAX + 2];
            snprintf(sub, sizeof(sub), "%s/", rel);
            int r = walk(from, to, sub, depth + 1);
            if (r < 0) failed = true; else total += r;
            continue;
        }
        if (!S_ISREG(ss.st_mode)) continue;
        if (!user_has) {
            if (copy_file(from, to) != 0) {
                logf_("cannot copy %s: %s", rel, strerror(errno));
                failed = true;
            } else {
                logf_("%s: copied (the user had none)", rel);
                total++;
            }
            continue;
        }
        if (!S_ISREG(us.st_mode)) continue;
        int r = 0;
        if (is_name(rel, "system_conf.toml") || is_name(rel, "system_conf.factory.toml")) {
            r = merge_file(rel, from, to, MERGE_KEYS);
        } else if (is_name(rel, "services.toml")) {
            r = merge_file(rel, from, to, MERGE_SERVICES);
        }
        if (r < 0) failed = true; else total += r;
    }
    closedir(d);
    return failed ? -1 : total;
}

int native_etc_update(const char *seed_dir, const char *user_etc, native_etc_log_fn log, void *ctx)
{
    s_log = log;
    s_log_ctx = ctx;
    struct stat st;
    if (seed_dir == NULL || user_etc == NULL) return 0;
    if (stat(user_etc, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;   /* the first start copies it all */
    if (stat(seed_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    return walk(seed_dir, user_etc, "", 0);
}
