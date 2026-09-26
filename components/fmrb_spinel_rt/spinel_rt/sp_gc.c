/* sp_gc.c -- the mark/sweep collector's non-inline machinery.
 * See sp_gc.h. The program root-marking and string-heap sweep are
 * supplied by the generated TU via sp_gc_mark_globals_hook /
 * sp_gc_str_sweep_hook. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
/* malloc_trim is glibc's (returns freed arena pages to the OS); no other libc
   has it, so elsewhere the trim below is a no-op and RSS is whatever the
   allocator keeps. An interposed allocator (jemalloc) makes it a no-op on
   glibc too: the symbol walks glibc's own, then empty, arena. Under
   SP_MULTI_CTX allocation is routed through the instance backend (not the
   process heap), and pulling in <malloc.h> after sp_mem_override.h has
   remapped malloc/free is pointless, so the trim is a no-op there too. */
#if defined(__GLIBC__) && !defined(SP_MULTI_CTX)
#include <malloc.h>
#else
#define malloc_trim(x) ((void)0)
#endif
#include <unistd.h>
#include <time.h>
#include "sp_gc.h"
#include <signal.h>
#include <unistd.h>
#include "sp_marshal.h"   /* sp_marshal_vt -- the instance lives here (always linked) */
void sp_str_verify_begin(void);   /* lib/sp_alloc.c: string side of the generational check */
void sp_str_lcache_clear(void);   /* lib/sp_alloc.c: this thread's string-length cache */
void sp_str_mark_settle(int full);  /* lib/sp_alloc.c: the slab strings' old total, from this mark */
size_t sp_str_verify_end(void);
void sp_str_verify_probe_arm(void);
int sp_str_verify_probe_hit(void);
void sp_str_verify_probe_done(void);
int  sp_str_sweep_begin(int *major);   /* lib/sp_alloc.c: the string heap's gate, taken here before the concurrent sweep starts */

/* ---- Globals shared with the generated TU (declared extern in sp_gc.h) ----
 * Under SP_MULTI_CTX these names are macros onto sp_ctx fields (sp_ctx.h), so
 * their storage lives in the instance and is initialized by
 * sp_instance_create(); the definitions here are dropped. */
/* The overflow segment: roots SP_GC_STACK_MAX and up, grown on demand and
   kept for the worker's life. A push the array cannot take used to be dropped
   on the floor, and a deep enough recursion then had the collector free
   strings its live frames still named. Under SP_MULTI_CTX it stays NULL: the
   instance's root stack is one segment (below). */
SP_TLS void ***sp_gc_roots_ext = NULL;
#ifndef SP_MULTI_CTX
SP_TLS void **sp_gc_roots[SP_GC_STACK_MAX];   /* per-worker (SP_TLS); see sp_gc.h */
static SP_TLS int sp_gc_roots_ext_cap = 0;
int sp_gc_roots_ext_reserve(int n) {
  int need = n - SP_GC_STACK_MAX;
  if (need <= sp_gc_roots_ext_cap) return 1;
  int nc = sp_gc_roots_ext_cap ? sp_gc_roots_ext_cap : 4096;
  while (nc < need) nc *= 2;
  void ***nx = (void ***)realloc(sp_gc_roots_ext, sizeof(void **) * (size_t)nc);
  if (!nx) return 0;
  sp_gc_roots_ext = nx; sp_gc_roots_ext_cap = nc;
  return 1;
}
int sp_gc_root_push_slow(void **p) {
  if (!sp_gc_roots_ext_reserve(sp_gc_nroots + 1)) return 0;
  sp_gc_roots_ext[sp_gc_nroots - SP_GC_STACK_MAX] = p;
  sp_gc_nroots++;
  return 1;
}
SP_TLS int sp_gc_nroots = 0;
#else
/* SP_MULTI_CTX: the root stack is one per-instance segment of SP_GC_ROOTS_CAP
   entries, sized by sp_instance_create; there is no overflow segment. */
int sp_gc_roots_ext_reserve(int n) { return n <= SP_GC_ROOTS_CAP; }
/* A per-instance root stack can be sized small (sp_instance_config
   .root_stack_entries). Dropping a root here would silently un-anchor a live
   object and corrupt the heap on the next collection (a non-deterministic
   use-after-free), so fail loudly and deterministically instead. */
int sp_gc_root_push_slow(void **p) { (void)p; sp_gc_root_overflow_die(); return 0; }
#endif
#ifdef SP_THREADS
sp_gc_wslot_t sp_gc_wslot[SP_MAX_WORKERS];   /* per-worker young head + flush delta, cache-line padded */
#else
#ifndef SP_MULTI_CTX
sp_gc_hdr *sp_gc_heap = NULL;
#endif
#endif
#ifndef SP_MULTI_CTX
size_t sp_gc_bytes = 0;
size_t sp_gc_old_bytes = 0;
int sp_gc_cycle = 0;
void (*sp_gc_mark_suspended_fibers_hook)(void) = NULL;
void (*sp_gc_mark_globals_hook)(void) = NULL;
void (*sp_gc_str_sweep_hook)(void) = NULL;
#endif
int (*sp_gc_str_major_due_hook)(void) = NULL;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_root_phase = 0;   /* the mark is walking the C roots (see sp_gc_mark_all) */
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* Runtime value-introspection vtable, set once per program by the generated TU
 * (sp_re_init / codegen init). Per-instance under SP_MULTI_CTX (sp_ctx-field
 * macros in sp_ctx.h) so instance A dispatches over A's symbol/class tables. */
#ifndef SP_MULTI_CTX
const char *(*sp_sym_name_fn)(sp_sym) = NULL;
int (*sp_json_kind_fn)(sp_RbVal) = NULL;
sp_int (*sp_json_len_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_json_aref_fn)(sp_RbVal, sp_int) = NULL;
void (*sp_json_hpair_fn)(sp_RbVal, sp_int, sp_RbVal *, sp_RbVal *) = NULL;
sp_RbVal (*sp_json_mk_hash_fn)(void) = NULL;
sp_sym (*sp_json_sym_intern_fn)(const char *) = NULL;
void (*sp_json_hash_set_fn)(sp_RbVal, const char *, sp_RbVal) = NULL;
const char *(*sp_poly_inspect_fn)(sp_RbVal) = NULL;
const char *(*sp_poly_to_s_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_hash_fn)(sp_RbVal) = NULL;
/* A user class's own #to_json, keyed by cls_id: the json package asks for it
   before falling back to the generic field reflection. */
const char *(*sp_obj_to_json_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_h_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_a_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_ary_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_deconstruct_fn)(sp_RbVal) = NULL;
int (*sp_obj_is_data_fn)(int) = NULL;
sp_RbVal (*sp_obj_with_fn)(sp_RbVal, sp_RbVal) = NULL;
const char *(*sp_obj_inspect_fn)(int cls_id, void *p) = NULL;
const char *(*sp_obj_to_s_fn)(int cls_id, void *p) = NULL;
sp_int (*sp_obj_to_int_fn)(int cls_id, void *p, int *ok) = NULL;
const char *(*sp_obj_to_str_fn)(int cls_id, void *p) = NULL;
const char *(*sp_obj_to_path_fn)(int cls_id, void *p) = NULL;
int (*sp_obj_conv_fn)(int cls_id, void *p, int which, sp_RbVal *out) = NULL;
const char *(*sp_obj_cls_name_fn)(int cls_id) = NULL;
int (*sp_class_le_id_fn)(int sub, int super) = NULL;
#endif
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
sp_marshal_vt sp_marshal_v = {0};   /* filled by the generated TU (sp_tu_init) */
#endif

/* The concurrent sweep (sp_sched.c): start takes the lists the barrier
   detached and sweeps them beside the mutators; wait, at the next barrier,
   finishes the previous one -- joins the sweepers, splices the survivors,
   recycles the pooled dead, retunes the string budget. NULL: sweep under the
   barrier as before. */
void (*sp_gc_conc_sweep_hook)(int full, int str_sweep, int str_major) = NULL;
void (*sp_gc_conc_wait_hook)(void) = NULL;
int sp_gc_conc_on = -1;          /* SPINEL_GC_CONC=0 turns the concurrent sweep off */
int sp_gc_conc_promote = 0;      /* this cycle's mark promotes what it marks (the sweep is concurrent) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
size_t sp_gc_mk_bytes = 0, sp_gc_mk_young_bytes = 0;   /* bytes the mark reached, and of those the young ones */
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
SP_TLS int sp_gc_in_sweeper = 0; /* a sweeper thread: finalizers skip the per-worker byte accounting */
/* ---- Collector-private globals ----
 * Under SP_MULTI_CTX the relocated ones are sp_ctx fields (macros in sp_ctx.h);
 * the static definitions are dropped so the state is per-instance. */
/* Initial work-list capacity for the mark phase, allocated lazily as one block
   of SP_GC_MARK_STACK_MAX pointers -- 512KB on LP64, 256KB on ILP32. When it
   fills, sp_gc_mark doubles it (below), and only a failed grow falls back to
   recursing through the scan hook. Ports whose heap is a fixed pool of a few
   hundred KB must shrink it with -DSP_GC_MARK_STACK_MAX=<n>; at the default a
   single instance asks its allocator for a contiguous block bigger than the
   whole pool it was given. Keep it comfortably above the object count the pool
   can hold, so the recursive path stays rare. */
#ifndef SP_GC_MARK_STACK_MAX
#define SP_GC_MARK_STACK_MAX (1024*64)
#endif
/* How far the work list may double before a full one falls back to recursing
   through the scan hook (sp_gc_mark). A port that would rather spend C stack
   than pool on a deep graph sets this to SP_GC_MARK_STACK_MAX (no growth). */
#ifndef SP_GC_MARK_STACK_LIMIT
#define SP_GC_MARK_STACK_LIMIT (1<<28)
#endif
/* The grow itself. Under SP_MULTI_CTX realloc is the instance's allocator,
   which ends the program when its pool is exhausted; a mark stack that cannot
   grow should recurse instead, as it does on a hosted heap, so it asks the
   pool without that consequence. */
#ifdef SP_MULTI_CTX
void *sp_mem_try_realloc(void *p, size_t n);   /* sp_ctx.c: NULL on exhaustion */
#define SP_GC_MARK_REALLOC(p, n) sp_mem_try_realloc((p), (n))
#else
#define SP_GC_MARK_REALLOC(p, n) realloc((p), (n))
#endif
#ifndef SP_MULTI_CTX
static int sp_gc_verify = 0;
static sp_gc_hdr *sp_gc_old_heap = NULL;
#endif
/* The mark stack grows on demand: overflowing it used to drop the walk into
   recursive scanning, and a live set of a few hundred thousand containers
   (an A* frontier of [vertex, priority] pairs) then overflowed the C stack
   and crashed the process mid-collection. SP_GC_MARK_STACK_MAX (above) is
   the size it starts at. */
/* Per thread: the collector's, and under the parallel mark each helper's
   own; a scan pushes onto the stack of the thread running it. */
#ifndef SP_MULTI_CTX
static SP_TLS void **sp_gc_mark_stack = NULL;
static SP_TLS int sp_gc_mark_top = 0;
#endif
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static SP_TLS int sp_gc_mark_cap = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* What this thread's marking counted, folded into the totals when its
   drain ends (a shared counter per marked object would bounce a line
   between the markers on every object). */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static SP_TLS size_t sp_gc_mkl_marked = 0, sp_gc_mkl_bytes = 0, sp_gc_mkl_young = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
static int sp_gc_par_mark = 0;   /* the drain is running on several threads */
static SP_TLS int sp_gc_mk_is_collector=0;
unsigned long long sp_gc_ph_mk_by_helpers=0, sp_gc_ph_mk_spills=0, sp_gc_ph_mk_takes=0;
double sp_gc_ph_mk_drain=0, sp_gc_ph_mk_join=0, sp_gc_ph_mk_idle=0;
int sp_gc_par_mark_on = -1;      /* SPINEL_GC_PAR_MARK=0 turns it off */
static void sp_gc_mkl_fold(void);
#ifndef SP_MULTI_CTX
static sp_gc_hdr **sp_gc_vsnap = NULL;
static size_t sp_gc_vsnap_n = 0, sp_gc_vsnap_cap = 0;
static size_t sp_gc_max_bytes = 0;
static int sp_gc_max_bytes_init = 0;
#endif
#define SP_GC_FULL_INTERVAL 8
#define SP_GC_FULL_INTERVAL_MIN 1   /* the adaptive floor under the minor mark: every cycle full */
#define SP_GC_PROMOTED_DEAD_CUT (1.0/16.0)   /* dead-at-full per minor, as a share of the live set */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static int sp_gc_minors_since_full = 0;
static double sp_gc_last_per_minor = 0;
static int sp_gc_fulls_at_min = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

/* Major-collection cadence. A compile-time constant until the measurements in
   docs/internals/gc-string-minor-design.md showed it is where essentially all
   of a minor mark's remaining cost lives (231 ms at 8 against 46 at 64 on a
   retained-heavy array workload). Runtime state, in this file, so the header
   every generated TU includes does not change. */
