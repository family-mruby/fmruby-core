#ifndef SP_ALLOC_H
#define SP_ALLOC_H
/* Shared GC-string allocation.

   The string-heap state below is `extern` -- defined once in sp_alloc.c and
   linked from libspinel_rt.a -- so the generated program's single translation
   unit and every standalone lib C file share ONE string heap. A cold runtime C file
   (marshal, pack, json, ...) can therefore allocate GC-tracked strings directly
   without #including sp_runtime.h and inheriting its per-TU `static` heap state
   (which would otherwise create a second, never-swept string heap and leak).

   The hot allocators stay `static inline` here so each including TU still
   inlines them over the shared extern state -- the same shape sp_gc_alloc
   already uses for the extern sp_gc_heap / sp_gc_bytes object heap. */
#include "sp_types.h"   /* sp_str_hdr, mrb_int, mrb_float */
#include "sp_gc.h"      /* sp_gc_collect, sp_oom_die, sp_gc_str_sweep_hook */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>      /* snprintf for the int/float formatters below */
#include <math.h>       /* HUGE_VAL / signbit for sp_float_to_s */

/* Global heap lock (Phase 1, design 6.1). Under SP_THREADS one mutex serializes
   the object- and string-heap mutations -- the trigger+collect, the calloc/
   malloc, and the list link -- so N>1 workers cannot corrupt the shared heap
   lists or byte counters. It wraps the hot allocators here (sp_str_alloc, in
   this header so the generated TU's inlined copy locks too) and in sp_alloc.c
   (sp_gc_alloc). The internal collect/sweep run under the held lock and never
   re-acquire it (no allocator re-enters from a scan/finalize callback). A single
   non-recursive mutex covers both heaps since a collection from either path
   sweeps the other under the same held lock. No-op (and no pthread dependency)
   in the single-threaded build, so that path is byte-identical.
   NOTE: at N>1 this alone is not sufficient -- a worker doing pure computation
   never reaches the lock, so stop-the-world via safepoints (added with the
   workers) is still required before the collector may run. */
#ifdef SP_THREADS
#include <pthread.h>
extern pthread_mutex_t sp_heap_lock;
#define SP_HEAP_LOCK()   pthread_mutex_lock(&sp_heap_lock)
#define SP_HEAP_UNLOCK() pthread_mutex_unlock(&sp_heap_lock)
#else
#define SP_HEAP_LOCK()   ((void)0)
#define SP_HEAP_UNLOCK() ((void)0)
#endif

/* ---- shared string-heap state (defined in sp_alloc.c) ---- */
extern sp_str_hdr *sp_str_heap;          /* live-string singly-linked list head */
extern size_t sp_str_heap_bytes;         /* live string-heap bytes */
extern size_t sp_str_threshold;          /* string-GC trigger (own heuristic) */
extern size_t sp_str_threshold_init;     /* recompute floor */
extern int    sp_str_stress_checked;     /* one-shot SPINEL_GC_STRESS check */

extern const char sp_str_empty_data[];
#define sp_str_empty (sp_str_empty_data + 1)

/* UTF-8 char-length cache. Shared (extern) so sp_str_sweep flushes the same
   table the length helpers in sp_runtime.h populate: a per-TU split would leave
   the generated TU's cache pointing at strings the archive-side sweep already
   freed. */
#define SP_STR_LCACHE_BITS 5
#define SP_STR_LCACHE_SIZE (1u << SP_STR_LCACHE_BITS)
struct sp_str_lcache_entry {
  const char *s;
  size_t byte_len;
  mrb_int char_len;
};
/* Per-worker (SP_TLS) in the threaded build: this string-length cache is keyed
   by string pointer and written without the heap lock (sp_str_byte_len is on the
   hot path), so a shared copy would be a data race across workers -- a torn read
   yields a wrong length and sp_str_concat's memcpy overruns. Each worker keeps
   its own; it is cleared at every safepoint park (before a sweep can recycle a
   cached string's address) and by the string sweep on the collector. */
extern SP_TLS struct sp_str_lcache_entry sp_str_lcache[SP_STR_LCACHE_SIZE];

/* Cold; single definitions in sp_alloc.c. sp_str_sweep is wired to the GC via a
   constructor so it runs from sp_gc_collect regardless of which TU triggered
   the collection. */
