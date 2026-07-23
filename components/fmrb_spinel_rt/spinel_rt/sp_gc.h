/* sp_gc.h -- the mark/sweep collector's shared surface.
 *
 * Included by both the generated translation unit (via sp_runtime.h) and
 * lib/sp_gc.c, which holds the collector's non-inline machinery (mark,
 * sweep, collect, the memory-limit governor, and the SPINEL_GC_VERIFY
 * support). The hot inline mark helpers stay here so both sides inline
 * them -- moving the cold collector body to a single compiled unit must
 * not de-inline the per-object mark path. The collector globals are
 * declared extern here and defined once in lib/sp_gc.c.
 */
#ifndef SP_GC_H
#define SP_GC_H

#include "sp_types.h"

/* ---- Value tag constants + the boxed value (sp_RbVal) ----
 * The mark helpers below dispatch on the tag, so the type lives here
 * rather than in the generated TU. */
#define SP_TAG_INT 0
#define SP_TAG_STR 1
#define SP_TAG_FLT 2
#define SP_TAG_BOOL 3
#define SP_TAG_NIL 4
#define SP_TAG_OBJ 5
#define SP_TAG_SYM 6
#define SP_TAG_CLASS 7
#define SP_TAG_ENCODING 8
#define SP_TAG_BIGINT 9   /* v.p is a GC-allocated sp_Bigint* */
/* SP_TAG_OBJ cls_id sentinel for an opaque foreign/FFI pointer (e.g. a
   ffi_read_ptr / ffi func ptr return). It is NOT a sp_gc_alloc allocation, so
   the collector must not trace it -- sp_mark_rbval skips it. Kept here (not with
   the other SP_BUILTIN_* in sp_runtime.h) so the inline mark helper can see it.
   Value is the next free slot below SP_BUILTIN_METHOD (-24). */
#define SP_BUILTIN_FOREIGN_PTR (-25)
/* a compiled Regexp: malloc-owned (never GC heap), so like FOREIGN_PTR the
   collector must not trace it */
#define SP_BUILTIN_REGEX       (-33)
/* Wide value types (heap-copied crossing into a poly slot). Shared here so
   lib/sp_marshal.c can recognize them by cls_id. */
#define SP_BUILTIN_COMPLEX  (-26)
#define SP_BUILTIN_RATIONAL (-27)
/* A Rational whose numerator/denominator exceed mrb_int: a boxed object with
   two sp_Bigint* fields, distinct from the by-value int Rational (#2469). */
#define SP_BUILTIN_BIG_RATIONAL (-35)
/* A Float range (1.0..3.0): a boxed sp_FloatRange, distinct from the int-backed
   by-value Range so its endpoints are not truncated. */
#define SP_BUILTIN_FLOAT_RANGE (-36)
typedef struct { int tag; int cls_id; union { mrb_int i; const char *s; mrb_float f; mrb_bool b; void *p; } v; } sp_RbVal;

/* ---- Collector globals shared with the generated TU ----
 * Only the globals touched by both the kept hot path (sp_gc_alloc, the
 * SP_GC_ROOT macros, GC.stat, the fiber root hook) and the moved cold
 * body are extern; the rest stay static on whichever side owns them. */
/* Capacity of the stack-resident GC root array (sp_gc_roots). At 8 bytes/entry
   the default is a 512 KB static buffer -- ample for desktop, but the dominant
   static allocation in a minimal binary. Embedded targets can shrink it with
   -DSP_GC_STACK_MAX=<n> (pass the SAME value when building lib/sp_gc.c and the
   generated TU -- both consult this for the array and the SP_GC_ROOT bound).
   Too small overflows silently into a dropped root (UAF), so size it to the
   program's deepest live-root nesting. */
#ifndef SP_GC_STACK_MAX
#define SP_GC_STACK_MAX 65536
#endif
#define SP_GC_FULL_INTERVAL 8
/* Per-worker root stack (SP_TLS): each OS worker carries the active roots of
   the green thread it runs, swapped with the fiber's saved_roots on a context
   switch. Plain globals in the single-threaded build. */