#define SP_GC_FULL_INTERVAL_MAX 128
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static int sp_gc_full_interval = SP_GC_FULL_INTERVAL;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* What the last full sweep found LIVE in the old generation, and the slack a
   heap may grow past it before a full is forced. The adaptive interval bounds
   the TIME between full sweeps and nothing bounded the SPACE: a phase that
   promotes heavily piles garbage onto a list nothing walks, sp_gc_old_bytes
   counts it as live, the object threshold retunes off that inflated total, and
   the interval's own correction is gated behind the full cycle it is
   postponing (#4076). A heap that really is live never trips this, because old
   stays where the last sweep left it. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
size_t sp_gc_old_live = 0;   /* what the last full found live in the old generation */
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#define SP_GC_OLD_GROWTH_SLACK (4u * 1024u * 1024u)
static int sp_gc_full_interval_fixed = 0;   /* SPINEL_GC_FULL_INTERVAL pins it */
#ifdef SP_MULTI_CTX
/* The interval an instance starts from: SPINEL_GC_FULL_INTERVAL when set,
   read once by the constructor below; sp_gc_instance_init copies it into the
   instance, whose own interval then adapts on its own heap. */
static int sp_gc_full_interval_start = SP_GC_FULL_INTERVAL;
#endif
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_full_runs = 0;    /* read by GC.stat (lib/sp_cold.c) */
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
int sp_gc_trim_wanted = 0;  /* a full cycle asks the trimmer thread for a malloc_trim (sp_sched.c) */
int sp_gc_trimmer_on = 0;   /* the trimmer thread is running */
/* High-water mark of the remembered set, for GC.stat. A minor collection
   walks every entry and runs its scan, so a workload that stores into most
   of its old heap makes the minor do the full mark's work plus the
   bookkeeping -- which is the shape to check when a minor mark is SLOWER
   than the whole-heap one on some route. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_rem_peak = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

/* Issue #755: bail out cleanly on OOM rather than returning NULL into a
   caller that would deref it next. */
void sp_oom_die(void){fputs("unhandled exception: out of memory\n",stderr);exit(1);}
#ifdef SP_MULTI_CTX
void sp_gc_root_overflow_die(void){fputs("unhandled exception: GC root stack overflow (sp_instance_config.root_stack_entries too small)\n",stderr);abort();}
#endif

/* ---- GC verify (SPINEL_GC_VERIFY=1): a sorted snapshot of every
 * registered header, so the scan-time membership test is O(log n). ---- */
static int sp_gc_vsnap_cmp(const void *a, const void *b){ uintptr_t x=(uintptr_t)*(sp_gc_hdr*const*)a, y=(uintptr_t)*(sp_gc_hdr*const*)b; return x<y?-1:x>y?1:0; }
static void sp_gc_vsnap_push(sp_gc_hdr *h){ if(sp_gc_vsnap_n==sp_gc_vsnap_cap){ size_t c=sp_gc_vsnap_cap?sp_gc_vsnap_cap*2:1024; sp_gc_hdr**n=(sp_gc_hdr**)realloc(sp_gc_vsnap,c*sizeof(sp_gc_hdr*)); if(!n)sp_oom_die(); sp_gc_vsnap=n; sp_gc_vsnap_cap=c; } sp_gc_vsnap[sp_gc_vsnap_n++]=h; }
static void sp_gc_verify_snapshot(void){ sp_gc_vsnap_n=0;
#ifdef SP_THREADS
  { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS; for(int i=0;i<n;i++)for(sp_gc_hdr*p=sp_gc_wslot[i].young;p;p=p->next)sp_gc_vsnap_push(p); }
#else
  for(sp_gc_hdr*p=sp_gc_heap;p;p=p->next)sp_gc_vsnap_push(p);
#endif
  for(sp_gc_hdr*p=sp_gc_old_heap;p;p=p->next)sp_gc_vsnap_push(p); if(sp_gc_vsnap_n>1)qsort(sp_gc_vsnap,sp_gc_vsnap_n,sizeof(sp_gc_hdr*),sp_gc_vsnap_cmp); }
static int sp_gc_obj_registered(sp_gc_hdr *h){ if(sp_slab_owns(h))return sp_slab_is_live(h)&&!sp_slab_is_str(h); uintptr_t hv=(uintptr_t)h; size_t lo=0,hi=sp_gc_vsnap_n; while(lo<hi){ size_t m=lo+(hi-lo)/2; uintptr_t x=(uintptr_t)sp_gc_vsnap[m]; if(x==hv)return 1; if(x<hv)lo=m+1; else hi=m; } return 0; }
/* Verify diagnostics: which phase/slot the bad pointer came from. The phase
 * label stays process-shared (a diagnostic string); the offending ctx pointer
 * is per-instance under SP_MULTI_CTX (macro in sp_ctx.h). */
int sp_gc_verify_on(void) { return sp_gc_verify; }
const char *sp_gc_dbg_phase = "?";
#ifndef SP_MULTI_CTX
void *sp_gc_dbg_ctx = NULL;
#endif
static void sp_gc_verify_fail(void *obj, sp_gc_hdr *h){
  fprintf(stderr, "  [phase=%s ctx=%p]\n", sp_gc_dbg_phase, sp_gc_dbg_ctx);
  sp_slab_describe(h);
  if (sp_gc_dbg_ctx) sp_slab_describe((char*)sp_gc_dbg_ctx - sizeof(sp_gc_hdr));
  if (sp_gc_dbg_ctx && sp_gc_dbg_phase && sp_gc_dbg_phase[0] == 's') {
    sp_gc_hdr *hc = (sp_gc_hdr *)((char *)sp_gc_dbg_ctx - sizeof(sp_gc_hdr));
    fprintf(stderr, "  holder scan=%p size=%u old=%d\n", (void *)hc->scan, (unsigned)hc->size, (int)hc->old);
  }
  fprintf(stderr,
    "\n*** SPINEL_GC_VERIFY: collector reached a non-heap/corrupt object ***\n"
    "  obj    = %p\n  header = %p\n"
    "  This pointer is on the GC mark path but is not a registered live GC\n"
    "  allocation -- most likely a raw/aliased pointer (e.g. into a string or\n"
    "  builder buffer) reachable from a root or a scanned field. Invoking its\n"
    "  scan hook would jump through a bogus function pointer.\n",
    obj, (void*)h);
  fflush(stderr);
  fprintf(stderr, "  ->scan = %p   ->size = %zu\n\n",
    (void*)(uintptr_t)h->scan, (size_t)h->size);
  abort();
}
/* Under verify, a fault ON the mark path is the interesting case and the one
   the existing check cannot reach: sp_gc_mark reads the marker byte BEFORE it
   can ask whether the object is registered, so a pointer whose [-1] is
   unreadable (a bare literal at the start of a rodata page, an interior
   pointer into an unmapped neighbour) takes the process down with nothing said.
   Name the root slot the collector was walking, which is what turns an
   unreproducible crash into a variable. Re-raises so the core dump is still
   produced. */
static void sp_gc_fault_report(int sig) {
  static const char hex[] = "0123456789abcdef";
  char buf[256]; size_t o = 0;
  const char *m1 = "\n*** SPINEL_GC_VERIFY: fault on the GC mark path (signal ";
  for (const char *p = m1; *p; p++) buf[o++] = *p;
  buf[o++] = (char)('0' + (sig / 10) % 10); buf[o++] = (char)('0' + sig % 10);
  const char *m2 = ")\n  phase = ";
  for (const char *p = m2; *p; p++) buf[o++] = *p;
  for (const char *p = sp_gc_dbg_phase ? sp_gc_dbg_phase : "?"; *p && o < 200; p++) buf[o++] = *p;
  const char *m3 = "\n  root slot = 0x";
  for (const char *p = m3; *p; p++) buf[o++] = *p;
  uintptr_t v = (uintptr_t)sp_gc_dbg_ctx;
  for (int i = (int)(sizeof(v) * 2) - 1; i >= 0; i--) buf[o++] = hex[(v >> (i * 4)) & 0xf];
  const char *m4 = "\n  The slot's value is the pointer the collector could not read.\n";
  for (const char *p = m4; *p; p++) buf[o++] = *p;
  ssize_t wr = write(2, buf, o); (void)wr;
#ifndef SP_NO_MMAN   /* an MMU-less port's libc has no signal(2) */
  signal(sig, SIG_DFL);
  raise(sig);
#endif
}
__attribute__((constructor)) static void sp_gc_debug_env(void){
#ifndef SP_MULTI_CTX
  const char *v=getenv("SPINEL_GC_VERIFY"); sp_gc_verify=(v&&*v&&*v!='0');
#endif
  /* SP_MULTI_CTX: gc_verify is per-instance, read from the env in
     sp_instance_create (no current ctx exists at process-constructor time). */
  { const char *ph=getenv("SPINEL_GC_PHASES"); sp_gc_ph_on=(ph&&*ph&&*ph!='0'); }
  { const char *fi=getenv("SPINEL_GC_FULL_INTERVAL");
#ifdef SP_MULTI_CTX
    if(fi&&*fi){ int n=atoi(fi); if(n>0&&n<=4096){ sp_gc_full_interval_start=n; sp_gc_full_interval_fixed=1; } } }
#else
    if(fi&&*fi){ int n=atoi(fi); if(n>0&&n<=4096){ sp_gc_full_interval=n; sp_gc_full_interval_fixed=1; } } }
#endif
  { const char *g=getenv("SPINEL_GC_VERIFY_GEN"); sp_gc_verify_gen=(g&&*g&&*g!='0');
    /* The default since 2026-09-12. What made it one: every test program
       clean under the generational verifier with stress on (every allocation
       collects, so every survivor promotes and any holder the barrier missed
       is named), the LangArena suite at 0.94x wall and 0.7x peak RSS end to
       end, and the Rails-shaped application (#4311) green on its minor leg.
       SPINEL_GC_MINOR=0 is the way back: the collector then marks the whole
       heap on every cycle, as it did before the barrier landed. */
    const char *mn=getenv("SPINEL_GC_MINOR"); sp_gc_minor_on=!(mn&&*mn&&*mn=='0');
    const char *ag=getenv("SPINEL_GC_AGE"); if(ag&&*ag) sp_gc_age_on=(*ag!='0');
    if(sp_gc_verify_gen) sp_gc_minor_on=1; }
  /* Read here rather than in sp_alloc_worker_tune, which a single-threaded
     program never calls: the budget policy is not a threads-only question. */
  { const char *ob = getenv("SPINEL_GC_OBJ_BUDGET");
    /* The default GATES the widening on what the last collection actually
       cost, rather than widening always (`walk`) or never (`obj`). Both of
       those are kept, because a measurement wants to be able to pin the
       policy at either end. */
    sp_gc_obj_budget_mode = (ob && strcmp(ob, "obj") == 0)  ? 0
                          : (ob && strcmp(ob, "walk") == 0) ? 1
                          : 2;
    /* PINNED, not floored. SPINEL_GC_THRESHOLD_*_KB sets where the budget
       STARTS and the retune moves it from there, which is right for running a
       program and wrong for asking what the retune itself is responsible for.
       rubys wanted exactly that question answered -- whether a change that
       lowers the rate of string garbage grows the resident set because the
       budget lets it, or for some reason that has nothing to do with the
       budget -- and said they had not run it because they did not know the
       knob. There was none (#4396). With `fixed`, the budget is whatever the
       floor says and stays there, so the two emits differ by the change and
       not by two different pacing histories. */
    sp_gc_obj_budget_fixed = (ob && strcmp(ob, "fixed") == 0);
    const char *sb = getenv("SPINEL_GC_STR_BUDGET");
    sp_gc_str_budget_fixed = (sb && strcmp(sb, "fixed") == 0);
    /* The schedule is the DEFAULT. `size` is the escape hatch back to the gate
       that shipped before it -- a size test re-aimed from its own leftover --
       for anyone the change costs more than it saves. `fixed` pins both the
       cadence and the backstop where the floor put them, as it always did. */
    const char *sm = getenv("SPINEL_GC_STR_MAJOR");
    sp_gc_str_major_fixed = (sm && strcmp(sm, "fixed") == 0);
    sp_gc_str_major_sched = !(sm && strcmp(sm, "size") == 0); }
#ifdef SP_MULTI_CTX
  /* no instance exists yet: ask the environment, as sp_instance_create will */
#ifndef SP_NO_MMAN   /* no signal(2) on an MMU-less port: verify runs without the fault report */
  { const char *v=getenv("SPINEL_GC_VERIFY");
    if (v&&*v&&*v!='0') { signal(SIGSEGV, sp_gc_fault_report); signal(SIGBUS, sp_gc_fault_report); } }
#endif
#else
  if (sp_gc_verify) { signal(SIGSEGV, sp_gc_fault_report); signal(SIGBUS, sp_gc_fault_report); }
#endif
}

/* Tag byte preceding `obj`: 0xfe heap-unmarked -> 0xfc; 0xfc/0xff/0xfd/0xf1/
 * 0xfb skipped; else a real GC object reached through its scan hook. 0xfb is
 * the static header-bearing table (the 1-byte binary substrings): nothing
 * before it is an sp_gc_hdr, so reaching for one and calling its scan hook
 * jumps into the payload byte. */

