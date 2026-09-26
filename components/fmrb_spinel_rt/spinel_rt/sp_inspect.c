/* sp_inspect.c -- generic container #inspect (see sp_inspect.h).

   Walks a boxed array / hash via the hooks the generated TU installs
   (sp_json_kind/len/aref/hpair classify and iterate; sp_poly_inspect_fn recurses
   into elements/keys/values; sp_sym_name_fn renders a symbol key shorthand). No
   typed-container accessors are touched, so this stays clear of the hot hash
   probe path. The result is built with the shared sp_String builder. */
#include "sp_inspect.h"
#include "sp_string.h"   /* sp_String, sp_alloc.h, SP_GC_ROOT via sp_gc.h */
#include "sp_str.h"      /* sp_sym_inspect_key for the symbol hash-key short form */
#include <stdlib.h>   /* realloc for the growing path below */

/* The walk guard's per-worker path (see sp_inspect.h). It lives here because
   this archive unit is the one every side of the runtime already links for
   #inspect, and the guard has to be one set of frames shared by the generated
   translation unit, the archive and the carried packages -- a per-TU copy would
   let a walk that crosses the boundary forget where it had been. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
SP_TLS sp_poly_recur_frame *sp_poly_recur_stack = NULL;
SP_TLS int sp_poly_recur_top = 0;
SP_TLS int sp_poly_recur_cap = 0;
#endif
/* Make room for at least `want` frames, doubling from 64. Off the hot path:
   a walk deep enough to reach the end of the buffer has already paid far more
   in its own recursion than this call costs. */
#define SP_POLY_RECUR_FIRST_CAP 64   /* frames; 1.5 KB, more than any real walk */
SP_COLD void sp_poly_recur_grow(int want) {
  int cap = sp_poly_recur_cap ? sp_poly_recur_cap : SP_POLY_RECUR_FIRST_CAP;
  while (cap < want) cap *= 2;
  sp_poly_recur_frame *f = (sp_poly_recur_frame *)
      realloc(sp_poly_recur_stack, sizeof(sp_poly_recur_frame) * (size_t)cap);
  if (!f) sp_oom_die();
  sp_poly_recur_stack = f;
  sp_poly_recur_cap = cap;
}

/* ---- the deep-walk index (SP_POLY_RECUR_SCAN, sp_inspect.h) ----
   Open addressing over the frames themselves: a slot names a frame by index
   and repeats its key, and is live only while that frame is still on the path
   with that key. A pop lowers sp_poly_recur_top and sp_poly_recur_ixtop and
   leaves the slots to go stale on their own; a lookup checks the frame, an
   insert may take a stale slot, and once the slots in use (live or stale) pass
   half the table it is rebuilt from the live frames at a quarter load. Nothing
   here runs until a walk is deeper than the scan budget. */
typedef struct { const void *a, *b; int kind, frame; } sp_poly_recur_slot;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static SP_TLS sp_poly_recur_slot *sp_poly_recur_ix = NULL;
static SP_TLS int sp_poly_recur_ixcap = 0;    /* a power of two, or 0 before first use */
static SP_TLS int sp_poly_recur_ixused = 0;   /* slots holding a frame, live or stale */
SP_TLS int sp_poly_recur_ixtop = 0;
#endif

/* Multipliers for the slot hash: 2^64 / phi and two more odd constants of the
   same shape (xxHash's), so a pointer's bits spread across the word before the
   fold and the mask. Any odd multipliers would do; these are the usual ones. */
#define SP_POLY_RECUR_HASH_M1 0x9E3779B97F4A7C15ull
#define SP_POLY_RECUR_HASH_M2 0xC2B2AE3D27D4EB4Full
#define SP_POLY_RECUR_HASH_M3 0x165667B19E3779F9ull
#define SP_POLY_RECUR_IX_MIN  256   /* slots; an eighth full at the scan budget */
/* Callers guarantee a built table: sp_poly_recur_index rebuilds before any probe. */
static inline int sp_poly_recur_slot_of(int kind, const void *a, const void *b) {
  uint64_t h = ((uint64_t)(uintptr_t)a >> 4) * SP_POLY_RECUR_HASH_M1
             ^ ((uint64_t)(uintptr_t)b >> 4) * SP_POLY_RECUR_HASH_M2
             ^ (uint64_t)kind * SP_POLY_RECUR_HASH_M3;
  h ^= h >> 32;
  return (int)(h & (uint64_t)(sp_poly_recur_ixcap - 1));
}
static inline int sp_poly_recur_slot_live(const sp_poly_recur_slot *s) {
  if (s->frame >= sp_poly_recur_top) return 0;
  const sp_poly_recur_frame *f = &sp_poly_recur_stack[s->frame];
  return f->a == s->a && f->b == s->b && f->kind == s->kind;
}
static void sp_poly_recur_ix_insert(int frame) {
  const sp_poly_recur_frame *f = &sp_poly_recur_stack[frame];
  int mask = sp_poly_recur_ixcap - 1;
  int i = sp_poly_recur_slot_of(f->kind, f->a, f->b);
  for (;;) {
    sp_poly_recur_slot *s = &sp_poly_recur_ix[i];
    if (s->frame < 0) { sp_poly_recur_ixused++; break; }
    if (!sp_poly_recur_slot_live(s)) break;   /* a stale slot is free */
    i = (i + 1) & mask;
  }
  sp_poly_recur_ix[i].a = f->a;
  sp_poly_recur_ix[i].b = f->b;
  sp_poly_recur_ix[i].kind = f->kind;
  sp_poly_recur_ix[i].frame = frame;
}
SP_COLD static void sp_poly_recur_ix_rebuild(int frames) {
  int cap = SP_POLY_RECUR_IX_MIN;
  while (cap < frames * 4) cap *= 2;
  if (cap != sp_poly_recur_ixcap) {
    free(sp_poly_recur_ix);
    sp_poly_recur_ix = (sp_poly_recur_slot *)malloc(sizeof(sp_poly_recur_slot) * (size_t)cap);
    if (!sp_poly_recur_ix) sp_oom_die();
    sp_poly_recur_ixcap = cap;
  }
  for (int i = 0; i < cap; i++) sp_poly_recur_ix[i].frame = -1;
  sp_poly_recur_ixused = 0;
  sp_poly_recur_ixtop = 0;
}
void sp_poly_recur_index(void) {
  int top = sp_poly_recur_top;
  if (sp_poly_recur_ixused + (top - sp_poly_recur_ixtop) > sp_poly_recur_ixcap / 2)
    sp_poly_recur_ix_rebuild(top);
  for (int i = sp_poly_recur_ixtop; i < top; i++) sp_poly_recur_ix_insert(i);
  sp_poly_recur_ixtop = top;
}
int sp_poly_recur_seen_deep(int kind, const void *a, const void *b) {
  /* a fiber switch (sp_exc_ctx_load) puts other frames under the index, and
     the first deep lookup of a worker finds no table at all */
  if (sp_poly_recur_ixcap == 0 || sp_poly_recur_ixtop < sp_poly_recur_top) sp_poly_recur_index();
  int mask = sp_poly_recur_ixcap - 1;
  int i = sp_poly_recur_slot_of(kind, a, b);
  for (;;) {
    const sp_poly_recur_slot *s = &sp_poly_recur_ix[i];
    if (s->frame < 0) return 0;
    if (s->a == a && s->b == b && s->kind == kind && sp_poly_recur_slot_live(s)) return 1;
    i = (i + 1) & mask;
  }
}

