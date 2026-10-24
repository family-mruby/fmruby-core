// Host stand-in for FmrbToml._parse (lib/add/picoruby-fmrb-app/ports/esp32/
// toml.c): TOML text on stdin, a Ruby literal of the same Hash on stdout --
// or "ERR:" and tomlc99's reason. The walk is the firmware's own
// (toml_walk.c), so the host tests see what the board's Ruby sees; only the
// last step (building mruby objects) differs.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "toml.h"
#include "toml_walk.h"

typedef struct {
    int depth;
    int first[FMRB_TOML_WALK_MAX_DEPTH + 2];
    const char *key;
    int in_table[FMRB_TOML_WALK_MAX_DEPTH + 2];
} out_t;

static void put_str(const char *s, size_t n)
{
    putchar('"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == ' ' || c == '_' || c == '.' || c == '/' || c == '-' || c == ':') {
            putchar(c);
        } else {
            printf("\\x%02X", c);
        }
    }
    putchar('"');
}

static void before_value(out_t *o)
{
    if (o->depth == 0) {
        return;
    }
    if (!o->first[o->depth]) {
        putchar(',');
    }
    o->first[o->depth] = 0;
    if (o->in_table[o->depth]) {
        put_str(o->key, strlen(o->key));
        printf("=>");
    }
}

static void open_c(out_t *o, int table)
{
    before_value(o);
    putchar(table ? '{' : '[');
    o->depth++;
    o->first[o->depth] = 1;
    o->in_table[o->depth] = table;
}

static void b_table(void *u) { open_c(u, 1); }
static void b_array(void *u) { open_c(u, 0); }
static void e_table(void *u) { ((out_t *)u)->depth--; putchar('}'); }
static void e_array(void *u) { ((out_t *)u)->depth--; putchar(']'); }
static void v_key(void *u, const char *k) { ((out_t *)u)->key = k; }
static void v_str(void *u, const char *s, size_t n) { before_value(u); put_str(s, n); }
static void v_int(void *u, int64_t i) { before_value(u); printf("%lld", (long long)i); }
static void v_bool(void *u, int b) { before_value(u); printf(b ? "true" : "false"); }
static void v_real(void *u, double d)
{
    before_value(u);
    if (isnan(d)) printf("Float::NAN");
    else if (isinf(d)) printf(d > 0 ? "Float::INFINITY" : "-Float::INFINITY");
    else printf("%.17g.to_f", d);
}

static const fmrb_toml_visitor_t visitor = {
    b_table, e_table, b_array, e_array, v_key, v_str, v_int, v_real, v_bool,
};

int main(void)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += n;
        if (cap - len < 2) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    buf[len] = '\0';
    char errbuf[128];
    toml_table_t *tab = toml_parse(buf, errbuf, sizeof(errbuf));
    if (!tab) {
        printf("ERR:%s", errbuf[0] ? errbuf : "cannot parse");
        return 0;
    }
    out_t o;
    memset(&o, 0, sizeof(o));
    fmrb_toml_walk(tab, &visitor, &o, free);
    toml_free(tab);
    free(buf);
    return 0;
}