static double sp_gc_stat_now(void);
#ifdef SP_THREADS
#include <pthread.h>
#if defined(__x86_64__) || defined(__i386__)
#define SP_GC_CPU_RELAX() __builtin_ia32_pause()
#elif defined(__aarch64__)
#define SP_GC_CPU_RELAX() __asm__ __volatile__("yield" ::: "memory")
#else
#define SP_GC_CPU_RELAX() ((void)0)
#endif
/* ---- The parallel mark ----
   The roots are walked by the collector alone (they are few, and the root
   phase has state of its own); the DRAIN, which is the mark's time, runs on
   the collector plus the parked workers the scheduler lends it
   (sp_gc_par_mark_hook). Work moves between markers in chunks: a marker whose
   stack is full spills half of it onto a shared chunk list, and one whose
   stack is empty takes a chunk from it. The drain is over when the list is
   empty and no marker holds work. */
#define SP_GC_MK_CHUNK 16
typedef struct sp_gc_mk_chunk { struct sp_gc_mk_chunk *next; int n; void *obj[SP_GC_MK_CHUNK]; } sp_gc_mk_chunk;
static pthread_mutex_t sp_gc_mk_lock = PTHREAD_MUTEX_INITIALIZER;
static sp_gc_mk_chunk *sp_gc_mk_work = NULL, *sp_gc_mk_free = NULL;
static int sp_gc_mk_nwork = 0;    /* chunks on the work list (read without the lock to avoid taking it for nothing) */
static int sp_gc_mk_busy = 0;     /* markers holding work */
static void sp_gc_mark_spill_n(int out) {
  int i = 0;
  pthread_mutex_lock(&sp_gc_mk_lock);
  while (i < out) {
    sp_gc_mk_chunk *c = sp_gc_mk_free;
    if (c) sp_gc_mk_free = c->next;
    else { pthread_mutex_unlock(&sp_gc_mk_lock); c = (sp_gc_mk_chunk *)malloc(sizeof *c); if (!c) sp_oom_die(); pthread_mutex_lock(&sp_gc_mk_lock); }
    int n = out - i; if (n > SP_GC_MK_CHUNK) n = SP_GC_MK_CHUNK;
    memcpy(c->obj, sp_gc_mark_stack + i, (size_t)n * sizeof(void *)); c->n = n; i += n;
    c->next = sp_gc_mk_work; sp_gc_mk_work = c; __atomic_fetch_add(&sp_gc_mk_nwork, 1, __ATOMIC_RELAXED); sp_gc_ph_mk_spills++;
  }
  pthread_mutex_unlock(&sp_gc_mk_lock);
  memmove(sp_gc_mark_stack, sp_gc_mark_stack + out, (size_t)(sp_gc_mark_top - out) * sizeof(void *));
  sp_gc_mark_top -= out;
}
/* The OLDER half of the stack goes out: what is left on top is what the
   scan that pushed it is about to need, and stays hot on this thread. */
static void sp_gc_mark_spill(void) { sp_gc_mark_spill_n(sp_gc_mark_top / 2); }
/* Before the helpers are woken: everything the root walk pushed goes out
   as chunks, so there is work for them the moment they arrive. A stack that
   only spilled when full (64K entries) never did on a live set of 30K
   objects, and eight helpers spun while the collector marked alone. */
void sp_gc_mark_par_begin(void) { if (sp_gc_mark_top > 0) sp_gc_mark_spill_n(sp_gc_mark_top); }
static int sp_gc_mk_markers = 1;   /* seats the scheduler hands out, plus the collector */
void sp_gc_mark_par_markers(int n) { sp_gc_mk_markers = n; }
static int sp_gc_mark_take(void) {
  if (__atomic_load_n(&sp_gc_mk_nwork, __ATOMIC_RELAXED) == 0) return 0;
  pthread_mutex_lock(&sp_gc_mk_lock);
  sp_gc_mk_chunk *c = sp_gc_mk_work;
  if (c) { sp_gc_mk_work = c->next; __atomic_fetch_sub(&sp_gc_mk_nwork, 1, __ATOMIC_RELAXED); sp_gc_ph_mk_takes++; }
  pthread_mutex_unlock(&sp_gc_mk_lock);
  if (!c) return 0;
  memcpy(sp_gc_mark_stack + sp_gc_mark_top, c->obj, (size_t)c->n * sizeof(void *));
  sp_gc_mark_top += c->n;
  pthread_mutex_lock(&sp_gc_mk_lock);
  c->next = sp_gc_mk_free; sp_gc_mk_free = c;
  pthread_mutex_unlock(&sp_gc_mk_lock);
  return 1;
}
/* One marker's drain, run by the collector and by every helper. Returns when
   the whole mark is done: no chunk to take and nobody holding work. */
void sp_gc_mark_par_run(void) {
  if (!sp_gc_mark_stack) { sp_gc_mark_stack = (void **)malloc(sizeof(void *) * SP_GC_MARK_STACK_MAX); if (!sp_gc_mark_stack) sp_oom_die(); sp_gc_mark_cap = SP_GC_MARK_STACK_MAX; sp_gc_mark_top = 0; }
  __atomic_fetch_add(&sp_gc_mk_busy, 1, __ATOMIC_ACQ_REL);
  for (;;) {
    while (sp_gc_mark_top > 0) {
      /* share while there is little on the list and a lot here: a marker
         that hoards a deep stack leaves the others spinning */
      if (sp_gc_mark_top > 2 * SP_GC_MK_CHUNK && __atomic_load_n(&sp_gc_mk_nwork, __ATOMIC_RELAXED) < sp_gc_mk_markers) sp_gc_mark_spill();
      void *obj = sp_gc_mark_stack[--sp_gc_mark_top];
      sp_gc_hdr *h = (sp_gc_hdr *)((char *)obj - sizeof(sp_gc_hdr));
      if (h->scan) h->scan(obj);
    }
    if (sp_gc_mark_take()) continue;
    __atomic_fetch_sub(&sp_gc_mk_busy, 1, __ATOMIC_ACQ_REL);
    double i0 = (sp_gc_ph_on && sp_gc_mk_is_collector) ? sp_gc_stat_now() : 0;
    for (;;) {
      if (__atomic_load_n(&sp_gc_mk_nwork, __ATOMIC_ACQUIRE) > 0) break;
      if (__atomic_load_n(&sp_gc_mk_busy, __ATOMIC_ACQUIRE) == 0) { if (i0) sp_gc_ph_mk_idle += sp_gc_stat_now() - i0; sp_gc_mkl_fold(); return; }
      SP_GC_CPU_RELAX();
    }
    if (i0) sp_gc_ph_mk_idle += sp_gc_stat_now() - i0;
    __atomic_fetch_add(&sp_gc_mk_busy, 1, __ATOMIC_ACQ_REL);
  }
}
void (*sp_gc_par_mark_hook)(void) = NULL;
void sp_gc_hdr_flags_check(void) {
  sp_gc_hdr h; memset(&h, 0, sizeof h);
  h.marked = 0x5a5a5a5; h.old = 1; h.aged = 1;
  unsigned w = *sp_gc_hdr_flags(&h);
  /* six words on either pointer width: four pointers, the size, the flags */
  if (w != (0x5a5a5a5u | SP_GC_FL_OLD | SP_GC_FL_AGED) || sizeof(sp_gc_hdr) != 6 * sizeof(void *)) {
    fprintf(stderr, "spinel: sp_gc_hdr flag layout is not the one the parallel mark assumes (word %08x)\n", w);
    abort();
  }
}
#endif
/* The drain: on the collector alone, or on the collector and the helpers the
   scheduler lends it. The verifiers and probes read state the helpers do not
   keep, so they drain serially. */
static void sp_gc_mark_drain_all(void) {
#ifdef SP_THREADS
  if (sp_gc_par_mark_hook && sp_gc_par_mark_on && !sp_gc_verify && !sp_gc_verify_gen && !sp_gc_verify_probe_on && !sp_gc_young_probe_on && !sp_gc_age_on) {
    sp_gc_par_mark = 1; sp_gc_mk_is_collector = 1;
    sp_gc_par_mark_hook();   /* lends helpers, runs sp_gc_mark_par_run itself, joins them */
    sp_gc_mkl_fold();        /* the collector's share, while par_mark still says which it is */
    sp_gc_par_mark = 0; sp_gc_mk_is_collector = 0;
    return;
  }
#endif
  sp_gc_mark_drain();
}
/* What this thread's marking counted on the string side: bytes reached, and
   of those the young ones the mark promoted (the bitmap strings; a string
   too large for the slab is counted by its list sweep). */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static SP_TLS size_t sp_gc_mkl_str = 0, sp_gc_mkl_str_young = 0;
size_t sp_gc_mk_str_bytes = 0, sp_gc_mk_str_young_bytes = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* and the slab objects the mark promoted (their bytes; the list sweep counts
   its own promotions as it splices them) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static SP_TLS size_t sp_gc_mkl_promo = 0;
size_t sp_gc_mk_promo_bytes = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* A heap string the mark reached. A slab string's mark is a bit in its
   chunk, and the bit is also the "already marked" test: the marker byte
   stays 0xfe, since nothing resets a byte the sweep does not touch. A
   string that fell back to malloc keeps the byte protocol (0xfe -> 0xfc,
   reset by the list sweep that frees or promotes it). */
void sp_gc_mark_str(const char *s) {
  sp_str_hdr *h = ((sp_str_hdr *)(s - 1)) - 1;
  if (sp_slab_owns(h)) {
    int wy;
    /* SPINEL_GC_VERIFY: a string slot the sweep has freed is nobody's to
       mark; marking it would set generation bits on a free slot */
    if (__builtin_expect(sp_gc_verify, 0) && !sp_slab_is_live(h)) { fprintf(stderr, "*** SPINEL_GC_VERIFY: the mark reached a freed heap string %p\n", (const void *)s); sp_gc_verify_fail((void *)s, (sp_gc_hdr *)h); }
    if (!sp_slab_mark(h, 0, &wy)) return;
    size_t sz = h->size & 0x3FFFFFFFu;
    sp_gc_mkl_str += sz;
    if (wy) sp_gc_mkl_str_young += sz;
    return;
  }
#ifdef SP_THREADS
  __atomic_store_n((unsigned char *)s - 1, (unsigned char)0xfc, __ATOMIC_RELAXED);   /* several markers may set it; a plain byte store, spelled so TSan reads it as intended */
#else
  ((char *)s)[-1] = (char)0xfc;
#endif
}
void sp_gc_mark(void*obj){if(!obj)return;unsigned char pm=((unsigned char*)obj)[-1];if(pm==0xfe){sp_gc_mark_str((const char*)obj);return;}
  if(pm==0xfc||pm==0xff||pm==0xfd||pm==0xf1||pm==0xfb)return;sp_gc_hdr*h=(sp_gc_hdr*)((char*)obj-sizeof(sp_gc_hdr));if(sp_gc_verify&&!sp_gc_obj_registered(h))sp_gc_verify_fail(obj,h);if(sp_gc_verify_probe_on){if(!h->old&&h->marked==sp_gc_verify_probe)sp_gc_verify_probe_hit=1;return;}if(sp_gc_young_probe_on){if(!h->old)sp_gc_young_probe_hit=1;return;}if(sp_gc_root_phase&&!h->old)h->aged=1;
  int was_young=0;
#ifdef SP_THREADS
  if(sp_gc_par_mark){
    /* Several threads mark at once: the stamp is claimed with an exchange
       on the flag word, so an object is counted and scanned by exactly one
       of them. Nothing else writes the word under the barrier. */
    unsigned *w=sp_gc_hdr_flags(h);unsigned o=__atomic_load_n(w,__ATOMIC_RELAXED);
    for(;;){
      if((o&SP_GC_FL_MARK_MASK)==sp_gc_mark_gen)return;
      if(sp_gc_minor&&(o&SP_GC_FL_OLD))return;
      unsigned nw=(o&~SP_GC_FL_MARK_MASK)|sp_gc_mark_gen;
      if(sp_gc_conc_promote&&!(o&SP_GC_FL_OLD))nw|=SP_GC_FL_OLD;
      if(__atomic_compare_exchange_n(w,&o,nw,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))break;
    }
    sp_gc_mkl_marked++;sp_gc_mkl_bytes+=h->size;if(!(o&SP_GC_FL_OLD)){sp_gc_mkl_young+=h->size;was_young=1;}
  }
  else
#endif
  {if(h->marked==sp_gc_mark_gen)return;if(sp_gc_minor&&h->old)return;h->marked=sp_gc_mark_gen;sp_gc_mkl_marked++;sp_gc_mkl_bytes+=h->size;if(!h->old){sp_gc_mkl_young+=h->size;was_young=1;if(sp_gc_conc_promote)h->old=1;/* promoted here, under the barrier: the concurrent sweep will not write the bit */}}
  /* A slab object: its generation is a bit in its chunk, and the mark is what
     promotes it -- the sweep reads the bitmaps and never this header. A
     first-time survivor under aging stays young (the sweep carries it into
     the next epoch) and is aged for the next time; the header's `old` bit
     follows the promotion so the write barrier sees it. */
  if(was_young&&sp_slab_owns(h)){
    /* SPINEL_GC_AGE keeps a first-time survivor young on the lists; a slab
       object promotes at once, since the remembered-set repair aging needs
       (the young probe over what this cycle promoted) walks the old list */
    int keep_young=0;
    int wy;
    sp_slab_mark(h,keep_young,&wy);
    sp_gc_mkl_promo+=h->size;
    {
#ifdef SP_THREADS
      if(sp_gc_par_mark)__atomic_fetch_or(sp_gc_hdr_flags(h),SP_GC_FL_OLD,__ATOMIC_RELAXED);
      else
#endif
      h->old=1;
    }
  }
  else if(sp_slab_owns(h)){int wy;sp_slab_mark(h,0,&wy);}
  if(h->scan){if(sp_gc_mark_stack&&sp_gc_mark_top>=sp_gc_mark_cap){
#ifdef SP_THREADS
    if(sp_gc_par_mark){sp_gc_mark_spill();}
    else
#endif
    if(sp_gc_mark_cap<SP_GC_MARK_STACK_LIMIT){int nc=sp_gc_mark_cap*2;void**ns=(void**)SP_GC_MARK_REALLOC(sp_gc_mark_stack,sizeof(void*)*(size_t)nc);if(ns){sp_gc_mark_stack=ns;sp_gc_mark_cap=nc;}}}
if(sp_gc_mark_stack&&sp_gc_mark_top<sp_gc_mark_cap){sp_gc_mark_stack[sp_gc_mark_top++]=obj;}
else{h->scan(obj);}}}
/* The thread's counts, folded in. The collector folds its own at the end of
   the mark; a helper folds when its drain ends. */