/* ---- the Set package's walks (prototypes and kinds in sp_inspect.h) ---- */
static sp_int sp_poly_recur_enter(int kind, const void *a, const void *b) {
  if (sp_poly_recur_seen(kind, a, b)) return -1;
  return sp_poly_recur_push(kind, a, b);
}
sp_int sp_poly_recur_enter_inspect(sp_RbVal set) {
  return sp_poly_recur_enter(SP_POLY_RECUR_SET_INSPECT, set.v.p, NULL);
}
sp_int sp_poly_recur_enter_eq(sp_RbVal set, sp_RbVal other) {
  return sp_poly_recur_enter(SP_POLY_RECUR_SET_EQ, set.v.p, other.v.p);
}
sp_int sp_poly_recur_enter_flatten(sp_RbVal set) {
  /* the Ruby raises on -1; as in the runtime's flatten, the walk's own frames
     go before the raise (sp_poly_recur_drop_kind) */
  if (sp_poly_recur_seen(SP_POLY_RECUR_SET_FLATTEN, set.v.p, NULL)) {
    sp_poly_recur_drop_kind(SP_POLY_RECUR_SET_FLATTEN);
    return -1;
  }
  return sp_poly_recur_push(SP_POLY_RECUR_SET_FLATTEN, set.v.p, NULL);
}
/* The mark comes back from Ruby, where anyone can spell the module's name. One
   this path never handed out to a Set walk -- outside the frames, or naming a
   frame of the runtime's own -- is ignored rather than moving the depth under
   a walk that is still running. A Set walk's own mark whose frame a handler
   has already dropped is beyond top, and ignored the same way. */
void sp_poly_recur_leave(sp_int mark) {
  if (mark < 0 || mark >= sp_poly_recur_top) return;
  int kind = sp_poly_recur_stack[mark].kind;
  if (kind != SP_POLY_RECUR_SET_INSPECT && kind != SP_POLY_RECUR_SET_EQ &&
      kind != SP_POLY_RECUR_SET_FLATTEN) return;
  sp_poly_recur_pop((int)mark);
}

const char *sp_inspect_container(sp_RbVal v) {
  /* The container is live across the sp_String allocations below; root it so a
     GC mid-inspect can't free it -- otherwise inspecting a freshly built,
     otherwise-unrooted collection (e.g. `p hash.flat_map { ... }`) is a
     use-after-free under GC pressure. */
  SP_GC_ROOT_RBVAL(v);
  int kind = sp_json_kind_fn ? sp_json_kind_fn(v) : 0;
  sp_int n = sp_json_len_fn ? sp_json_len_fn(v) : 0;
  /* A container reached from inside itself renders as the ellipsis and stops:
     CRuby prints [[...]] for `a << a` and {h: {...}} for `h[:h] = h`. The mark
     covers the loop below, so a SIBLING copy of the same object still renders
     in full -- only the one the walk is standing inside is elided. */
  if (sp_poly_recur_seen(SP_POLY_RECUR_INSPECT, v.v.p, NULL))
    return kind == 1 ? SPL("[...]") : SPL("{...}");
  int rmark = sp_poly_recur_push(SP_POLY_RECUR_INSPECT, v.v.p, NULL);
  if (kind == 1) {  /* array: [e0, e1, ...] */
    sp_String *s = sp_String_new("[");
    SP_GC_ROOT(s);
    for (sp_int i = 0; i < n; i++) {
      if (i) sp_String_append(s, ", ");
      sp_String_append(s, sp_poly_inspect_fn(sp_json_aref_fn(v, i)));
    }
    sp_String_append(s, "]");
    sp_poly_recur_pop(rmark);
    return sp_str_dup(s->data);
  }
  /* hash: {k => v, ...}, with the `sym: v` shorthand for a Symbol key. */
  sp_String *s = sp_String_new("{");
  SP_GC_ROOT(s);
  for (sp_int i = 0; i < n; i++) {
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
  sp_poly_recur_pop(rmark);
  return sp_str_dup(s->data);
}
