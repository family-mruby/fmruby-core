/* sp_inspect.c -- generic container #inspect (see sp_inspect.h).

   Walks a boxed array / hash via the hooks the generated TU installs
   (sp_json_kind/len/aref/hpair classify and iterate; sp_poly_inspect_fn recurses
   into elements/keys/values; sp_sym_name_fn renders a symbol key shorthand). No
   typed-container accessors are touched, so this stays clear of the hot hash
   probe path. The result is built with the shared sp_String builder. */
#include "sp_inspect.h"
#include "sp_string.h"   /* sp_String, sp_alloc.h, SP_GC_ROOT via sp_gc.h */
#include "sp_str.h"      /* sp_sym_inspect_key for the symbol hash-key short form */

const char *sp_inspect_container(sp_RbVal v) {
  /* The container is live across the sp_String allocations below; root it so a
     GC mid-inspect can't free it -- otherwise inspecting a freshly built,
     otherwise-unrooted collection (e.g. `p hash.flat_map { ... }`) is a
     use-after-free under GC pressure. */
  SP_GC_ROOT_RBVAL(v);
  int kind = sp_json_kind_fn ? sp_json_kind_fn(v) : 0;
  mrb_int n = sp_json_len_fn ? sp_json_len_fn(v) : 0;
  if (kind == 1) {  /* array: [e0, e1, ...] */
    sp_String *s = sp_String_new("[");
    SP_GC_ROOT(s);
    for (mrb_int i = 0; i < n; i++) {
      if (i) sp_String_append(s, ", ");
      sp_String_append(s, sp_poly_inspect_fn(sp_json_aref_fn(v, i)));
    }
    sp_String_append(s, "]");
    return s->data;
  }
  /* hash: {k => v, ...}, with the `sym: v` shorthand for a Symbol key. */
  sp_String *s = sp_String_new("{");
  SP_GC_ROOT(s);
  for (mrb_int i = 0; i < n; i++) {
    if (i) sp_String_append(s, ", ");
    sp_RbVal k, val;
    sp_json_hpair_fn(v, i, &k, &val);
    if (k.tag == SP_TAG_SYM) {
      sp_String_append(s, sp_sym_inspect_key(sp_sym_name_fn ? sp_sym_name_fn((sp_sym)k.v.i) : ""));
      sp_String_append(s, ": ");
    }
    else {
      sp_String_append(s, sp_poly_inspect_fn(k));
      sp_String_append(s, " => ");
    }
    sp_String_append(s, sp_poly_inspect_fn(val));
  }
  sp_String_append(s, "}");
  return s->data;
}