void sp_str_sweep(void);
void sp_str_lcache_clear(void);
/* Collect + retune (see sp_alloc.c). The single-threaded allocators call the
   per-heap variants directly; sp_stw_collect (threaded) runs _all under STW. */
void sp_str_collect_retune(void);
void sp_gc_collect_retune_all(void);
int  sp_gc_collection_wanted(void);
void sp_stw_collect(void);   /* lib/sp_sched.c: stop-the-world collect (threaded) */

static inline char *sp_str_alloc(size_t len) {
  size_t total = sizeof(sp_str_hdr) + 1 + len + 1;
  SP_HEAP_LOCK();
  /* String-heap pressure drives its own collection (see sp_str_heap_bytes).
     Collect BEFORE the new allocation, like sp_gc_alloc, so the string being
     built isn't yet live during the sweep. Operands of the calling op (e.g. the
     arguments to sp_str_concat) must be reachable across this point -- they are
     for rooted locals; the codegen's SP_GC_ROOT discipline is what keeps them
     so. Threshold recompute mirrors sp_gc_alloc's. */
  if (!sp_str_stress_checked) { sp_str_stress_checked = 1; const char *e = getenv("SPINEL_GC_STRESS"); if (e && *e && *e != '0') { SP_GC_CTR_SET(sp_str_threshold, 2048); sp_str_threshold_init = 2048; } }
  if (SP_GC_CTR_GET(sp_str_heap_bytes) > sp_str_threshold) {
#ifdef SP_THREADS
    /* Same as sp_gc_alloc: a string-heap collection must stop the world too,
       else a worker still mutating the object/string heap races the sweep.
       Drop the heap lock before the barrier so we stay parkable. */
    SP_HEAP_UNLOCK();
    sp_stw_collect();
    SP_HEAP_LOCK();
#else
    sp_str_collect_retune();         /* sp_gc_collect runs sp_str_sweep */
#endif
  }
  sp_str_hdr *h = (sp_str_hdr *)malloc(total);
  if (!h) sp_oom_die();
  h->next = sp_str_heap;
  h->size = (uint32_t)total;
  h->len = (uint32_t)len;
  h->hash = 0;
  sp_str_heap = h;
  SP_GC_CTR_ADD(sp_str_heap_bytes, total);
  SP_HEAP_UNLOCK();
  /* Don't fold string-heap pressure into sp_gc_bytes: the threshold heuristic
     in sp_gc_alloc is keyed on object-heap survivors, and the str-heap sweep
     that runs alongside (sp_str_sweep, from sp_gc_collect) doesn't add surviving
     strings back into sp_gc_bytes. Folding them in over-fires the object
     heuristic (the reason they're excluded). */
  char *body = (char *)(h + 1);
  body[0] = (char)0xfe;
  body[1 + len] = 0;
  return body + 1;
}

/* Raw variant: the caller writes a NUL-terminated payload whose final
   length it doesn't know yet (worst-case sized transforms: dump, gsub,
   tr, ...). Leave the header length unset so sp_str_byte_len answers
   strlen until sp_str_set_len records the real (possibly NUL-embedded)
   length; a capacity left in the header would over-read on concat and
   break byte-exact equality. */
#define SP_STR_LEN_UNSET 0xFFFFFFFFu
static inline char *sp_str_alloc_raw(size_t total_with_null) {
  char *s = sp_str_alloc(total_with_null > 0 ? total_with_null - 1 : 0);
  (((sp_str_hdr *)(s - 1)) - 1)->len = SP_STR_LEN_UNSET;
  return s;
}

static inline size_t sp_str_byte_len(const char *s) {
  if (!s) return 0;
  unsigned char marker = ((const unsigned char *)s)[-1];
  /* 0xf1 (frozen heap string / frozen literal) also carries a real sp_str_hdr
     whose len is the true byte length, so an embedded NUL survives freezing
     (#2462 dedup, .freeze). */
  if (marker == 0xfe || marker == 0xfc || marker == 0xfd || marker == 0xf1) {
    uint32_t l = (((const sp_str_hdr *)(s - 1)) - 1)->len;
    if (l != SP_STR_LEN_UNSET) return l;
  }
  return strlen(s);
}