extern SP_TLS void **sp_gc_roots[SP_GC_STACK_MAX];
extern SP_TLS int sp_gc_nroots;

/* GC root tracking. SP_GC_ROOT registers a stack-resident root with a
   cleanup-attribute sentinel so it auto-pops when its declaring scope ends.
   Shared here (was in sp_runtime.h) so standalone lib C files -- e.g. the
   Marshal loader, which builds GC arrays/hashes across a recursive parse --
   can root their in-flight objects too. Helpers touch only the extern root
   stack above, so relocating them is layout-neutral. */
static inline int _sp_gc_root_push(void **p) {
  if (sp_gc_nroots < SP_GC_STACK_MAX) { sp_gc_roots[sp_gc_nroots++] = p; return 1; }
  return 0;
}
static inline void _sp_gc_root_pop(int *added) { if (*added) sp_gc_nroots--; }
static inline void sp_gc_cleanup(int *p) { sp_gc_nroots = *p; }
#define _SP_GC_CONCAT2(a,b) a##b
#define _SP_GC_CONCAT(a,b) _SP_GC_CONCAT2(a,b)
#define SP_GC_SAVE() int __attribute__((cleanup(sp_gc_cleanup))) _gc_saved = sp_gc_nroots
#define SP_GC_ROOT(v) int __attribute__((cleanup(_sp_gc_root_pop))) _SP_GC_CONCAT(_sp_gcr_, __COUNTER__) = _sp_gc_root_push((void**)&(v))
/* Root a poly (sp_RbVal) local: tag the stored slot's low bit so the mark
   walker routes it through sp_mark_rbval (the object pointer sits in a union at
   a nonzero offset, only for STR/OBJ tags). */
#define SP_GC_ROOT_RBVAL(v) int __attribute__((cleanup(_sp_gc_root_pop))) _SP_GC_CONCAT(_sp_gcr_, __COUNTER__) = _sp_gc_root_push((void**)((uintptr_t)&(v) | (uintptr_t)1))
/* Root a string slot that may hold a NON-spinel pointer (a stack line
   buffer from sp_File_gets_buf, an external char*): tag bit 2 routes the
   mark through sp_mark_string, which touches nothing unless the marker
   byte is exactly 0xfe -- safe on arbitrary memory, unlike sp_gc_mark's
   header walk. Use this for string parameters in runtime helpers. */
#define SP_GC_ROOT_STR(v) int __attribute__((cleanup(_sp_gc_root_pop))) _SP_GC_CONCAT(_sp_gcr_, __COUNTER__) = _sp_gc_root_push((void**)((uintptr_t)&(v) | (uintptr_t)2))
#define SP_GC_RESTORE() sp_gc_nroots = _gc_saved
extern sp_gc_hdr *sp_gc_heap;
extern size_t sp_gc_bytes;
extern size_t sp_gc_old_bytes;
extern int sp_gc_cycle;
extern void (*sp_gc_mark_suspended_fibers_hook)(void);

/* Heap byte-counter accounting. The container growth paths (sp_array.h,
   sp_alloc.h's PolyArray, the string builder) adjust sp_gc_bytes inline
   WITHOUT the heap lock -- at N>1 workers that is a data race against the
   locked allocators and against each other (torn counters skew the GC
   trigger; TSan flags it). Under SP_THREADS every update and every
   trigger-decision read goes through a relaxed atomic: the counter is a
   heuristic (collection thresholds), so relaxed ordering is enough -- no
   other data is published through it. Collector-side code that runs under
   stop-the-world (sweep, retune) keeps plain accesses: every mutator is
   parked at the barrier, which already gives the happens-before edge.
   The single-threaded build expands to the exact plain +=/-= it had, so
   that archive stays byte-identical. */
