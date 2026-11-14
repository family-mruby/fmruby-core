/*
 * Proof of concept: read RBS record type aliases with the standalone C parser
 * that ships in the rbs gem (no Ruby), and print what Asterism's define_type
 * takes: a name, [[field, type], ...] and a description.
 *
 *   rbs2ast <prefix> <file.rbs>
 *
 * Mapping (RBS -> Asterism field type):
 *   Integer -> int, Float -> float, String -> str, bool -> bool,
 *   untyped -> any, Array[T] -> T[], an alias named bin -> bin,
 *   another alias Ns::name -> "<prefix>/ns.name", T? -> T (noted as optional)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rbs/parser.h"
#include "rbs/string.h"
#include "rbs/util/rbs_encoding.h"

/* ---- allocation accounting (linked with -Wl,--wrap=malloc,...) ---------- */
#include <malloc.h>
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static size_t g_live, g_peak, g_calls;
static void account(void *p, int sign) {
    if (!p) return;
    size_t n = malloc_usable_size(p);
    if (sign > 0) { g_live += n; g_calls++; if (g_live > g_peak) g_peak = g_live; }
    else g_live -= n;
}
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); account(p, 1); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); account(p, 1); return p; }
void *__wrap_realloc(void *o, size_t n) { account(o, -1); void *p = __real_realloc(o, n); account(p, 1); return p; }
void __wrap_free(void *p) { account(p, -1); __real_free(p); }

/* ---- output helpers ------------------------------------------------------ */
static rbs_parser_t *g_parser;
static const char *g_prefix;

static void put_json_str(const char *s, size_t n) {
    putchar('"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c == '\n') fputs("\\n", stdout);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static void sym(rbs_ast_symbol_t *s, const char **start, size_t *len) {
    rbs_constant_t *c = rbs_constant_pool_id_to_constant(&g_parser->constant_pool, s->constant_id);
    *start = (const char *) c->start;
    *len = c->length;
}

/* "Ns::Sub::name" -> buf "ns.sub.name" (lower-cased namespace). */
static void dotted_name(rbs_type_name_t *tn, char *buf, size_t cap) {
    size_t at = 0;
    for (rbs_node_list_node_t *n = tn->rbs_namespace->path->head; n; n = n->next) {
        const char *s; size_t l;
        sym((rbs_ast_symbol_t *) n->node, &s, &l);
        for (size_t i = 0; i < l && at + 2 < cap; i++) buf[at++] = (char) tolower((unsigned char) s[i]);
        if (at + 2 < cap) buf[at++] = '.';
    }
    const char *s; size_t l;
    sym(tn->name, &s, &l);
    for (size_t i = 0; i < l && at + 1 < cap; i++) buf[at++] = s[i];
    buf[at] = 0;
}

/* Writes the Asterism field type of an RBS type into buf. Returns false when
 * the type has no mapping (the caller reports it). */
static bool field_type(rbs_node_t *t, char *buf, size_t cap, bool *optional) {
    switch (t->type) {
    case RBS_TYPES_OPTIONAL:
        *optional = true;
        return field_type(((rbs_types_optional_t *) t)->type, buf, cap, optional);
    case RBS_TYPES_BASES_BOOL: snprintf(buf, cap, "bool"); return true;
    case RBS_TYPES_BASES_ANY:  snprintf(buf, cap, "any");  return true;
    case RBS_TYPES_CLASS_INSTANCE: {
        rbs_types_class_instance_t *ci = (rbs_types_class_instance_t *) t;
        const char *s; size_t l;
        sym(ci->name->name, &s, &l);
        if (l == 7 && !memcmp(s, "Integer", 7)) { snprintf(buf, cap, "int"); return true; }
        if (l == 5 && !memcmp(s, "Float", 5))   { snprintf(buf, cap, "float"); return true; }
        if (l == 6 && !memcmp(s, "String", 6))  { snprintf(buf, cap, "str"); return true; }
        if (l == 5 && !memcmp(s, "Array", 5) && ci->args->length == 1) {
            char inner[160];
            if (!field_type(ci->args->head->node, inner, sizeof inner, optional)) return false;
            snprintf(buf, cap, "%s[]", inner);
            return true;
        }
        return false;
    }
    case RBS_TYPES_ALIAS: {
        rbs_types_alias_t *al = (rbs_types_alias_t *) t;
        char name[128];
        dotted_name(al->name, name, sizeof name);
        if (!strcmp(name, "bin")) { snprintf(buf, cap, "bin"); return true; }
        snprintf(buf, cap, "%s/%s", g_prefix, name);
        return true;
    }
    default:
        return false;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: rbs2ast <prefix> <file.rbs>\n"); return 2; }
    g_prefix = argv[1];
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 2; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t) size + 1);
    if (fread(src, 1, (size_t) size, f) != (size_t) size) { fprintf(stderr, "read failed\n"); return 2; }
    fclose(f);

    size_t before = g_live;
    rbs_string_t str = rbs_string_new(src, src + size);
    g_parser = rbs_parser_new(str, RBS_ENCODING_UTF_8_ENTRY, 0, (int) size);
    rbs_signature_t *sig = NULL;
    if (!rbs_parse_signature(g_parser, &sig) || g_parser->error) {
        fprintf(stderr, "parse error: %s\n", g_parser->error ? g_parser->error->message : "?");
        return 1;
    }
    size_t parse_peak = g_peak - before;

    int skipped = 0;
    printf("{\"types\":[");
    bool first = true;
    for (rbs_node_list_node_t *n = sig->declarations->head; n; n = n->next) {
        if (n->node->type != RBS_AST_DECLARATIONS_TYPE_ALIAS) continue;
        rbs_ast_declarations_type_alias_t *al = (rbs_ast_declarations_type_alias_t *) n->node;
        if (al->type->type != RBS_TYPES_RECORD) continue;
        char name[128];
        dotted_name(al->name, name, sizeof name);
        printf("%s\n {\"name\":\"%s/%s\",\"f\":[", first ? "" : ",", g_prefix, name);
        first = false;
        bool ff = true;
        rbs_types_record_t *rec = (rbs_types_record_t *) al->type;
        for (rbs_hash_node_t *h = rec->all_fields->head; h; h = h->next) {
            const char *ks; size_t kl;
            sym((rbs_ast_symbol_t *) h->key, &ks, &kl);
            rbs_types_record_field_type_t *ft = (rbs_types_record_field_type_t *) h->value;
            char ty[200]; bool opt = !ft->required;
            if (!field_type(ft->type, ty, sizeof ty, &opt)) {
                fprintf(stderr, "%s: field %.*s: no Asterism type for this RBS type\n", name, (int) kl, ks);
                skipped++;
                continue;
            }
            printf("%s[", ff ? "" : ","); ff = false;
            put_json_str(ks, kl); putchar(',');
            put_json_str(ty, strlen(ty)); putchar(']');
        }
        printf("],\"d\":");
        if (al->comment) put_json_str(al->comment->string.start, (size_t) (al->comment->string.end - al->comment->string.start));
        else printf("null");
        putchar('}');
    }
    printf("\n],\"stats\":{\"source_bytes\":%ld,\"parse_peak_bytes\":%zu,\"malloc_calls\":%zu,\"skipped_fields\":%d}}\n",
           size, parse_peak, g_calls, skipped);
    rbs_parser_free(g_parser);
    free(src);
    return 0;
}