static void sp_gc_mkl_fold(void){
#ifdef SP_THREADS
  if(sp_gc_par_mark&&!sp_gc_mk_is_collector)__atomic_fetch_add(&sp_gc_ph_mk_by_helpers,sp_gc_mkl_marked,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_ct_marked,sp_gc_mkl_marked,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_mk_bytes,sp_gc_mkl_bytes,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_mk_young_bytes,sp_gc_mkl_young,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_mk_str_bytes,sp_gc_mkl_str,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_mk_str_young_bytes,sp_gc_mkl_str_young,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_mk_promo_bytes,sp_gc_mkl_promo,__ATOMIC_RELAXED);
#else
  sp_gc_ct_marked+=sp_gc_mkl_marked;sp_gc_mk_bytes+=sp_gc_mkl_bytes;sp_gc_mk_young_bytes+=sp_gc_mkl_young;
  sp_gc_mk_str_bytes+=sp_gc_mkl_str;sp_gc_mk_str_young_bytes+=sp_gc_mkl_str_young;sp_gc_mk_promo_bytes+=sp_gc_mkl_promo;
#endif
  sp_gc_mkl_marked=0;sp_gc_mkl_bytes=0;sp_gc_mkl_young=0;sp_gc_mkl_str=0;sp_gc_mkl_str_young=0;sp_gc_mkl_promo=0;
}

void sp_gc_mark_drain(void){
  while(sp_gc_mark_top>0){void*obj=sp_gc_mark_stack[--sp_gc_mark_top];
    sp_gc_hdr*h=(sp_gc_hdr*)((char*)obj-sizeof(sp_gc_hdr));
    /* Name the object being scanned, not the root group it came from: a bad
       reference found here is a field of THIS object, and the group label on
       its own says nothing about which. */
    if(sp_gc_verify){sp_gc_dbg_phase="scan";sp_gc_dbg_ctx=obj;}
    if(h->scan)h->scan(obj);}
}
/* sp_gc_stat_now is defined with the rest of the phase clock, below: the split
   here is the same measurement, taken one level down. */
static double sp_gc_stat_now(void);
/* Close the mark sub-phase that ended here. Same shape and same cost as
   SP_GC_PH in sp_gc_collect: off by default, one not-taken branch per
   boundary, four of them per collection. */
#define SP_GC_MK_PH(bucket)   do { if (sp_gc_ph_on) { double _t = sp_gc_stat_now(); (bucket) += _t - mk_t; mk_t = _t; } } while (0)

void sp_gc_mark_all(void){if(!sp_gc_mark_stack){sp_gc_mark_stack=(void**)malloc(sizeof(void*)*SP_GC_MARK_STACK_MAX);if(sp_gc_mark_stack)sp_gc_mark_cap=SP_GC_MARK_STACK_MAX;}sp_gc_mark_top=0;if(sp_gc_verify)sp_gc_verify_snapshot();int vd=sp_gc_verify;
  double mk_t = sp_gc_ph_on ? sp_gc_stat_now() : 0.0;
  /* An object a C root names directly is one some runtime function holds in
     a local across this collection, and such a function may have recorded
     its holder BEFORE the allocation that collected (the barrier-then-store
     shape #4376 describes): the record is cleared by this cycle, and the
     store that follows would go unrecorded. Promoting every survivor covered
     that: an old value into an old holder needs no record. Aging survivors
     reopens it, so what a root names directly is promoted on this survival
     (aged is set before the mark); the rest of the graph ages. The same for
     a suspended fiber's roots, which are runtime locals too. */
  sp_gc_root_phase=1;
  for(int i=0;i<sp_gc_nroots;i++){void**e=sp_gc_root_at(i);if(vd){sp_gc_dbg_phase="root";sp_gc_dbg_ctx=(void*)e;}if((uintptr_t)e&(uintptr_t)3){sp_gc_mark_root_entry(e);}
else{void*obj=*e;if(obj)sp_gc_mark(obj);}}
  SP_GC_MK_PH(sp_gc_ph_mk_roots);
  if(vd)sp_gc_dbg_phase="fibers";if(sp_gc_mark_suspended_fibers_hook)sp_gc_mark_suspended_fibers_hook();
  sp_gc_root_phase=0;
  SP_GC_MK_PH(sp_gc_ph_mk_fibers);
  if(vd)sp_gc_dbg_phase="globals";if(sp_gc_mark_globals_hook)sp_gc_mark_globals_hook();
  SP_GC_MK_PH(sp_gc_ph_mk_globals);
  sp_gc_mark_drain_all();
  sp_gc_mkl_fold();
  SP_GC_MK_PH(sp_gc_ph_mk_scan);
  if(vd){sp_gc_dbg_phase="?";sp_gc_dbg_ctx=NULL;}}

#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
unsigned sp_gc_mark_gen = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* Set for the duration of a minor mark: an object already promoted is not
   walked, because the sweep does not free the old list on a minor cycle and
   the remembered set carries the old->young references the walk would miss. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_minor = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
int sp_gc_minor_on = 1;   /* the constructor above reads SPINEL_GC_MINOR=0 */
int sp_gc_verify_gen = 0;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_verify_gen_fail = 0;
int sp_gc_verify_probe_on = 0, sp_gc_verify_probe_hit = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* Armed after a minor's sweep to ask each remembered holder whether it still
   reaches a young object (one the sweep kept young for a second look): with the
   probe on, sp_gc_mark records the answer and marks nothing. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_young_probe_on = 0, sp_gc_young_probe_hit = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* This cycle's sweeps age survivors instead of promoting every one: a minor
   whose remembered set is intact. SPINEL_GC_AGE=0 turns aging off. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_age_survivors = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
int sp_gc_age_on = 0;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
size_t sp_gc_young_kept_bytes = 0;
size_t sp_gc_npromoted = 0;
unsigned sp_gc_verify_probe = 0;
void *sp_gc_remembered[SP_GC_REMEMBERED_MAX];
int sp_gc_nremembered = 0;
int sp_gc_rem_overflow = 0;
void *sp_gc_pinned[SP_GC_PINNED_MAX];
int sp_gc_npinned = 0;
int sp_gc_pin_overflow = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

/* Sticky remembered entry; see the comment on sp_gc_pin_remembered in sp_gc.h
   for why the by-reference String ABI needs one. Same tag protocol as
   sp_gc_wb_slow: the byte in front says whether there is a header at all, so
   a literal, a frozen string or the static root fiber is refused rather than
   dereferenced. The caller only ever passes a heap cell or an object it
   named, so this is a guard and not a filter.

   `pinned` makes it once per object rather than once per lending call: the
   set is one entry per distinct holder, not per call, which is what keeps it
   small enough to walk on every minor cycle. Overflow degrades the same way
   the barrier's does -- to a full mark, which needs no remembered set at
   all -- rather than silently dropping a holder. */
void sp_gc_pin_remembered_slow(void *obj) {
  if (!obj) return;
  { unsigned char pm = ((unsigned char *)obj)[-1];
    if (pm == 0xfd || pm == 0xff || pm == 0xf1 || pm == 0xf0 ||
        pm == 0xfe || pm == 0xfc || pm == 0xfb) return; }
  sp_gc_hdr *h = (sp_gc_hdr *)obj - 1;
  if (h->pinned) return;
  /* The slot is claimed BEFORE the bit goes up, which is the opposite order to
     sp_gc_wb_slow's, and for the opposite reason. There the bit is the record
     and the array is the index, so a bit with no entry is self-healing. Here
     the ARRAY is the record and the bit is only a dedupe, so a bit with no
     entry would refuse the object forever -- it would never be pinned and
     every young string it later holds would be invisible to a minor mark. A
     refused push therefore leaves the bit down and the object is offered
     again on its next lending, by which time a compaction may have made
     room. Two threads racing here can both win a slot; a duplicate entry is
     scanned twice and costs nothing. */
#ifdef SP_THREADS
  { int idx = __atomic_fetch_add(&sp_gc_npinned, 1, __ATOMIC_RELAXED);
    if (idx < SP_GC_PINNED_MAX) { sp_gc_pinned[idx] = obj; h->pinned = 1; }
    else { __atomic_store_n(&sp_gc_pin_overflow, 1, __ATOMIC_RELAXED);
           __atomic_store_n(&sp_gc_npinned, SP_GC_PINNED_MAX, __ATOMIC_RELAXED); } }
#else
  if (sp_gc_npinned < SP_GC_PINNED_MAX) { sp_gc_pinned[sp_gc_npinned++] = obj; h->pinned = 1; }
  else sp_gc_pin_overflow = 1;
#endif
}

void sp_gc_wb_slow(void *obj) {
  if (!obj) return;
  /* Same tag-byte protocol sp_gc_mark uses: the byte in front says whether
     there is a header to read at all. The root fiber is a static whose guard
     byte is 0xfd, so reaching past it for `old` walks off the end of a global
     (ASAN: global-buffer-overflow, and a wandering segfault without it), and a
     literal or a frozen string is not a GC allocation either. */
  { unsigned char pm = ((unsigned char *)obj)[-1];
    if (pm == 0xfd || pm == 0xff || pm == 0xf1 || pm == 0xf0 ||
        pm == 0xfe || pm == 0xfc || pm == 0xfb) return; }
  sp_gc_hdr *h = (sp_gc_hdr *)obj - 1;
  if (!h->old || h->dirty) return;
  /* The bit goes up before the entry goes in, so between here and the push an
     object carries it with nothing naming it, and the collector's clear reads
     the array. Two things keep that safe, and the second is the one that
     actually covers every case:

     - No collection reaches this window from another thread. Nothing below
       allocates or polls a safepoint, so a worker cannot park inside this
       function, and a collection waits for every other worker to park.
     - A collection that begins ON THIS THREAD, mid-window, is harmless because
       the bit goes up FIRST. That collection leaves the bit set with no entry;
       the push we are about to make lands in the next epoch's array, and the
       clear after that finds it. It is self-healing, where pushing first would
       not be. This is not hypothetical: an async `Signal.trap` handler runs
       Ruby in signal context (sp_sig_c_handler -> sp_proc_call), on whatever
       worker the OS picks, and the allocation it makes can collect right here
       -- on a thread that does not park, because the collector never parks
       itself. The single-threaded build has always cleared through the array
       and has always had that same path.

     Add an allocation or a safepoint poll below and the first argument goes;
     reverse the two writes and the second one goes with it. */
  h->dirty = 1;
#ifdef SP_THREADS
  /* Mutators run this concurrently, so the slot has to be claimed atomically:
     a plain `n++` lets two workers take the same index and one of the two
     holders is silently dropped from the set -- a missing barrier with all the
     barriers in place. The dirty bit needs no such care: only a mutator writes
     it, only ever to 1, and the collector reads and clears it under
     stop-the-world. */
  { int idx = __atomic_fetch_add(&sp_gc_nremembered, 1, __ATOMIC_RELAXED);
    if (idx < SP_GC_REMEMBERED_MAX) sp_gc_remembered[idx] = obj;
    else { __atomic_store_n(&sp_gc_rem_overflow, 1, __ATOMIC_RELAXED);
           __atomic_store_n(&sp_gc_nremembered, SP_GC_REMEMBERED_MAX, __ATOMIC_RELAXED); } }
#else
  if (sp_gc_nremembered < SP_GC_REMEMBERED_MAX) sp_gc_remembered[sp_gc_nremembered++] = obj;
  else sp_gc_rem_overflow = 1;
#endif
}
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
int sp_gc_str_minor_only = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
/* Object-threshold retune, installed by sp_alloc.c. Running it INSIDE every
   collection (not only on the object-triggered wrapper) keeps the trigger
   tracking the live size whichever heap initiated the collect; the old
   split retunes left one threshold stale and re-triggered immediately. */
