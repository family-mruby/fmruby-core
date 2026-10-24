#include <string.h>
#include "toml_walk.h"

typedef struct {
    const fmrb_toml_visitor_t *v;
    void *user;
    void (*xfree)(void *);
} walk_t;

static void walk_table(walk_t *w, const toml_table_t *tab, int depth);
static void walk_array(walk_t *w, const toml_array_t *arr, int depth);

static void walk_raw(walk_t *w, toml_raw_t raw)
{
    char *s = NULL;
    int b;
    int64_t i;
    double d;
    if (toml_rtos(raw, &s) == 0) {
        w->v->string(w->user, s, strlen(s));
        w->xfree(s);
    } else if (toml_rtob(raw, &b) == 0) {
        w->v->boolean(w->user, b);
    } else if (toml_rtoi(raw, &i) == 0) {
        w->v->integer(w->user, i);
    } else if (toml_rtod(raw, &d) == 0) {
        w->v->real(w->user, d);
    } else {
        /* A date, or a bare word the parser let through: keep the text. */
        w->v->string(w->user, raw, strlen(raw));
    }
}

static void walk_array(walk_t *w, const toml_array_t *arr, int depth)
{
    w->v->begin_array(w->user);
    if (depth < FMRB_TOML_WALK_MAX_DEPTH) {
        int n = toml_array_nelem(arr);
        for (int i = 0; i < n; i++) {
            toml_raw_t raw = toml_raw_at(arr, i);
            if (raw) {
                walk_raw(w, raw);
                continue;
            }
            const toml_array_t *sub = toml_array_at(arr, i);
            if (sub) {
                walk_array(w, sub, depth + 1);
                continue;
            }
            const toml_table_t *tab = toml_table_at(arr, i);
            if (tab) {
                walk_table(w, tab, depth + 1);
            }
        }
    }
    w->v->end_array(w->user);
}

static void walk_table(walk_t *w, const toml_table_t *tab, int depth)
{
    w->v->begin_table(w->user);
    if (depth < FMRB_TOML_WALK_MAX_DEPTH) {
        const char *key;
        for (int i = 0; (key = toml_key_in(tab, i)) != NULL; i++) {
            toml_raw_t raw = toml_raw_in(tab, key);
            if (raw) {
                w->v->key(w->user, key);
                walk_raw(w, raw);
                continue;
            }
            const toml_array_t *arr = toml_array_in(tab, key);
            if (arr) {
                w->v->key(w->user, key);
                walk_array(w, arr, depth + 1);
                continue;
            }
            const toml_table_t *sub = toml_table_in(tab, key);
            if (sub) {
                w->v->key(w->user, key);
                walk_table(w, sub, depth + 1);
            }
        }
    }
    w->v->end_table(w->user);
}

void fmrb_toml_walk(const toml_table_t *root, const fmrb_toml_visitor_t *v,
                    void *user, void (*xfree)(void *))
{
    walk_t w = { v, user, xfree };
    walk_table(&w, root, 0);
}
