// FmrbToml._parse: the firmware's TOML reader (components/fmrb_toml, tomlc99)
// for Ruby. The Ruby half (mrblib/fmrb-toml.rb) reads files, logs and writes;
// this half only turns text into a Hash.
//
// Memory: tomlc99 allocates through one process-wide pair of functions
// (fmrb_toml_init). While a VM is inside _parse, that pair takes from the
// VM's own pool (mrb_malloc_simple), so reading an app's settings costs the
// app's heap, not the shared SYSTEM pool; every other caller (the spawner,
// the kernel, the HID table) keeps the SYSTEM pool. The switch is per app
// slot, so two VMs parsing at once each use their own pool.

#include <string.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include "fmrb_app.h"
#include "fmrb_attr.h"
#include "fmrb_toml.h"
#include "toml_walk.h"
#include "app_local.h"

// Which app slots are inside _parse. PSRAM on the boards (nothing in
// internal RAM); read by the allocator hooks below from the same task.
FMRB_EXT_RAM_BSS_ATTR static uint8_t s_in_parse[FMRB_MAX_APPS];

static mrb_state *parsing_vm(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || ctx->vm_type != FMRB_VM_TYPE_MRUBY || !ctx->mrb) {
        return NULL;
    }
    if (ctx->app_id < 0 || ctx->app_id >= FMRB_MAX_APPS || !s_in_parse[ctx->app_id]) {
        return NULL;
    }
    return ctx->mrb;
}

static bool vm_alloc(size_t size, void **out)
{
    mrb_state *mrb = parsing_vm();
    if (!mrb) {
        return false;
    }
    *out = mrb_malloc_simple(mrb, size);
    return true;
}

static bool vm_free(void *ptr)
{
    mrb_state *mrb = parsing_vm();
    if (!mrb) {
        return false;
    }
    mrb_free(mrb, ptr);
    return true;
}

// ---- building the Hash -----------------------------------------------------

typedef struct {
    mrb_state *mrb;
    mrb_value root;     // the Hash returned
    mrb_value stack;    // Array of the containers being filled, innermost last
    const char *key;    // key of the next value, when the innermost is a Hash
    int arena;          // arena index with root and stack on it
} build_t;

static void put(build_t *b, mrb_value v)
{
    mrb_state *mrb = b->mrb;
    mrb_int depth = RARRAY_LEN(b->stack);
    if (depth == 0) {
        b->root = v;
    } else {
        mrb_value top = mrb_ary_ref(mrb, b->stack, depth - 1);
        if (mrb_hash_p(top)) {
            mrb_hash_set(mrb, top, mrb_str_new_cstr(mrb, b->key), v);
        } else {
            mrb_ary_push(mrb, top, v);
        }
    }
    // Everything made so far hangs off root or stack: the arena can let go.
    mrb_gc_arena_restore(mrb, b->arena);
}

static void open_container(build_t *b, mrb_value c)
{
    mrb_bool first = RARRAY_LEN(b->stack) == 0;
    put(b, c);
    if (first) {
        // root is reachable from nothing yet: keep it on the arena.
        mrb_gc_protect(b->mrb, c);
        b->arena = mrb_gc_arena_save(b->mrb);
    }
    mrb_ary_push(b->mrb, b->stack, c);
}

static void v_begin_table(void *u) { build_t *b = u; open_container(b, mrb_hash_new(b->mrb)); }
static void v_begin_array(void *u) { build_t *b = u; open_container(b, mrb_ary_new(b->mrb)); }
static void v_end(void *u) { build_t *b = u; mrb_ary_pop(b->mrb, b->stack); }
static void v_key(void *u, const char *key) { ((build_t *)u)->key = key; }
static void v_string(void *u, const char *s, size_t n) { build_t *b = u; put(b, mrb_str_new(b->mrb, s, (mrb_int)n)); }
static void v_integer(void *u, int64_t i) { build_t *b = u; put(b, mrb_int_value(b->mrb, (mrb_int)i)); }
static void v_real(void *u, double d) { build_t *b = u; put(b, mrb_float_value(b->mrb, (mrb_float)d)); }
static void v_boolean(void *u, int x) { build_t *b = u; put(b, mrb_bool_value(x != 0)); }

static const fmrb_toml_visitor_t s_visitor = {
    v_begin_table, v_end, v_begin_array, v_end, v_key,
    v_string, v_integer, v_real, v_boolean,
};

typedef struct {
    build_t *b;
    toml_table_t *tab;
} walk_arg_t;

static mrb_value walk_body(mrb_state *mrb, void *data)
{
    walk_arg_t *a = (walk_arg_t *)data;
    (void)mrb;
    fmrb_toml_walk(a->tab, &s_visitor, a->b, fmrb_toml_xfree);
    return a->b->root;
}

// FmrbToml._parse(text) -> Hash, or a String saying why the text is not TOML.
static mrb_value mrb_fmrb_toml_s_parse(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_value text;
    mrb_get_args(mrb, "S", &text);

    fmrb_app_task_context_t *ctx = fmrb_current();
    int slot = (ctx && ctx->app_id >= 0 && ctx->app_id < FMRB_MAX_APPS) ? ctx->app_id : -1;

    // Raises on a NUL inside the text, so before the slot is marked.
    char *src = (char *)mrb_string_cstr(mrb, text);
    char errbuf[128];
    errbuf[0] = '\0';
    if (slot >= 0) s_in_parse[slot] = 1;
    toml_table_t *tab = toml_parse(src, errbuf, sizeof(errbuf));
    if (!tab) {
        if (slot >= 0) s_in_parse[slot] = 0;
        return mrb_str_new_cstr(mrb, errbuf[0] ? errbuf : "cannot parse");
    }

    int ai = mrb_gc_arena_save(mrb);
    build_t b;
    b.mrb = mrb;
    b.root = mrb_nil_value();
    b.stack = mrb_ary_new(mrb);
    b.key = NULL;
    b.arena = ai;
    mrb_gc_protect(mrb, b.stack);
    b.arena = mrb_gc_arena_save(mrb);

    walk_arg_t arg = { &b, tab };
    mrb_bool failed = FALSE;
    mrb_value result = mrb_protect_error(mrb, walk_body, &arg, &failed);
    toml_free(tab);
    if (slot >= 0) s_in_parse[slot] = 0;
    mrb_gc_arena_restore(mrb, ai);
    if (failed) {
        mrb_exc_raise(mrb, result);
    }
    mrb_gc_protect(mrb, result);
    return result;
}

void mrb_fmrb_toml_init(mrb_state *mrb)
{
    fmrb_toml_set_task_allocator(vm_alloc, vm_free);
    struct RClass *mod = mrb_define_module(mrb, "FmrbToml");
    mrb_define_module_function(mrb, mod, "_parse", mrb_fmrb_toml_s_parse, MRB_ARGS_REQ(1));
}