void (*sp_gc_obj_retune_hook)(size_t before) = NULL;
/* Minor sweep of one young list (under stop-the-world): free/recycle the dead,
   promote survivors into the shared old heap, accumulating survivor bytes into
   sp_gc_bytes and sp_gc_old_bytes (both pre-seeded by the caller). */
/* Counts, not bytes. The sweep's cost is per SLOT -- a header touch and a
   free -- and the mark's is per LIVE OBJECT. Sized in bytes the two are not
   comparable across programs: a cache of large strings and a churn of small
   arrays can hold the same megabytes with slot counts orders of magnitude
   apart, which is why a per-byte cost ratio measured on one workload did not
   carry to another (#4384). Counted, the coefficients are properties of this
   code rather than of a program's allocation sizes. */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
size_t sp_gc_ct_swept = 0, sp_gc_ct_marked = 0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
static void sp_gc_sweep_young(sp_gc_hdr **pp){
  size_t kept=0, promoted=0;
  while(*pp){sp_gc_hdr*h=*pp;SP_GC_CTR_ADD(sp_gc_ct_swept,1);if(h->marked!=sp_gc_mark_gen){*pp=h->next;if(h->recycle){h->recycle(h);}
  else{if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));sp_slab_free(h);}}
  else if(sp_gc_age_survivors&&!h->aged){h->aged=1;kept+=h->size;pp=&h->next;}   /* first survival: stays young */
  else{*pp=h->next;h->next=sp_gc_old_heap;sp_gc_old_heap=h;h->old=1;sp_gc_old_bytes+=h->size;sp_gc_bytes+=h->size;promoted++;}}
  sp_gc_young_kept_bytes+=kept;
  sp_gc_npromoted+=promoted;
}
#ifdef SP_THREADS
/* One worker's young list, swept BY THAT WORKER while it is parked at the
   stop-the-world barrier. Two things make this worth the barrier phase it
   needs. It is parallel -- the sweep was 93% of the stopped time and every
   other worker idled through it. And the frees go back to the arena the
   allocation came from: a single collector thread freeing eight workers'
   objects takes eight different arena locks and touches eight cold sets of
   metadata, which is why the serial sweep got MORE expensive as workers were
   added (51 ms at one worker, 188 ms at eight, for the same allocations).

   Survivors are collected into a caller-owned local list rather than pushed
   straight onto the shared old heap: that is the one part that cannot be
   concurrent, so it becomes an O(workers) splice the collector does after. */
/* The list form, which the concurrent sweep (sp_sched.c) runs beside the
   mutators on a young list detached from its worker at the barrier. What
   differs from the stop-the-world form: the survivor's `old` bit is not
   written, since the mark set it (sp_gc_mark, under the barrier), so a store
   into the survivor after the world resumed was already recorded, and the
   bit shares a word with the one the barrier writes. A pooled object IS
   pushed onto its pool here, beside the mutators popping from it: the pools
   are Treiber stacks, and the ABA a push could hand a popper needs the
   popped object to die and come back between the popper's load and its
   exchange -- a whole collection, which cannot complete while that popper
   has not parked. Chaining the dead for the collector to push at the next
   barrier instead cost the barrier more than the sweep it had shed (8.8s of
   13.3s on a server run). */
void sp_gc_sweep_list(sp_gc_hdr **pp, int conc, sp_gc_hdr **out_head, sp_gc_hdr **out_tail, size_t *out_bytes) {
  sp_gc_hdr *head = NULL, *tail = NULL;
  size_t live = 0;
  /* Counted into a LOCAL and published once. Every worker runs this at the
     same time, so incrementing the global per slot was a data race (found by
     scripts/tsan-run.sh) -- and the atomic that would fix it in place is a
     locked add per slot on one shared cache line, which is the same
     ping-pong the parallel sweep exists to avoid. */
  size_t swept = 0, kept = 0, promoted = 0;
  while (*pp) {
    sp_gc_hdr *h = *pp;
    __builtin_prefetch(h->next);
    swept++;
    if (h->marked != sp_gc_mark_gen) {
      *pp = h->next;
      if (h->recycle) h->recycle(h);
      else { if (h->finalize) h->finalize((char *)h + sizeof(sp_gc_hdr)); sp_slab_free(h); }
    }
    else if (!conc && sp_gc_age_survivors && !h->aged) {
      h->aged = 1; kept += h->size; pp = &h->next;   /* first survival: stays young */
    }
    else {
      *pp = h->next;
      h->next = head; head = h;
      if (!tail) tail = h;
      if (!conc) h->old = 1;      /* survivor: joins the old list (see sp_gc_wb) */
      live += h->size; promoted++;
    }
  }
  SP_GC_CTR_ADD(sp_gc_ct_swept, swept);
  SP_GC_CTR_ADD(sp_gc_young_kept_bytes, kept);
  SP_GC_CTR_ADD(sp_gc_npromoted, promoted);
  *out_head = head; *out_tail = tail; *out_bytes = live;
}
void sp_gc_sweep_slot(int wid, sp_gc_hdr **out_head, sp_gc_hdr **out_tail, size_t *out_bytes) {
  sp_gc_sweep_list(&sp_gc_wslot[wid].young, 0, out_head, out_tail, out_bytes);
}
/* The old list's full-cycle sweep in list form, for the concurrent driver:
   frees the unmarked and answers the bytes that stayed. The dirty bits are
   left alone (the barrier writes that word from the mutators; the collector
   clears the recorded ones under the barrier instead). */
void sp_gc_sweep_old_list(sp_gc_hdr **pp, size_t *out_live, sp_gc_hdr **out_tail) {
  size_t live = 0, swept = 0; sp_gc_hdr *tail = NULL;
  while (*pp) {
    sp_gc_hdr *h = *pp;
    __builtin_prefetch(h->next);
    swept++;
    if (h->marked != sp_gc_mark_gen) {
      *pp = h->next;
      if (h->recycle) h->recycle(h);
      else { if (h->finalize) h->finalize((char *)h + sizeof(sp_gc_hdr)); sp_slab_free(h); }
    }
    else { live += h->size; tail = h; pp = &h->next; }
  }
  SP_GC_CTR_ADD(sp_gc_ct_swept, swept);
  *out_live = live; *out_tail = tail;   /* the tail, so the reattach is a splice and not a walk */
}
/* Installed by the scheduler when it can drive the parked workers; NULL means
   nobody is parked to help and the collector sweeps every slot itself. */
void (*sp_gc_par_sweep_hook)(void) = NULL;

void sp_gc_promote_slot(sp_gc_hdr *head, sp_gc_hdr *tail, size_t bytes) {
  if (!head) return;
  tail->next = sp_gc_old_heap;
  sp_gc_old_heap = head;
  sp_gc_old_bytes += bytes;
  sp_gc_bytes += bytes;
}
sp_gc_hdr *sp_gc_old_detach(void) { sp_gc_hdr *h = sp_gc_old_heap; sp_gc_old_heap = NULL; return h; }
void sp_gc_old_attach(sp_gc_hdr *head, sp_gc_hdr *tail) {
  if (!head) return;
  if (!tail) { tail = head; while (tail->next) tail = tail->next; }
  tail->next = sp_gc_old_heap;
  sp_gc_old_heap = head;
}
#endif
/* ---- the slab's sweep: one worker's chunks, by their bitmaps ---- */
/* a dead object with a finalizer or a recycler (lib/sp_slab.c calls this for
   each): 1 when the slot is freed, 0 when the recycler kept the header
   (parked on its pool; the pool frees it itself when over the cap, and the
   slot's bits say which happened) */
static int sp_gc_die_cb(void *hp){
  sp_gc_hdr *h=(sp_gc_hdr*)hp;
  if(sp_gc_verify&&(h->size<sizeof(sp_gc_hdr)||h->size>(1u<<30)||((uintptr_t)h->recycle&&(uintptr_t)h->recycle<4096)||((uintptr_t)h->finalize&&(uintptr_t)h->finalize<4096))){
    fprintf(stderr,"*** SPINEL_GC_VERIFY: a dead slot with a finalizer bit is no object: hdr=%p size=%zu recycle=%p finalize=%p scan=%p\n",(void*)h,(size_t)h->size,(void*)h->recycle,(void*)h->finalize,(void*)h->scan);
    sp_slab_describe(h); abort();
  }
  if(h->recycle){
    /* parked before the pool sees it: another thread may pop and relive it
       the moment it is pushed, and parking after that would undo the relive.
       An over-cap header the recycler frees itself, which this thread's
       free count shows. */
    unsigned long f0=sp_slab_frees;
    h->recycle(h);   /* the sweep pinned the slot before calling (sp_slab_sweep_worker) */
    return sp_slab_frees!=f0;
  }
  if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));
  return 1;
}
static void sp_gc_clear_dirty_cb(void *hp,void *arg){(void)arg;((sp_gc_hdr*)hp)->dirty=0;}
static void sp_gc_stamp_reset_cb(void *hp,void *arg){(void)arg;((sp_gc_hdr*)hp)->marked=0;}
static void sp_gc_rem_invariant_cb(void *hp,void *arg){
  sp_gc_hdr *h=(sp_gc_hdr*)hp; void *o=(char*)h+sizeof(sp_gc_hdr); int listed=0;
  for(int ri=0;ri<sp_gc_nremembered;ri++) if(sp_gc_remembered[ri]==o){listed=1;break;}
  if(!!h->dirty!=listed){
    fprintf(stderr,"spinel: GC remembered-set invariant broken: obj=%p scan=%p dirty=%d listed=%d full=%d age=%d cycle=%d\n",
            o,(void*)h->scan,(int)h->dirty,listed,*(int*)arg,sp_gc_age_survivors,sp_gc_cycle);
    fprintf(stderr,"  size=%zu old=%d marked=%u gen=%u fin=%p\n",h->size,(int)h->old,(unsigned)h->marked,(unsigned)sp_gc_mark_gen,(void*)h->finalize);
    sp_slab_describe(hp); sp_slab_history(hp);
    abort();
  }
}
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static int sp_gc_sweep_full_now=0;   /* the cycle the stop-the-world sweep tasks are for */
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
size_t sp_gc_ph_slab_freed_obj=0, sp_gc_ph_slab_freed_str=0;
/* bytes of headers the pools hold, summed over the sweep of one cycle and
   folded into the live total the budget is sized from (sp_gc_parked_take) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static size_t sp_gc_parked_acc=0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
size_t sp_gc_parked_take(void){ size_t v; 
#ifdef SP_THREADS
  v=__atomic_exchange_n(&sp_gc_parked_acc,0,__ATOMIC_RELAXED);
#else
  v=sp_gc_parked_acc; sp_gc_parked_acc=0;
#endif
  return v; }
void sp_gc_sweep_chunks(int wid,int full){
  sp_slab_sweep_stats st;
  sp_slab_sweep_worker(wid,full,0,sp_gc_die_cb,&st);
  SP_GC_CTR_ADD(sp_gc_ct_swept,st.swept);
#ifdef SP_THREADS
  __atomic_fetch_add(&sp_gc_ph_slab_freed_obj,st.freed_obj,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_ph_slab_freed_str,st.freed_str,__ATOMIC_RELAXED);
  __atomic_fetch_add(&sp_gc_parked_acc,st.parked,__ATOMIC_RELAXED);
#else
  sp_gc_ph_slab_freed_obj+=st.freed_obj; sp_gc_ph_slab_freed_str+=st.freed_str; sp_gc_parked_acc+=st.parked;
#endif
}
/* the form the stop-the-world parallel sweep (sp_sched.c) runs per slot */
void sp_gc_sweep_chunks_slot(int wid){ sp_gc_sweep_chunks(wid,sp_gc_sweep_full_now); }
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
static sp_gc_hdr **sp_gc_vg_cand; static size_t sp_gc_vg_n, sp_gc_vg_cap; static unsigned sp_gc_vg_gen;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
static void sp_gc_vg_cand_cb(void *hp,void *arg){
  (void)arg; sp_gc_hdr *h=(sp_gc_hdr*)hp;
  if(!sp_gc_vg_cand||h->old||h->marked==sp_gc_vg_gen)return;
  if(sp_gc_vg_n==sp_gc_vg_cap){sp_gc_vg_cap*=2;sp_gc_vg_cand=(sp_gc_hdr**)realloc(sp_gc_vg_cand,sizeof(sp_gc_hdr*)*sp_gc_vg_cap);if(!sp_gc_vg_cand)return;}
  sp_gc_vg_cand[sp_gc_vg_n++]=h;
}
/* The generational check, out of line: it is off unless SPINEL_GC_VERIFY_GEN
   is set, and inline it added 1.8KB to the middle of the collector -- code
   the common cycle walks past on its way to the sweep. */