static inline void sp_str_set_len(char *s, size_t len) {
  if (!s) return;
  unsigned char marker = ((unsigned char *)s)[-1];
  if (marker == 0xfe || marker == 0xfc || marker == 0xfd || marker == 0xf1) {
    sp_str_hdr *hd = ((sp_str_hdr *)(s - 1)) - 1;
    hd->len = (uint32_t)len;
    hd->hash = 0;  /* length change implies content change: invalidate cached hash */
  }
}

static inline const char *sp_str_from_bytes(const char *data, size_t len) {
  char *s = sp_str_alloc(len);
  if (data) memcpy(s, data, len);
  s[len] = 0;
  return s;
}
static inline const char *sp_str_dup_external(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *r = sp_str_alloc(n);
  memcpy(r, s, n);
  return r;
}

/* Integer / Float -> decimal string. Shared here (over the string heap) so cold
   readers such as lib/sp_json.c can format numbers without sp_runtime.h. */
/* Interpolation writers: append one part into a caller-provided buffer and
   return the new tail. emit_interp sizes the buffer from static bounds
   (SP_W_INT_MAX digits per int, literal lengths) plus strlen of the
   pre-evaluated dynamic parts, so one sp_str_alloc_raw serves the whole
   string (previously: one heap string per part + an sp_sprintf pass). */
#define SP_W_INT_MAX 21  /* -9223372036854775808 */
static inline char *sp_w_int(char *p, mrb_int n) {
  if (n == SP_INT_NIL) return p;  /* a nil int slot interpolates as "" */
  uint64_t u;
  if (n < 0) { *p++ = '-'; u = (uint64_t)(-(n + 1)) + 1; }
  else u = (uint64_t)n;
  char tmp[SP_W_INT_MAX]; int i = 0;
  do { tmp[i++] = (char)('0' + (u % 10)); u /= 10; } while (u > 0);
  while (i > 0) *p++ = tmp[--i];
  return p;
}
/* NOTE: s must be a marked spinel string (heap or codegen literal) --
   sp_str_byte_len reads the marker byte at s[-1], which is out of bounds on
   a foreign C literal. emit_interp therefore uses strlen + memcpy for
   dynamic string parts instead of this helper. */
static inline char *sp_w_str(char *p, const char *s) {
  if (!s) return p;
  size_t l = sp_str_byte_len(s);
  memcpy(p, s, l);
  return p + l;
}
static inline char *sp_w_bool(char *p, mrb_bool v) {
  if (v) { memcpy(p, "true", 4); return p + 4; }
  memcpy(p, "false", 5); return p + 5;
}

static inline const char *sp_int_to_s(mrb_int n){char*b=sp_str_alloc_raw(32);int len=snprintf(b,32,"%lld",(long long)n);if(len<0)len=0;sp_str_set_len(b,(size_t)len);return b;}
/* Float#to_s (Ruby semantics): the shortest decimal that round-trips, fixed
   point for a decimal exponent in [-4, 15], scientific otherwise; NaN, ±Infinity
   and -0.0 match CRuby's spelling. */
/* Float#to_s / #inspect: shortest round-trip decimal. Cold (display only) and
   large, so it lives out-of-line in sp_alloc.c rather than inlining into every
   TU that formats a float. */
const char *sp_float_to_s(mrb_float f);

/* ---- object construction (shared so lib C files can build values) ----
   The built-in cls_id sentinels, the core sp_box_* constructors, the object
   allocator, and sp_PolyArray. Moved here from sp_runtime.h so a standalone TU
   (sp_pack.c, sp_strscan.c, ...) can allocate and box without the per-TU heap
   state. SP_BUILTIN_FOREIGN_PTR/COMPLEX/RATIONAL are in sp_gc.h. */
