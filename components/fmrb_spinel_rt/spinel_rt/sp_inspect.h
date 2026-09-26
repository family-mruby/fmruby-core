#ifndef SP_INSPECT_H
#define SP_INSPECT_H
/* sp_inspect.h -- generic container #inspect, split out of spinel_rt.h.

   sp_inspect_container formats a boxed array or hash in Ruby #inspect form,
   walking it through the generic sp_json_* / sp_poly_inspect_fn hooks (sp_gc.h)
   and building the result with sp_String (sp_string.h). The per-type inspect
   helpers in spinel_rt.h are now one-line wrappers that box their receiver and
   call this, so all the inspect string-building compiles once in the archive
   rather than in every generated TU. */
#include "sp_gc.h"   /* sp_RbVal */

const char *sp_inspect_container(sp_RbVal v);

/* ---- the walk guard: an object already on the path stops the walk ----
 *
 * `a = []; a << a` is legal Ruby, and so is a Struct that holds itself. Every
 * walk that descends into a container -- #inspect, ==, eql?, <=>, #hash,
 * #flatten, #join, puts -- would follow such a cycle until the C stack ran
 * out. CRuby's rb_exec_recursive keeps, per thread, the set of objects (or
 * object PAIRS) the current walk is already inside and answers a fixed result
 * for one it meets again; this is that set, as a small stack.
 *
 * Frames hold raw pointers that are only ever COMPARED, never dereferenced:
 * the collector does not move objects, and pushing a frame allocates nothing,
 * so a walk cannot collect the very object it is remembering. Per-worker
 * (SP_TLS) like the exception stack, and carried across a green thread's
 * suspension by sp_exc_ctx_save/load, so two fibers walking at once each keep
 * their own path. */
#define SP_POLY_RECUR_INSPECT 1
#define SP_POLY_RECUR_EQ      2   /* shared by ==, eql? and <=>: all three answer
                                     "equal" for a pair they are already inside */
#define SP_POLY_RECUR_HASH    3
#define SP_POLY_RECUR_FLATTEN 4
#define SP_POLY_RECUR_JOIN    5
#define SP_POLY_RECUR_PUTS    6   /* `puts a` walks an array without inspecting it */
/* What a repeated object contributes to a #hash: a fixed ODD constant, because
   nil, false and 0 all hash to 0 and a Struct holding itself must not hash like
   one holding nil. */
#define SP_POLY_RECUR_HASH_VALUE ((sp_int)0x3f5b9d7e2c1a4d05LL)
typedef struct { const void *a, *b; int kind; } sp_poly_recur_frame;
/* The frames grow on demand rather than sitting in a fixed TLS array: a fixed
   cap would leave a cycle whose repeated object is deeper than the cap running
   off the C stack, and kilobytes of TLS shift the layout (and the cost) of
   every hot per-worker variable -- the break stack's own comment records
   measuring 8% of optcarrot on exactly that. Only the pointer and the two
   counts live in TLS; the frames are malloc'd on the first push, doubled when
   full, and never shrunk, per worker. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern SP_TLS sp_poly_recur_frame *sp_poly_recur_stack;
extern SP_TLS int sp_poly_recur_top;
extern SP_TLS int sp_poly_recur_cap;
#endif
void sp_poly_recur_grow(int want);   /* first allocation and every doubling */
/* A lookup scans the path: a real program's path is under ten frames, and a
   scan of that beats any table. But a scan at every level of a deep walk is
   quadratic -- a 2000-deep nest would compare 2000 frames at each of its 2000
   levels, where CRuby's rb_exec_recursive, a hash, stays linear. So past
   SP_POLY_RECUR_SCAN frames the path is also indexed (sp_inspect.c) and a
   lookup is one probe. Frames [0, sp_poly_recur_ixtop) are in the index; a pop
   below that simply lowers it, and the next deep push indexes what is missing. */