static SP_NOINLINE void sp_gc_verify_gen_run(void) {
    /* Snapshot the young objects the minor did NOT reach, then mark whole-heap
       and see which of them the full mark does: each one is held only through
       an old object the barrier failed to record. Without the snapshot the
       full mark's own stamps make the two indistinguishable. */
    unsigned minor_gen = sp_gc_mark_gen;
    sp_str_verify_begin();
    size_t cap = 4096, n = 0;
    sp_gc_hdr **cand = (sp_gc_hdr **)malloc(sizeof(sp_gc_hdr *) * cap);
    if (cand) {
      /* the slab's young objects first, then the lists' */
      sp_gc_vg_cand=cand; sp_gc_vg_n=0; sp_gc_vg_cap=cap; sp_gc_vg_gen=minor_gen;
      sp_slab_each_object(1,0,sp_gc_vg_cand_cb,NULL);
      cand=sp_gc_vg_cand; n=sp_gc_vg_n; cap=sp_gc_vg_cap;
#ifdef SP_THREADS
      { int w=sp_active_workers; if(w<1)w=1; if(w>SP_MAX_WORKERS)w=SP_MAX_WORKERS;
        for(int i=0;i<w;i++)
          for(sp_gc_hdr*h=sp_gc_wslot[i].young;h;h=h->next)
            if(!h->old && h->marked!=minor_gen){
              if(n==cap){cap*=2;cand=(sp_gc_hdr**)realloc(cand,sizeof(sp_gc_hdr*)*cap);if(!cand)break;}
              cand[n++]=h; } }
#else
      for(sp_gc_hdr*h=sp_gc_heap;h;h=h->next)
        if(!h->old && h->marked!=minor_gen){
          if(n==cap){cap*=2;cand=(sp_gc_hdr**)realloc(cand,sizeof(sp_gc_hdr*)*cap);if(!cand)break;}
          cand[n++]=h; }
#endif
    }
    sp_gc_mark_gen = (sp_gc_mark_gen + 1) & 0x7ffffffu;
    if(!sp_gc_mark_gen) sp_gc_mark_gen = 1;
    sp_gc_mark_all();
    /* The sweep that follows uses THIS mark, so it has to keep everything the
       minor's would have kept, or the check changes what the program sees. A
       minor takes every recorded holder as a root -- a dead old object on the
       set still keeps its young referents alive until a full cycle -- and the
       whole-heap walk above does not. With survivors now aged rather than
       promoted, a referent the precise walk dropped stayed reachable from its
       (dead, still recorded) holder, which the next minor walked into freed
       memory. Walking the set here keeps the two marks equivalent; it hides
       no miss, since a candidate reached through a RECORDED holder was reached
       by the minor and is not a candidate. */
    for(int ri=0;ri<sp_gc_nremembered;ri++){
      sp_gc_hdr *rh=(sp_gc_hdr*)sp_gc_remembered[ri]-1;
      if(rh->scan) rh->scan(sp_gc_remembered[ri]);
    }
    for(int pi=0;pi<sp_gc_npinned;pi++){
      sp_gc_hdr *ph=(sp_gc_hdr*)sp_gc_pinned[pi]-1;
      if(ph->scan) ph->scan(sp_gc_pinned[pi]);
    }
    sp_gc_mark_drain();
    size_t str_leaked = sp_str_verify_end();
    if(str_leaked){
      fprintf(stderr,"spinel: GC generational check: %zu young STRING(s) reachable only "
                     "through an old object the barrier did not record\n", str_leaked);
      for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next){
        if(h->dirty||!h->scan) continue;
        sp_str_verify_probe_arm();
        sp_gc_verify_probe=sp_gc_mark_gen; sp_gc_verify_probe_on=1;
        h->scan((char*)h+sizeof(sp_gc_hdr));
        sp_gc_verify_probe_on=0;
        if(sp_str_verify_probe_hit()) fprintf(stderr,"spinel:   string holder scan=%p\n",(void*)h->scan);
      }
      sp_str_verify_probe_done();
      sp_gc_verify_gen_fail = 1;
    }
    size_t leaked = 0;
    for(size_t i=0;cand&&i<n;i++) if(cand[i]->marked==sp_gc_mark_gen) leaked++;
    if(leaked){
      fprintf(stderr,"spinel: GC generational check: %zu young object(s) reachable only "
                     "through an old one the barrier did not record\n", leaked);
      /* Name the holders: an old object reaching one of the LEAKED objects is
         where the missing barrier is, and its scan function names the type.

         The probe has to test membership of the leaked set, not "young and
         marked". Every live young object carries the full mark's stamp by the
         time this runs, so the looser test named every old object that holds
         any young one at all -- which, in a program whose long-lived objects
         point at fresh ones, is all of them, on every collection. The count
         above was always right; the names were noise. */
      /* Leave the full mark's stamp on the leaked objects ALONE and take it
         off every other young one, so the probe's "young and stamped" test
         means "leaked" for the duration. A spare bit cannot do this: marked is
         a wrapping generation counter that takes every value in its range. */
      unsigned other = sp_gc_mark_gen ^ 1u;
      char *leaked_flag = (char *)calloc(n ? n : 1, 1);
      for(size_t i=0;i<n;i++) leaked_flag[i] = (cand[i]->marked==sp_gc_mark_gen);
#ifndef SP_THREADS
      for(sp_gc_hdr*h=sp_gc_heap;h;h=h->next) if(h->marked==sp_gc_mark_gen) h->marked=other;
#endif
      for(size_t i=0;i<n;i++) if(leaked_flag[i]) cand[i]->marked = sp_gc_mark_gen;
      for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next){
        if(h->dirty||!h->scan) continue;
        sp_gc_verify_probe_hit=0; sp_gc_verify_probe=sp_gc_mark_gen; sp_gc_verify_probe_on=1;
        h->scan((char*)h+sizeof(sp_gc_hdr));
        sp_gc_verify_probe_on=0;
        if(sp_gc_verify_probe_hit) fprintf(stderr,"spinel:   holder scan=%p\n",(void*)h->scan);
      }
#ifndef SP_THREADS
      for(sp_gc_hdr*h=sp_gc_heap;h;h=h->next) if(h->marked==other) h->marked=sp_gc_mark_gen;
#endif
      free(leaked_flag);
      sp_gc_verify_gen_fail = 1;
    }
    free(cand);
}

/* Phase accounting (SPINEL_GC_PHASES=1). sp_gc_stat_seconds says how much time
   a collection cost; it never said which part of one, so "GC is half of wall"
   gave no way to choose between the mark and the sweep. Each figure below had
   been measured by patching this file by hand, more than once; this keeps the
   measurement instead of redoing it.

   The remembered-set clear gets a bucket of its own rather than being folded
   into a sweep, and earned it: the threaded clear used to walk the WHOLE old
   heap on every non-full cycle, O(live) per collection, and nothing attributed
   it -- 5.7s of 13.1s of collector time on a server workload, invisible in the
   total. That is what this found, and #4380 then removed; it reads ~0 now. */
double sp_gc_ph_wait_top = 0;   /* joining the previous sweep at the top of the collection */
double sp_gc_ph_park_sweeping = 0; unsigned long long sp_gc_ph_park_sweeping_n = 0;   /* the park wait spent while an owner sweep of the previous cycle was still running (sp_sched.c) */
double sp_gc_ph_mark = 0, sp_gc_ph_oldsweep = 0, sp_gc_ph_slotsweep = 0,
       sp_gc_ph_rembclear = 0, sp_gc_ph_strsweep = 0, sp_gc_ph_trim = 0;
double sp_gc_ph_slot_max = 0, sp_gc_ph_task_sum = 0, sp_gc_ph_task_obj = 0, sp_gc_ph_task_sold = 0, sp_gc_ph_task_syoung = 0;   /* filled by the threaded sweep driver */
unsigned long long sp_gc_ph_mk_helpers = 0, sp_gc_ph_mk_drains = 0;
double sp_gc_ph_conc_wait = 0, sp_gc_ph_conc_wall = 0, sp_gc_ph_barrier = 0, sp_gc_ph_park = 0, sp_gc_ph_apply_obj = 0, sp_gc_ph_apply_str = 0, sp_gc_ph_apply_release = 0; unsigned long long sp_gc_ph_conc_waits = 0;   /* joining the previous sweep under the barrier */
double sp_gc_ph_mk_roots = 0, sp_gc_ph_mk_fibers = 0,
       sp_gc_ph_mk_globals = 0, sp_gc_ph_mk_scan = 0;
int sp_gc_ph_on = 0;
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
#ifndef SP_MULTI_CTX  /* per-instance under SP_MULTI_CTX (sp_ctx.h) */
unsigned long long sp_gc_stat_collections=0;
unsigned long long sp_gc_stat_fulls=0;
double sp_gc_stat_seconds=0;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
static double sp_gc_stat_now(void){
#if defined(CLOCK_MONOTONIC)
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
  return (double)ts.tv_sec+(double)ts.tv_nsec*1e-9;
#else
  return 0.0;
#endif
}
/* Close the phase that ended here. Off by default, so a build that does not ask
   for the breakdown pays one not-taken branch per boundary, against a
   collection measured in microseconds. */
#define SP_GC_PH(bucket) \
  do { if (sp_gc_ph_on) { double _t = sp_gc_stat_now(); (bucket) += _t - ph_t; ph_t = _t; } } while (0)


/* What malloc still holds is container buffers; the objects and strings
   are the slab's. A trim is still a 60 ms walk of every arena on a
   32-worker box, so once a second is the cadence (SPINEL_GC_TRIM_SEC). */