#ifdef SP_THREADS
#define SP_GC_CTR_ADD(ctr, n) __atomic_fetch_add(&(ctr), (size_t)(n), __ATOMIC_RELAXED)
#define SP_GC_CTR_SUB(ctr, n) __atomic_fetch_sub(&(ctr), (size_t)(n), __ATOMIC_RELAXED)
#define SP_GC_CTR_GET(ctr)    __atomic_load_n(&(ctr), __ATOMIC_RELAXED)
#define SP_GC_CTR_SET(ctr, n) __atomic_store_n(&(ctr), (size_t)(n), __ATOMIC_RELAXED)
#else
#define SP_GC_CTR_ADD(ctr, n) ((ctr) += (size_t)(n))
#define SP_GC_CTR_SUB(ctr, n) ((ctr) -= (size_t)(n))
#define SP_GC_CTR_GET(ctr)    (ctr)
#define SP_GC_CTR_SET(ctr, n) ((ctr) = (size_t)(n))
#endif

/* Push a header onto the shared sp_gc_heap list. Under SP_THREADS this is a
   lock-free CAS push so callers that hold no lock (the pool-hit relink) stay
   off the heap mutex; the allocators, which hold the mutex anyway for the
   collect trigger, use the same push so every writer to the list head agrees
   on one protocol (a plain locked store racing an unlocked CAS would itself
   be a race). The `next` store is atomic: a node being re-linked from a
   shared pool free list may still have a stale pool popper reading its
   `next` (that popper's CAS then fails and discards the value, but the read
   itself must be defined). Removals happen only in the stop-the-world sweep
   with every mutator parked, so the push never races a pop and needs no ABA
   defense. Release order publishes the node's initialized header to the
   collector. */
#ifdef SP_THREADS
#define SP_GC_HEAP_PUSH(hdr) do { \
    sp_gc_hdr *_sp_old = __atomic_load_n(&sp_gc_heap, __ATOMIC_RELAXED); \
    do { __atomic_store_n(&(hdr)->next, _sp_old, __ATOMIC_RELAXED); } \
    while (!__atomic_compare_exchange_n(&sp_gc_heap, &_sp_old, (hdr), 1, \
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)); \
  } while (0)
#else
#define SP_GC_HEAP_PUSH(hdr) do { (hdr)->next = sp_gc_heap; sp_gc_heap = (hdr); } while (0)
#endif

/* ---- Collector entry points (defined in lib/sp_gc.c) ---- */
void sp_gc_mark(void *obj);
void sp_gc_mark_all(void);
void sp_gc_collect(void);
void sp_gc_enforce_mem_limit(void);
/* Collect + re-tune the threshold, assuming exclusive heap access (see
   sp_alloc.c). sp_stw_collect (sp_sched.c, threaded build) stops the world then
   runs sp_gc_collect_retune; the single-threaded allocator calls it directly
   under the heap lock. */
void sp_gc_collect_retune(void);
void sp_stw_collect(void);
void sp_oom_die(void);

/* ---- Embedder callbacks supplied by the generated TU ----
 * The collector cannot own the program's roots or string heap (they are
 * static state in the generated TU: the regexp match globals, ARGV, the
 * in-flight exception stack, and the heap-string free list). The TU
 * installs its mark-roots and string-sweep callbacks here at startup;
 * sp_gc_mark_all / sp_gc_collect invoke them through these pointers, the
 * same way fibers register sp_gc_mark_suspended_fibers_hook. */
extern void (*sp_gc_mark_globals_hook)(void);
extern void (*sp_gc_str_sweep_hook)(void);

/* ---- value-introspection hooks (set by the generated TU at startup) ----
 * lib/sp_json.c (and other cold readers) own no container types; they reach the
 * generated TU's typed arrays/hashes only through these generic readers, the
 * same idiom as the GC hooks above. sp_sym_name maps a symbol id to its name;
 * sp_json_kind classifies a boxed value (1=array, 2=hash, 0=other); len/aref
 * iterate any array; hpair yields a hash's (key,value) at insertion index i. */