#define SP_POLY_RECUR_SCAN 32
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
extern SP_TLS int sp_poly_recur_ixtop;
#endif
int  sp_poly_recur_seen_deep(int kind, const void *a, const void *b);
void sp_poly_recur_index(void);      /* bring the index up to sp_poly_recur_top */
/* Is (kind, a, b) already on the path? `b` is NULL for the single-object kinds
   (inspect, hash, flatten, join, puts) and the other side of the pair for
   equality. An empty path (the common case) reads one int and returns. */
static inline int sp_poly_recur_seen(int kind, const void *a, const void *b) {
  int top = sp_poly_recur_top;
  /* the scan covers SCAN frames; the push of the frame at index SCAN is the
     one that indexes (below), and from then on top is SCAN + 1 or more */
  if (SP_UNLIKELY(top > SP_POLY_RECUR_SCAN)) return sp_poly_recur_seen_deep(kind, a, b);
  for (int i = top - 1; i >= 0; i--) {
    if (sp_poly_recur_stack[i].a == a && sp_poly_recur_stack[i].b == b &&
        sp_poly_recur_stack[i].kind == kind) return 1;
  }
  return 0;
}
/* Push a frame and answer the mark to hand back to sp_poly_recur_pop. */
static inline int sp_poly_recur_push(int kind, const void *a, const void *b) {
  int mark = sp_poly_recur_top;
  if (SP_UNLIKELY(mark == sp_poly_recur_cap)) sp_poly_recur_grow(mark + 1);
  sp_poly_recur_stack[mark].a = a;
  sp_poly_recur_stack[mark].b = b;
  sp_poly_recur_stack[mark].kind = kind;
  sp_poly_recur_top = mark + 1;
  if (SP_UNLIKELY(mark >= SP_POLY_RECUR_SCAN)) sp_poly_recur_index();
  return mark;
}
static inline void sp_poly_recur_pop(int mark) {
  sp_poly_recur_top = mark;
  if (SP_UNLIKELY(sp_poly_recur_ixtop > mark)) sp_poly_recur_ixtop = mark;
}
/* The current depth as a mark, for a walk that took a branch where it did not
   push: the pop that pairs with it then leaves the path exactly as it was. */
static inline int sp_poly_recur_save(void) { return sp_poly_recur_top; }
/* Drop every frame of `kind` from the top of the path. A walk about to raise
   with no answer to give -- an unlimited flatten or a join that met itself --
   takes its own frames with it, so nothing is left on the path for a later
   walk to trip over even where no handler restores the depth (an at_exit
   block still walks). A handler further out that does restore re-exposes the
   frames of walks that are still running, which is what it should do. */
static inline void sp_poly_recur_drop_kind(int kind) {
  int top = sp_poly_recur_top;
  while (top > 0 && sp_poly_recur_stack[top - 1].kind == kind) top--;
  sp_poly_recur_pop(top);
}
/* The Set package (packages/set/set.rb) walks its elements in Ruby, so its
   #inspect, #== and #flatten bind these through native_func and keep their
   frames on this same path: per fiber, restored by every longjmp, and with no
   `ensure` in the Ruby. `enter` answers the mark for sp_poly_recur_leave, or
   -1 when the walk is already inside that Set (or pair), which is the caller's
   cue to stop. The module is reachable by name from Ruby, so `leave` ignores a
   mark this path never handed out to a Set walk. The kinds are Set's own so a
   frame the runtime pushed for the same object or pair -- sp_poly_eql on two
   Sets reaches Set#== through the user hook -- is not mistaken for the Set
   walk's. */
#define SP_POLY_RECUR_SET_INSPECT 7
#define SP_POLY_RECUR_SET_EQ      8
#define SP_POLY_RECUR_SET_FLATTEN 9
sp_int sp_poly_recur_enter_inspect(sp_RbVal set);
sp_int sp_poly_recur_enter_eq(sp_RbVal set, sp_RbVal other);
sp_int sp_poly_recur_enter_flatten(sp_RbVal set);
void   sp_poly_recur_leave(sp_int mark);
#endif /* SP_INSPECT_H */