unsigned long long sp_gc_ph_trim_req = 0, sp_gc_ph_trim_inline = 0; double sp_gc_ph_trim_inline_t = 0;
static void sp_gc_trim_request(void){
  static double last_trim=0, trim_every=-1;
  if(trim_every<0){ const char*e=getenv("SPINEL_GC_TRIM_SEC"); trim_every=(e&&*e)?atof(e):1.0; }
  double now=sp_gc_stat_now();
  if(trim_every>0&&now-last_trim>=trim_every){
    last_trim=now;
    if (sp_gc_ph_on) sp_gc_ph_trim_req++;
#ifdef SP_THREADS
    /* Not here: a trim walks every arena, 60 ms on a 32-worker box, and
       under stop-the-world that was the longest pause the server had (p99
       147 ms against 105 without it). The trimmer thread (sp_sched.c) does
       it beside the running program, where a mutator that lands on the
       arena being walked waits a few milliseconds for that arena alone. */
    if (sp_gc_trimmer_on) __atomic_store_n(&sp_gc_trim_wanted, 1, __ATOMIC_RELEASE);
    else { double t0 = sp_gc_ph_on ? sp_gc_stat_now() : 0; malloc_trim(0); if (sp_gc_ph_on) { sp_gc_ph_trim_inline++; sp_gc_ph_trim_inline_t += sp_gc_stat_now() - t0; } }   /* no worker pool yet, so no trimmer: a single-threaded program on the mt archive */
#else
    malloc_trim(0);
#endif
  }
}
void sp_gc_collect(void){
  /* The previous cycle's sweep may still be running beside the mutators:
     finish it before anything here walks a list or reads a live total. */
  double ph_top0 = sp_gc_ph_on ? sp_gc_stat_now() : 0;
  if(sp_gc_conc_wait_hook) sp_gc_conc_wait_hook();
  /* the slab's workers hold slots claimed ahead of use: unclaimed before
     anything here reads the young bits (they would name garbage) */
  sp_slab_runs_release();
  size_t ob_before = sp_gc_bytes;
  double stat_t0 = sp_gc_stat_now();
  double ph_t = stat_t0;
  if (sp_gc_ph_on) sp_gc_ph_wait_top += stat_t0 - ph_top0;
  /* SPINEL_GC_AGE keeps a young survivor young for a second look and repairs
     the remembered set by probing what the cycle promoted, which it finds at
     the head of the old LIST. A slab object's promotion is a bit in its chunk
     and nothing walks those, so with the slab on an aged list object held by
     a slab object promoted this cycle would go unrecorded: aging is off then. */
  if(sp_gc_age_on&&sp_slab_on>0)sp_gc_age_on=0;
  int full=(sp_gc_cycle%sp_gc_full_interval==0);sp_gc_cycle++;
  /* Forced by growth rather than by the schedule: the old generation has
     outgrown what the last full found live in it. */
  int forced_full=0;
  if(!full&&!sp_gc_full_interval_fixed&&
     sp_gc_old_bytes>sp_gc_old_live*2+SP_GC_OLD_GROWTH_SLACK){ full=1; forced_full=1; }
  /* The string heap runs its own major cadence, and a string major needs a
     whole-heap mark: on a minor cycle the sweep has to stand it down, since
     the old strings only old objects hold were never marked. Standing it down
     silently let the string old generation grow to the object FULL cadence
     instead of its own -- 36 MB of old strings against a 0.6 MB live set on
     one benchmark, and the wall with it. So a due string major makes the
     cycle full. Off-schedule for the object heap, so its cadence does not
     adapt on the sample. */
  int str_full=0;
  if(!full&&sp_gc_minor_on&&!sp_gc_full_interval_fixed&&
     sp_gc_str_major_due_hook&&sp_gc_str_major_due_hook()){ full=1; str_full=1; }
  if(full)sp_gc_full_runs++;
  if(!full)sp_gc_minors_since_full++;
  /* new mark generation: every object becomes unmarked without touching it.
     On the (30-bit) wrap, clear the whole heap once so no stale stamp can
     alias the reused generation value. */
  sp_gc_mark_gen=(sp_gc_mark_gen+1)&0x7ffffffu;   /* marked is 27 bits (see sp_gc_hdr) */
  if(!sp_gc_mark_gen){
    sp_gc_mark_gen=1;
    sp_slab_each_object(1,1,sp_gc_stamp_reset_cb,NULL);
    for(sp_gc_hdr*hh=sp_gc_old_heap;hh;hh=hh->next)hh->marked=0;
#ifdef SP_THREADS
    { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS; for(int i=0;i<n;i++)for(sp_gc_hdr*hh=sp_gc_wslot[i].young;hh;hh=hh->next)hh->marked=0; }
#else
    for(sp_gc_hdr*hh=sp_gc_heap;hh;hh=hh->next)hh->marked=0;
#endif
  }
  /* SPINEL_GC_MINOR=0 turns this off and the collector behaves exactly as it
     did before the barrier landed. SPINEL_GC_VERIFY_GEN is what proves the
     coverage: it re-marks whole-heap after every minor and names the holder of
     anything the minor missed. */
  if (sp_gc_nremembered > sp_gc_rem_peak) sp_gc_rem_peak = sp_gc_nremembered;
  sp_gc_minor = sp_gc_minor_on && !full && !sp_gc_rem_overflow && !sp_gc_pin_overflow;
  /* Sweep beside the mutators rather than under the barrier? Only with a
     driver, and not under the diagnostic modes that read the lists between
     mark and sweep (verify, the generational verifier, aging's young probe). */
  if(sp_gc_conc_on<0){ const char*e=getenv("SPINEL_GC_CONC"); sp_gc_conc_on=!(e&&*e=='0'); }
  if(sp_gc_par_mark_on<0){ const char*e=getenv("SPINEL_GC_PAR_MARK"); sp_gc_par_mark_on=!(e&&*e=='0'); }
  int conc = sp_gc_conc_on && sp_gc_conc_sweep_hook && !sp_gc_verify && !sp_gc_verify_gen &&
             !sp_gc_age_on;
  sp_gc_conc_promote = conc;
  sp_gc_mk_bytes = 0; sp_gc_mk_young_bytes = 0;
  sp_gc_mk_str_bytes = 0; sp_gc_mk_str_young_bytes = 0; sp_gc_mk_promo_bytes = 0;
  /* the closed epoch is what the sweep frees from; everything allocated from
     here on is in the other parity and untouched by it (lib/sp_slab.c) */
  if(sp_gc_verify)sp_slab_verify_all();
  sp_slab_epoch_flip();
  /* This thread's string-length cache: the list sweep dropped each freed
     string from it one by one, and the bitmap sweep touches no string. Every
     parked worker cleared its own on the way in; the collector's (and the
     single thread's) goes here, before anything is freed. */
  sp_str_lcache_clear();
  sp_gc_mark_all();
  if(sp_gc_minor){
    /* the remembered set is the rest of the root set for a minor: each entry is
       an old object holding a reference the walk above did not follow. The
       minor stays in force through this walk and the drain: every survivor of
       a minor is promoted by its sweep, so an old object never holds a young
       one except through a recorded store, and walking INTO old objects here
       only re-marks what the cycle cannot free. Clearing the flag first made
       the drain do exactly that: 27% (gcbench) to 48% (threaded render) of a
       minor's marked objects were old, and its time followed the count. */
    for(int ri=0;ri<sp_gc_nremembered;ri++){
      sp_gc_hdr *rh=(sp_gc_hdr*)sp_gc_remembered[ri]-1;
      if(sp_gc_verify){sp_gc_dbg_phase="remembered";sp_gc_dbg_ctx=sp_gc_remembered[ri];}
      if(rh->scan) rh->scan(sp_gc_remembered[ri]);
    }
    /* and the sticky half: holders whose stores the barrier never sees, so
       there is no cycle in which they are "already recorded" */
    for(int pi=0;pi<sp_gc_npinned;pi++){
      sp_gc_hdr *ph=(sp_gc_hdr*)sp_gc_pinned[pi]-1;
      if(sp_gc_verify){sp_gc_dbg_phase="pinned";sp_gc_dbg_ctx=sp_gc_pinned[pi];}
      if(ph->scan) ph->scan(sp_gc_pinned[pi]);
    }
    if(sp_gc_verify){sp_gc_dbg_phase="minor-drain";sp_gc_dbg_ctx=NULL;}
    sp_gc_mark_drain_all();
    sp_gc_mkl_fold();
    if(sp_gc_verify){sp_gc_dbg_phase="?";sp_gc_dbg_ctx=NULL;}
  }
  sp_gc_minor = 0;
  sp_gc_conc_promote = 0;
  /* Verification: re-run the mark whole-heap and compare. Anything the full
     mark reaches that the minor did not is a reference the barrier failed to
     record -- the one failure mode of this design, silent until it is a use
     after free. Off unless SPINEL_GC_VERIFY_GEN is set. */
  if(!full && sp_gc_verify_gen) sp_gc_verify_gen_run();
  /* Drop pinned holders that are about to be freed, while the marks are final
     and before any sweep runs -- an entry naming freed memory would be
     dereferenced on the next minor cycle. What a cycle can free decides the
     test: a full one frees anything unmarked, and a minor one frees only from
     the young lists, so an old holder survives it whatever its stamp says.
     Clearing the bit on the way out is what lets the object be pinned again
     if it turns out to be alive after all -- it cannot, since it is being
     freed, but the bit and the array must not disagree. */
  { int keep = 0;
    for (int pi = 0; pi < sp_gc_npinned; pi++) {
      sp_gc_hdr *ph = (sp_gc_hdr *)sp_gc_pinned[pi] - 1;
      int live = full ? (ph->marked == sp_gc_mark_gen)
                      : (ph->old || ph->marked == sp_gc_mark_gen);
      if (live) sp_gc_pinned[keep++] = sp_gc_pinned[pi];
      else ph->pinned = 0;
    }
    sp_gc_npinned = keep;
    /* The overflow degradation lasts exactly as long as the array is full.
       While it is set every mark is whole-heap, so nothing is missed; once
       compaction has made room the set is authoritative again. */
    if (sp_gc_pin_overflow && keep < SP_GC_PINNED_MAX) sp_gc_pin_overflow = 0; }
  SP_GC_PH(sp_gc_ph_mark);
  sp_str_mark_settle(full);
  if(full){
    size_t old_before=sp_gc_old_bytes;
    if(conc){
      /* the old list is swept beside the mutators; what stays is what the
         mark reached of it, and that is known now */
      sp_gc_old_bytes=sp_gc_mk_bytes-sp_gc_mk_young_bytes;
    }
    else{
    /* the old LIST: the objects too large for the slab. The slab's old
       generation is swept with each worker's chunks below, and what stays of
       both is what the mark reached, known now. */
    /* the remembered set's bits, cleared through the array while it still
       names live memory (the sweep below frees some of what it names), as
       the concurrent form does; only an overflowed set costs the walk of
       every old object, which on a list benchmark was a tenth of the run */
    if(sp_gc_rem_overflow){ for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next)h->dirty=0; sp_slab_each_object(0,1,sp_gc_clear_dirty_cb,NULL); }
    else for(int ri=0;ri<sp_gc_nremembered;ri++)((sp_gc_hdr*)sp_gc_remembered[ri]-1)->dirty=0;
    sp_gc_hdr**pp=&sp_gc_old_heap;
    while(*pp){sp_gc_hdr*h=*pp;__builtin_prefetch(h->next);SP_GC_CTR_ADD(sp_gc_ct_swept,1);if(h->marked!=sp_gc_mark_gen){*pp=h->next;if(h->recycle){h->recycle(h);}
    else{if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));sp_slab_free(h);}}
    else{pp=&h->next;}}
    sp_gc_old_bytes=sp_gc_mk_bytes-sp_gc_mk_young_bytes;
    }
    /* Retune the cadence on what this sweep actually reclaimed. A heap the
       full cycle barely touches is one the minor mark was re-walking for
       nothing, and the interval can grow; a heap it empties is one where
       promoted objects are dying, and every cycle the interval adds is memory
       held past its death. The second case is not hypothetical: at a fixed
       interval of 128 a workload that promotes and then drops 20k arrays per
       round peaked at 285 MB against 26 at 8, and ran slower for it. */
    sp_gc_old_live=sp_gc_old_bytes;   /* re-baseline the space bound */

    /* The survival ratio is a statement about a sample taken ON SCHEDULE. A
       full forced by growth is not that sample -- it is evidence the cadence
       was already too long for this phase, and reading survival there gets the
       answer backwards: growth that is still live reads as ~100% survival and
       DOUBLES the interval. So shorten on a forced full and do not adapt. */
    if(!sp_gc_full_interval_fixed&&forced_full){
      if(sp_gc_full_interval>SP_GC_FULL_INTERVAL) sp_gc_full_interval/=2;
    }
    else if(str_full){ /* the string heap's sample, not this heap's */ }
    else if(old_before>0&&!sp_gc_full_interval_fixed){
      size_t kept=sp_gc_old_bytes;
      if(kept>old_before-(old_before>>2)){            /* >75% survived */
        /* ...unless the minors between were promoting garbage (below): a
           high survival at a SHORT cadence is what the short cadence bought */
        if(sp_gc_full_interval<SP_GC_FULL_INTERVAL_MAX&&sp_gc_last_per_minor<SP_GC_PROMOTED_DEAD_CUT/2) sp_gc_full_interval*=2;
      }
      else if(kept<(old_before>>1)){                  /* <50% survived */
        if(sp_gc_full_interval>SP_GC_FULL_INTERVAL) sp_gc_full_interval/=2;
      }
      /* Under the minor mark a second reading of the same sample: what the
         minors between two fulls PROMOTED and this full then freed, per minor,
         against the live set. A minor is worth its skipped old walk only if
         what it promotes mostly survives; an interpreter's evaluation frames
         promoted 13% of the live set per minor and died at the next full,
         and carrying them was a third of its wall (most of it outside the
         collector: the heap the mutator ran on had that much dead in it).
         Above the cut the cadence shortens toward every cycle full, where the
         minor never runs; below it the survival rule above governs. */
      if(sp_gc_minor_on&&kept>0&&sp_gc_minors_since_full>0){
        size_t dead=old_before>kept?old_before-kept:0;
        double per_minor=(double)dead/(double)kept/(double)sp_gc_minors_since_full;
        sp_gc_last_per_minor=per_minor;
        if(per_minor>SP_GC_PROMOTED_DEAD_CUT){
          if(sp_gc_full_interval>SP_GC_FULL_INTERVAL_MIN) sp_gc_full_interval/=2;
        }
        sp_gc_fulls_at_min=0;
      }
      else if(sp_gc_minor_on&&sp_gc_full_interval==SP_GC_FULL_INTERVAL_MIN){
        /* every cycle full measures nothing about minors; try one again now
           and then, and let the reading above decide */
        if(++sp_gc_fulls_at_min>=8){ sp_gc_fulls_at_min=0; sp_gc_last_per_minor=0; sp_gc_full_interval=2; }
      }
    }
    sp_gc_minors_since_full=0;
  }
  /* minor: the old list is not walked at all -- an old object's stale stamp
     simply reads as unmarked next generation, which is what a fresh unmark
     pass used to produce. */
  /* Decided BEFORE the object sweep, because the threaded path sweeps the
     string heap from inside it (each worker takes its own slot) and would
     otherwise read this flag while it was still 0 -- and then sweep the old
     string list on a minor cycle, freeing strings only an old object holds. */
  SP_GC_PH(sp_gc_ph_oldsweep);
  sp_gc_str_minor_only = (sp_gc_str_sweep_hook && !full && sp_gc_minor_on);
  /* Age survivors only on a minor whose remembered set is intact: a full
     cycle's old sweep clears every dirty bit and discards the array, so a
     holder of an object kept young would be forgotten; an overflowed set is
     cleared by a whole-heap walk for the same reason. On those cycles every
     survivor promotes, as before. */
  sp_gc_age_survivors = sp_gc_age_on && sp_gc_minor_on && !full &&
                        !sp_gc_rem_overflow && !sp_gc_pin_overflow;
  sp_gc_young_kept_bytes = 0; sp_gc_npromoted = 0;