#define SP_BUILTIN_ARRAY_OF(tag) (-(tag) - 1)
#define SP_BUILTIN_INT_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_INT) /* -1 */
#define SP_BUILTIN_STR_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_STR) /* -2 */
#define SP_BUILTIN_FLT_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_FLT) /* -3 */
#define SP_BUILTIN_PTR_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_OBJ) /* -6 */
#define SP_BUILTIN_SYM_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_SYM) /* -7 */
#define SP_BUILTIN_PROC (-9)
#define SP_BUILTIN_RANGE (-10)
#define SP_BUILTIN_TIME (-11)
#define SP_BUILTIN_POLY_ARRAY (-12)
#define SP_BUILTIN_STR_INT_HASH (-13)
#define SP_BUILTIN_STR_STR_HASH (-14)
#define SP_BUILTIN_INT_STR_HASH (-15)
#define SP_BUILTIN_SYM_INT_HASH (-16)
#define SP_BUILTIN_SYM_STR_HASH (-17)
#define SP_BUILTIN_STR_POLY_HASH (-18)
#define SP_BUILTIN_SYM_POLY_HASH (-19)
#define SP_BUILTIN_POLY_POLY_HASH (-20)
#define SP_BUILTIN_OBJECT        (-21)
#define SP_BUILTIN_FIBER         (-22)
#define SP_BUILTIN_IO            (-23)
#define SP_BUILTIN_METHOD        (-24)
#define SP_BUILTIN_ENUMERATOR    (-25)
/* Exception lived at -13, aliasing SP_BUILTIN_STR_INT_HASH, which both put
   exceptions inside the hash cls_id block [-20,-13] (breaking is_a?(Hash) /
   poly .class for exceptions) and made a str_int_hash arriving as a poly value
   misdispatch through the Exception inspect path. Give it a distinct id below
   the hash block so the two no longer collide. */
#define SP_BUILTIN_EXCEPTION     (-28)
#define SP_BUILTIN_THREAD        (-29)
#define SP_BUILTIN_QUEUE         (-30)
#define SP_BUILTIN_MUTEX         (-31)
#define SP_BUILTIN_CONDVAR       (-32)
/* Integer-keyed Integer-valued hash. It sits BELOW the contiguous hash block
   [-20,-13] (which was full), so the "is this a hash" range checks that test
   that block must also test for this id explicitly. */
#define SP_BUILTIN_INT_INT_HASH  (-34)
#define SP_BUILTIN_BASIC_OBJECT  (-37)  /* a bare BasicObject.new instance */
#define SP_BUILTIN_DIR           (-38)  /* an open directory handle (sp_Dir *) */

static inline sp_RbVal sp_box_int(mrb_int v)    { sp_RbVal r; r.tag = SP_TAG_INT;  r.cls_id = 0; r.v.i = v; return r; }
/* A NULL char* IS Ruby nil throughout the string paths (the nullable-string
   invariant); preserve that across the boxing boundary, or a boxed nil-string
   carries SP_TAG_STR and fails tag-keyed comparisons -- `defined?(x).should
   == nil` compared STR(NULL) against NIL and answered false. */
static inline sp_RbVal sp_box_str(const char *v){ sp_RbVal r; if (!v) { r.tag = SP_TAG_NIL; r.cls_id = 0; r.v.s = NULL; return r; } r.tag = SP_TAG_STR;  r.cls_id = 0; r.v.s = v; return r; }
static inline sp_RbVal sp_box_float(mrb_float v){ sp_RbVal r; r.tag = SP_TAG_FLT;  r.cls_id = 0; r.v.f = v; return r; }
/* Write the full union word, not just the narrow `b` member: hash keys and
   poly equality compare bool values through `v.i`, so bytes left
   uninitialized here make two `true`s unequal (a garbage-dependent hash). */
static inline sp_RbVal sp_box_bool(mrb_bool v)  { sp_RbVal r; r.tag = SP_TAG_BOOL; r.cls_id = 0; r.v.i = (v != 0); return r; }
static inline sp_RbVal sp_box_nil(void)         { sp_RbVal r; r.tag = SP_TAG_NIL;  r.cls_id = 0; r.v.i = 0; return r; }
static inline sp_RbVal sp_box_obj(void *p, int cls_id) { sp_RbVal r; r.tag = SP_TAG_OBJ; r.cls_id = cls_id; r.v.p = p; return r; }
static inline sp_RbVal sp_box_sym(sp_sym v)     { if (v == (sp_sym)-1) { sp_RbVal n; n.tag = SP_TAG_NIL; n.cls_id = 0; n.v.i = 0; return n; } sp_RbVal r; r.tag = SP_TAG_SYM;  r.cls_id = 0; r.v.i = (mrb_int)v; return r; }  /* (sp_sym)-1 is the nilable-symbol sentinel: box it as nil, never as :"" */
static inline sp_RbVal sp_box_poly_array(void *p) { return sp_box_obj(p, SP_BUILTIN_POLY_ARRAY); }