extern const char *(*sp_sym_name_fn)(sp_sym);
extern int (*sp_json_kind_fn)(sp_RbVal);
extern mrb_int (*sp_json_len_fn)(sp_RbVal);
extern sp_RbVal (*sp_json_aref_fn)(sp_RbVal, mrb_int);
extern void (*sp_json_hpair_fn)(sp_RbVal, mrb_int, sp_RbVal *, sp_RbVal *);
/* Container BUILDERS for JSON.parse (installed by the generated TU, which owns
   the hash type): make an empty string-keyed hash, and set a (key, value) pair
   -- CRuby's JSON.parse returns String keys. Arrays are built directly from the
   package ABI (sp_PolyArray). */
extern sp_RbVal (*sp_json_mk_hash_fn)(void);
extern sp_sym (*sp_json_sym_intern_fn)(const char *);  /* symbolize_names key interner (TU-installed) */
extern void (*sp_json_hash_set_fn)(sp_RbVal, const char *, sp_RbVal);
/* Recursive #inspect of a boxed value, for lib/sp_inspect.c's container walker
   (set to sp_poly_inspect; same idiom as the JSON hooks). */
extern const char *(*sp_poly_inspect_fn)(sp_RbVal);
/* Convert a plain object (a Struct) to a boxed StrPoly hash of its members,
   generic (no format knowledge). The generated program installs it (switch on
   cls_id) when it has Structs and a package consumes it; a consumer such as
   the json package reads it to serialize an object as a hash. NULL otherwise. */
extern sp_RbVal (*sp_obj_to_hash_fn)(sp_RbVal);
/* default Object#inspect for user objects: the generated TU installs a
   per-class ivar walk (sp_obj_inspect_sw); sp_poly_inspect's OBJ default
   consults it so nested/boxed objects render like CRuby */
extern const char *(*sp_obj_inspect_fn)(int cls_id, void *p);
/* Same shape for user #to_s: sp_poly_to_s's OBJ default consults it so a
   boxed user object with a custom to_s renders through it. */
extern const char *(*sp_obj_to_s_fn)(int cls_id, void *p);

/* ---- Hot inline mark helpers (inlined into both sides) ----
 * String tag bytes: 0xfe heap-unmarked -> 0xfc marked; others skipped. */
static inline void sp_mark_string(const char *s) {
  if (!s) return;
  if ((unsigned char)s[-1] == 0xfe) {
    ((char *)s)[-1] = (char)0xfc;
  }
  /* No frozen (0xf1) branch here: this is inlined into optcarrot's GC mark and
     is layout-sensitive. A live frozen heap string is kept immortal by
     sp_str_sweep instead (#1449). */
}
static inline void sp_mark_rbval(sp_RbVal v) {
  if (v.tag == SP_TAG_STR) sp_mark_string(v.v.s);
  else if (v.tag == SP_TAG_OBJ && v.cls_id != SP_BUILTIN_FOREIGN_PTR &&
           v.cls_id != SP_BUILTIN_REGEX) sp_gc_mark(v.v.p);
  else if (v.tag == SP_TAG_BIGINT) sp_gc_mark(v.v.p);
}
/* Closure-cell content markers. A captured non-int local is laundered into the
   pointer-sized mrb_int cell as (uintptr_t)<ptr>; the cell's GC scan marks the
   referent so it survives as long as the capturing proc does. */
static inline void sp_cell_scan_str(void *p) { sp_mark_string(*(const char **)p); }
static inline void sp_cell_scan_ptr(void *p) { sp_gc_mark(*(void **)p); }
static inline void sp_cell_scan_rbval(void *p) { sp_mark_rbval(*(sp_RbVal *)p); }
/* A low-bit-tagged root entry is an sp_RbVal* (see SP_GC_ROOT_RBVAL);
   an untagged entry is a plain void** to a direct GC pointer. */
static inline void sp_gc_mark_root_entry(void **e) {
  uintptr_t u = (uintptr_t)e;
  if (u & (uintptr_t)1) { sp_mark_rbval(*(sp_RbVal *)(u & ~(uintptr_t)1)); }
  else if (u & (uintptr_t)2) { sp_mark_string(*(const char **)(u & ~(uintptr_t)2)); }
  else { void *o = *e; if (o) sp_gc_mark(o); }
}

#endif