#ifdef SP_THREADS
  if(conc){
    /* Every survivor is promoted by the mark (sp_gc_conc_promote), so the
       old generation grows by the young bytes it reached, and the live total
       is the old total. The lists themselves go to the driver, which detaches
       them from their workers and sweeps them once the world is running. */
    int str_major=0;
    int str_sweep=sp_str_sweep_begin(&str_major);
    if(sp_gc_str_minor_only) str_major=0;
    sp_gc_old_bytes+=sp_gc_mk_young_bytes;
    sp_gc_bytes=sp_gc_old_bytes+sp_gc_parked_take();   /* the pooled headers the previous sweep counted */
    /* the remembered set: what it recorded led the mark to young objects
       that are old now, so the record is spent; a full cycle cannot lean on
       the old sweep to clear the bits the way the barrier form does. Cleared
       before the sweepers start: they read the flag word of every old object
       and nobody else may be writing it then */
    if(sp_gc_rem_overflow){ for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next)h->dirty=0; sp_slab_each_object(0,1,sp_gc_clear_dirty_cb,NULL); }
    else for(int ri=0;ri<sp_gc_nremembered;ri++)((sp_gc_hdr*)sp_gc_remembered[ri]-1)->dirty=0;
    sp_gc_nremembered=0; sp_gc_rem_overflow=0;
    SP_GC_PH(sp_gc_ph_rembclear);
    sp_gc_conc_sweep_hook(full,str_sweep,str_major);
    { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS;
      for(int i=0;i<n;i++)sp_gc_wslot[i].flush_delta=0; }
    SP_GC_PH(sp_gc_ph_slotsweep);
    SP_GC_PH(sp_gc_ph_strsweep);
    sp_gc_str_minor_only = 0;
    if(full) sp_gc_trim_request();   /* the slab's own release waits for the sweep (the driver runs it) */
    SP_GC_PH(sp_gc_ph_trim);
    sp_gc_stat_collections++;
    if(full)sp_gc_stat_fulls++;
    sp_gc_stat_seconds+=sp_gc_stat_now()-stat_t0;
    if(sp_gc_obj_retune_hook)sp_gc_obj_retune_hook(ob_before);
    return;
  }
  { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS;
    /* Hand each parked worker its own slot. Only with a pool worth the barrier
       round trip: below that the serial walk the collector has always done is
       cheaper than waking everyone. */
    sp_gc_sweep_full_now=full;
    if (sp_gc_par_sweep_hook && n > 1) sp_gc_par_sweep_hook();   /* every slot's chunks too */
    else {
      for(int i=0;i<n;i++)sp_gc_sweep_young(&sp_gc_wslot[i].young);
      /* every slot's chunks, the ones no worker runs included (the sweeper
         threads', a worker that left); a chunk is swept exactly once a cycle */
      for(int i=0;i<SP_MAX_WORKERS;i++)sp_gc_sweep_chunks(i,full);
    }
    /* The recompute above set sp_gc_bytes from every live object's size, so the
       workers' unflushed per-worker deltas are now subsumed -- clear them (all
       mutators are parked, so this is race-free). */
    for(int i=0;i<n;i++)sp_gc_wslot[i].flush_delta=0; }
#else
  sp_gc_sweep_young(&sp_gc_heap);
  sp_gc_sweep_chunks(0,full);
#endif
  /* the slab's promotions were counted by the mark; the list sweeps above
     spliced theirs into the old total as they went */
  sp_gc_old_bytes+=sp_gc_mk_promo_bytes;
  /* Take the live total AFTER the sweep, not before it. The sweep accumulates
     each survivor's size into sp_gc_old_bytes as it promotes, so this reads the
     same number either way -- but a dead object's finalizer subtracts the
     buffer it accounted for on the way up, and against a total already reset to
     the live set those bytes were never there. Churning large arrays walked the
     counter below zero; the retune read the wrapped value and set a threshold
     near SIZE_MAX, which never fires again (#4073). */
  SP_GC_PH(sp_gc_ph_slotsweep);
  sp_gc_bytes=sp_gc_old_bytes+SP_GC_CTR_GET(sp_gc_young_kept_bytes)+sp_gc_parked_take();   /* the kept young and the pooled headers still occupy the heap */
  /* The remembered set has done its job and starts over after EVERY cycle, not
     only a full one. Every young object it led the mark to has just been
     promoted by the sweep above, so a holder that is not written to again has
     nothing left to record; one that is gets recorded afresh by sp_gc_wb.
     Keeping entries until the next full cycle instead made the array grow
     monotonically and overflow on any real workload.

     The clear cannot go through the array alone. Once it has overflowed,
     objects carry dirty=1 with no entry in it, and clearing only what the
     array holds leaves them permanently dirty -- so sp_gc_wb's `!h->dirty`
     test rejects them forever and every young object they later point at is
     invisible to the minor mark. That is a silent use-after-free, and it is
     why the overflow path has to pay for the whole-heap walk. */
  if(full){
    /* the old sweep above cleared every survivor; the array may name objects it
       just freed, so it must not be walked here */
    sp_gc_nremembered=0; sp_gc_rem_overflow=0;
  }
  /* Clear the bits the barrier set, not every bit in the old heap. An object can
     carry the bit with no entry naming it in exactly one case -- sp_gc_wb was
     refused a slot -- and it sets sp_gc_rem_overflow when it is, so the overflow
     branch covers precisely the state the array cannot describe. Same reasoning
     in both builds.

     This reads the array, so it needs the array consistent: see the note in
     sp_gc_wb_slow on why no collection can observe it mid-update.

     The threaded path walked the whole old list here instead, on every non-full
     cycle -- O(live) per collection for work proportional to the stores. On a
     server it was the largest phase of the stopped window (5.7s of 13.1s of
     collector time, collector at 65% of wall; campfire from matz/spinel#4352). */
  else if(sp_gc_rem_overflow){
    for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next)h->dirty=0;
    sp_slab_each_object(0,1,sp_gc_clear_dirty_cb,NULL);
    sp_gc_nremembered=0; sp_gc_rem_overflow=0;
  }
  else if(sp_gc_age_survivors){
    /* The premise of the clear -- every young object the set led the mark to
       has just been promoted -- no longer holds for the survivors the sweep
       kept young for a second look. Two kinds of holder reach those now, and
       both have to be on the set the next minor walks, or it never walks them
       and frees what they hold.

       An object promoted THIS cycle holding one: the store was young-into-
       young when it happened, so no barrier saw it, and promotion made it an
       old-into-young edge nobody recorded. The promotions sit at the head of
       the old list (every sweep prepends), so those are walked with the young
       probe armed -- the scan marks nothing and answers only whether a young
       object is reached -- and recorded when it is. A refused slot sets the
       overflow flag, which makes the next mark whole-heap.

       A holder already on the set: it stays recorded while it still reaches a
       young object, and is cleared when it does not. The holders are old and a
       minor frees no old object, so the array names nothing freed. */
    sp_gc_rem_overflow=0;
    sp_gc_young_probe_on=1;
    { size_t np=sp_gc_npromoted; sp_gc_hdr *h=sp_gc_old_heap;
      for(size_t k=0;k<np&&h;k++,h=h->next){
        if(h->dirty||!h->scan) continue;
        sp_gc_young_probe_hit=0;
        h->scan((char*)h+sizeof(sp_gc_hdr));
        if(!sp_gc_young_probe_hit) continue;
        h->dirty=1;
        if(sp_gc_nremembered<SP_GC_REMEMBERED_MAX) sp_gc_remembered[sp_gc_nremembered++]=(char*)h+sizeof(sp_gc_hdr);
        else sp_gc_rem_overflow=1;
      } }
    int keep=0;
    for(int ri=0;ri<sp_gc_nremembered;ri++){
      void *obj=sp_gc_remembered[ri];
      sp_gc_hdr *rh=(sp_gc_hdr*)obj-1;
      sp_gc_young_probe_hit=0;
      if(rh->scan) rh->scan(obj);
      if(sp_gc_young_probe_hit) sp_gc_remembered[keep++]=obj;
      else rh->dirty=0;
    }
    sp_gc_young_probe_on=0;
    sp_gc_nremembered=keep;
  }
  else{
    for(int ri=0;ri<sp_gc_nremembered;ri++)((sp_gc_hdr*)sp_gc_remembered[ri]-1)->dirty=0;
    sp_gc_nremembered=0;
    sp_gc_rem_overflow=0;
  }
  /* Under SPINEL_GC_VERIFY: the remembered set's invariant, dirty <=> listed,
     holds for every old object once the array is not overflowed. */
  if(sp_gc_verify&&!sp_gc_rem_overflow){
    sp_slab_each_object(0,1,sp_gc_rem_invariant_cb,&full);
    for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next){
      void *o=(char*)h+sizeof(sp_gc_hdr); int listed=0;
      for(int ri=0;ri<sp_gc_nremembered;ri++) if(sp_gc_remembered[ri]==o){listed=1;break;}
      if(!!h->dirty!=listed){
        fprintf(stderr,"spinel: GC remembered-set invariant broken: obj=%p scan=%p dirty=%d listed=%d full=%d age=%d cycle=%d\n",
                o,(void*)h->scan,(int)h->dirty,listed,full,sp_gc_age_survivors,sp_gc_cycle);
        abort();
      }
    }
  }
  SP_GC_PH(sp_gc_ph_rembclear);
  /* Sweep the string heap only when IT is over its trigger: the sweep is a
     full walk of the live string list, and running it on every OBJECT-heap
     collection made each collection O(live strings) -- the dominant cost of
     an allocation-heavy run. Skipping is safe: string marks accumulate, so a
     dead string at worst survives until the next string sweep (delayed
     reclamation, not a leak), and the sweep itself resets marks for the next
     cycle. The retune keeps the trigger tracking the live size. */
  /* Only on a full cycle now. The string heap has no generation of its own, so
     its sweep frees anything the mark did not reach -- and a minor mark does
     not reach a string held by an old object, because it does not walk old
     objects at all. Sweeping strings on a minor cycle therefore reaped live
     ones (test/file_basename_gc). Deferring to the full cycle is the same
     delayed reclamation the trigger already allowed. */
  /* The string heap is generational in its own right, so a minor cycle can
     sweep its young list -- the strings an old object holds live in the old
     list, which this leaves alone, and a young string stored into an old
     holder is what the barrier records. Deferring the whole sweep to the
     full cycle instead made every string-heavy workload SLOWER with the
     minor mark than without it (+17% on a string benchmark, and rubys'
     ballast inversion on lobsters); sweeping young only turns that into
     -52%. */
  if(sp_gc_str_sweep_hook){
    sp_gc_str_sweep_hook();
  }
  SP_GC_PH(sp_gc_ph_strsweep);
  sp_gc_str_minor_only = 0;
  /* malloc_trim walks the allocator arena; once per full cycle was ~10% of
     collection time on allocation-heavy runs, and "every 4th full" was a
     fraction of that only while a full was one cycle in eight. Under the
     generational mark a small-heap server runs nearly every cycle full (the
     cadence rule's floor), and every 4th of THOSE was 2.4 s of a 6.3 s
     collector budget on campfire, against 0.46 s with the mark off. The
     arena does not need walking more than about once a second: the RSS it
     returns is what the last second freed. */
  if(full){
    /* the object heap's own chunks first: what the slab hands back here is
       what malloc_trim used to find in the arena */
    sp_slab_release();
    sp_gc_trim_request();
  }
  SP_GC_PH(sp_gc_ph_trim);
  /* Bump BEFORE the retune hook: the hook is where the stats line is printed
     (sp_alloc.c sees both thresholds and the string heap), and it must read
     this collection, not the previous one. */
  sp_gc_stat_collections++;
  if(full)sp_gc_stat_fulls++;
  sp_gc_stat_seconds+=sp_gc_stat_now()-stat_t0;
  if(sp_gc_obj_retune_hook)sp_gc_obj_retune_hook(ob_before);
}

/* Issue #1302: optional RSS ceiling via SPINEL_MAX_HEAP_MB; checked only
 * at GC-trigger points against real /proc/self/statm RSS. Default off. */
void sp_gc_enforce_mem_limit(void){
  if(!sp_gc_max_bytes_init){const char*e=getenv("SPINEL_MAX_HEAP_MB");long v=(e&&*e)?atol(e):0;sp_gc_max_bytes=(v>0)?(size_t)v*1024*1024:0;sp_gc_max_bytes_init=1;}
  if(!sp_gc_max_bytes)return;
#if defined(__linux__)
  FILE*sf=fopen("/proc/self/statm","r");if(!sf)return;long tot=0,res=0;int n=fscanf(sf,"%ld %ld",&tot,&res);fclose(sf);if(n!=2||res<=0)return;
  size_t rss=(size_t)res*(size_t)sysconf(_SC_PAGESIZE);
  if(rss>sp_gc_max_bytes){fprintf(stderr,"unhandled exception: out of memory (RSS %zu MB exceeded SPINEL_MAX_HEAP_MB=%zu MB)\n",rss/(1024*1024),sp_gc_max_bytes/(1024*1024));exit(1);}
#endif
}

#ifdef SP_MULTI_CTX
/* The collector's per-instance state that does not start at zero, set up by
   sp_instance_create with the new instance current. The remembered and pinned
   sets are this instance's own arrays, sized by the config: overflowing either
   is safe (the next collection marks the whole heap, which needs neither), so
   a port trades a small array for more full marks, never for correctness. */
void sp_gc_instance_init(int remembered_entries, int pinned_entries) {
  sp_gc_full_interval = sp_gc_full_interval_start;
  sp_gc_remembered_cap = remembered_entries > 0 ? remembered_entries : 0;
  sp_gc_pinned_cap = pinned_entries > 0 ? pinned_entries : 0;
  sp_gc_remembered = sp_gc_remembered_cap ? (void **)malloc(sizeof(void *) * (size_t)sp_gc_remembered_cap) : NULL;
  sp_gc_pinned = sp_gc_pinned_cap ? (void **)malloc(sizeof(void *) * (size_t)sp_gc_pinned_cap) : NULL;
  /* the Marshal vtable is per program, filled by its init (sp_tu_init) */
  SP_CTX()->marshal_v = calloc(1, sizeof(sp_marshal_vt));
}
#endif