/* GC object allocator. The threshold/stress state is extern (defined in
   sp_alloc.c) so every TU shares it -- the same model as sp_gc_heap/bytes.
   sp_gc_alloc itself is an external function (defined in sp_alloc.c) so the
   cold lib C files that already link it (sp_fiber.c, sp_io.c, sp_bigint.c)
   keep resolving the same symbol. */
extern size_t sp_gc_threshold;
extern size_t sp_gc_threshold_init;
extern int sp_gc_stress_checked;
void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *));
void *sp_gc_alloc_nogc(size_t sz, void (*fin)(void *), void (*scn)(void *));

__attribute__((noreturn)) void sp_raise_cls(const char *cls, const char *msg);  /* lib/sp_core.c */
__attribute__((noreturn)) void sp_raise_frozen_str(const char *s);              /* lib/sp_str.c */
/* The message carries the rodata marker byte: an in-flight exception's msg is
   marked by the GC (sp_mark_string reads s[-1]), so a bare literal -- whose
   [-1] is out of bounds -- would be UB when it lands at a section edge. */
static void __attribute__((noinline, cold)) sp_raise_frozen_array(void) { sp_raise_cls("FrozenError", (&("\xff" "can't modify frozen Array")[1])); }

/* sp_PolyArray: a growable array of boxed values. */
typedef struct { sp_RbVal *data; mrb_int len; mrb_int cap; mrb_int frozen; } sp_PolyArray;
static inline void sp_PolyArray_scan(void *p) { sp_PolyArray *a = (sp_PolyArray *)p; for (mrb_int i = 0; i < a->len; i++) sp_mark_rbval(a->data[i]); }
static inline void sp_PolyArray_fin(void *p) { sp_PolyArray *a = (sp_PolyArray *)p; sp_gc_hdr *h = (sp_gc_hdr *)((char *)a - sizeof(sp_gc_hdr)); SP_GC_CTR_SUB(sp_gc_bytes, sizeof(sp_RbVal) * a->cap); h->size -= sizeof(sp_RbVal) * a->cap; free(a->data); }
static inline sp_PolyArray *sp_PolyArray_new(void) { sp_PolyArray *a = (sp_PolyArray *)sp_gc_alloc(sizeof(sp_PolyArray), sp_PolyArray_fin, sp_PolyArray_scan); a->cap = 16; a->data = (sp_RbVal *)malloc(sizeof(sp_RbVal) * a->cap); if (!a->data) sp_oom_die(); a->len = 0; { sp_gc_hdr *h = (sp_gc_hdr *)((char *)a - sizeof(sp_gc_hdr)); h->size += sizeof(sp_RbVal) * a->cap; SP_GC_CTR_ADD(sp_gc_bytes, sizeof(sp_RbVal) * a->cap); } return a; }
static inline void sp_PolyArray_push(sp_PolyArray *a, sp_RbVal v) { if (!a) return; if (a->frozen) { sp_raise_frozen_array(); return; } if (a->len >= a->cap) { sp_gc_hdr *h = (sp_gc_hdr *)((char *)a - sizeof(sp_gc_hdr)); SP_GC_CTR_SUB(sp_gc_bytes, sizeof(sp_RbVal) * a->cap); h->size -= sizeof(sp_RbVal) * a->cap; a->cap = (a->cap * 2) + 1; void *nd = realloc(a->data, sizeof(sp_RbVal) * a->cap); if (!nd) sp_oom_die(); a->data = (sp_RbVal *)nd; h->size += sizeof(sp_RbVal) * a->cap; SP_GC_CTR_ADD(sp_gc_bytes, sizeof(sp_RbVal) * a->cap); } a->data[a->len++] = v; }
static inline sp_RbVal sp_PolyArray_get(sp_PolyArray *a, mrb_int i) { if (!a) return sp_box_nil(); if (i < 0) i += a->len; if (i < 0 || i >= a->len) return sp_box_nil(); return a->data[i]; }
#endif /* SP_ALLOC_H */
