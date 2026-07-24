/* sp_ctx.h -- per-instance runtime context (SP_MULTI_CTX).
 *
 * Lets several Spinel-compiled programs share one process, each with its own
 * heap and GC on its own OS thread. See docs/internals/multi-instance.md.
 *
 * Design contract:
 *   - SP_MULTI_CTX UNDEFINED (default): this header is inert. The runtime's
 *     mutable globals keep their original definitions in sp_alloc.c / sp_gc.c /
 *     sp_re.c / sp_random.c and their original names resolve to those globals.
 *     Nothing here changes the generated code or the hot path -- byte-identical.
 *   - SP_MULTI_CTX DEFINED: the same globals become macros onto sp_ctx fields
 *     reached through SP_CTX() (a thread-local current-instance pointer). The
 *     runtime .c files guard their definitions with #ifndef SP_MULTI_CTX so the
 *     state lives only in the ctx, initialized by sp_instance_create().
 *
 * Only library-side state (compiled once into libspinel_rt.a and shared across
 * translation units) needs relocation. sp_runtime.h statics are per-TU already
 * (it is included only by the generated program) and stay put.
 *
 * INCLUDE ORDER: the sp_ctx struct has fields typed as sp_RbVal / sp_sym
 * (the value-introspection vtable), which are defined in sp_gc.h. So this
 * header must be reached AFTER sp_gc.h. That is automatic on the normal path
 * (sp_gc.h includes this header right after defining sp_RbVal); a .c file that
 * includes sp_ctx.h directly must include sp_gc.h (or a header that pulls it)
 * first. sp_ctx.h intentionally does NOT include sp_gc.h -- that would be a
 * cycle (sp_gc.h needs this header's macros before its own inline helpers).
 */
#ifndef SP_CTX_H
#define SP_CTX_H

#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>      /* jmp_buf (fn_exc_arm below) */
#include "sp_types.h"    /* mrb_int, sp_sym */
#include "sp_random.h"   /* sp_Random (by value below); pulls only sp_types.h */

struct sp_gc_hdr;
struct sp_str_hdr;
struct mrb_regexp_pattern;   /* re_* engine handle (sp_re.h) */
struct sp_Proc;              /* proc handle (sp_runtime.h); trap_proc[] below */

/* Per-instance runtime state. Field names are the original global names with
 * their sp_/sp_gc_ prefix dropped; the compat macros below re-attach them. */
typedef struct sp_ctx {
  /* --- string allocator (was sp_alloc.c) --- */
  struct sp_str_hdr *str_heap;
  size_t str_heap_bytes;
  size_t str_threshold;
  size_t str_threshold_init;
  int    str_stress_checked;

  /* --- object GC thresholds (was sp_alloc.c) --- */
  size_t gc_threshold;
  size_t gc_threshold_init;
  int    gc_stress_checked;

  /* --- GC heap/state (was sp_gc.c) --- */
  struct sp_gc_hdr *gc_heap;
  struct sp_gc_hdr *gc_old_heap;
  size_t gc_bytes;
  size_t gc_old_bytes;
  int    gc_cycle;
  int    gc_verify;
  void **gc_mark_stack;
  int    gc_mark_top;
  struct sp_gc_hdr **gc_vsnap;
  size_t gc_vsnap_n, gc_vsnap_cap;
  size_t gc_max_bytes;
  int    gc_max_bytes_init;
  void  *gc_dbg_ctx;

  /* --- GC root stack (was sp_gc.c / sp_gc.h) --- */
  void ***gc_roots;   /* default: points at a static array; MC: heap */
  int     gc_nroots;
  int     gc_roots_cap;

  /* --- GC program hooks (per program; see multi-instance.md) --- */
  void (*gc_mark_globals_hook)(void);
  void (*gc_str_sweep_hook)(void);
  void (*gc_mark_suspended_fibers_hook)(void);

  /* --- regexp last-match state (was sp_re.c, $~) --- */
  const char *re_captures[10];
  int         re_caps[64];
  const char *re_last_str;
  const char *re_match_str;
  const char *re_match_pre;
  const char *re_match_post;
  int         re_last_ncap;
  const struct mrb_regexp_pattern *re_last_pat;

  /* --- RNG state (was sp_random.c) --- */
  uint64_t   krand_state;
  int        krand_seeded;
  sp_Random  random_default;
  mrb_int    kernel_seed;

  /* --- value-introspection vtable (was sp_gc.c; set per program by the
   *     generated TU init, so per-instance). sp_marshal_v stays shared for now
   *     (by-value sp_marshal_vt; Marshal is rarely used concurrently). --- */
  const char *(*sym_name_fn)(sp_sym);
  int         (*json_kind_fn)(sp_RbVal);
  mrb_int     (*json_len_fn)(sp_RbVal);
  sp_RbVal    (*json_aref_fn)(sp_RbVal, mrb_int);
  void        (*json_hpair_fn)(sp_RbVal, mrb_int, sp_RbVal *, sp_RbVal *);
  sp_RbVal    (*json_mk_hash_fn)(void);
  sp_sym      (*json_sym_intern_fn)(const char *);
  void        (*json_hash_set_fn)(sp_RbVal, const char *, sp_RbVal);
  const char *(*poly_inspect_fn)(sp_RbVal);
  sp_RbVal    (*obj_to_hash_fn)(sp_RbVal);
  const char *(*obj_inspect_fn)(int cls_id, void *p);
  const char *(*obj_to_s_fn)(int cls_id, void *p);

  /* --- TU-provided per-program state, relocated for multi-program linking
   *     (T4-0). These are defined non-static in sp_runtime.h, so two generated
   *     TUs in one binary would collide; under SP_MULTI_CTX they live here
   *     instead (data below; the ~20 TU functions become per-ctx pointers). --- */
  sp_RbVal        proc_poly_ret;         /* was _sp_proc_poly_ret */
  sp_RbVal        proc_poly_args[16];    /* was _sp_proc_poly_args */
  const char     *trap_state[SP_SIG_MAX];/* was sp_trap_state */
  struct sp_Proc *trap_proc[SP_SIG_MAX]; /* was sp_trap_proc */

  /* --- TU functions the runtime calls, routed per-instance (T4-0). The TU
   *     keeps its own static definitions (sp_runtime.h) and registers them via
   *     sp_tu_ctx_init; the runtime .c files reach them through the name macros
   *     below. Same scheme as the introspection vtable above. --- */
  const char *(*fn_sprintf)(const char *, ...);
  sp_RbVal    (*fn_box_proc)(void *);
  void        (*fn_bigint_raise_zerodiv)(const char *);
  mrb_int     (*fn_proc_call)(struct sp_Proc *, mrb_int, mrb_int *);
  void       *(*fn_exc_ctx_new)(void);
  void        (*fn_exc_ctx_free)(void *);
  void        (*fn_exc_ctx_save)(void *);
  void        (*fn_exc_ctx_load)(void *);
  void        (*fn_exc_ctx_mark)(void *);
  void        (*fn_exc_arm)(jmp_buf);
  void        (*fn_exc_disarm)(void);
  const char *(*fn_exc_cur_cls)(void);
  const char *(*fn_exc_cur_msg)(void);
  void       *(*fn_exc_cur_obj)(void);
  void        (*fn_exc_stage_recv)(sp_RbVal);
  void        (*fn_fiber_reraise)(const char *, const char *, void *);
  SP_NORETURN void (*fn_raise_cls)(const char *, const char *);
  SP_NORETURN void (*fn_raise_stop_iteration)(sp_RbVal);
  int         (*fn_signal_resolve)(sp_RbVal);
  const char *(*fn_signal_signame)(mrb_int);

  /* --- allocation backend (T3-2 sp_mem_* hooks) --- */
  void  *mem_ud;
  void *(*mem_alloc)(void *ud, size_t);    /* MUST zero-fill */
  void *(*mem_realloc)(void *ud, void *, size_t);
  void  (*mem_dealloc)(void *ud, void *);

  /* --- I/O backend (VFS hooks) ---
   * Under SP_MULTI_CTX the File/Dir ops route regular-file byte I/O through
   * these instead of raw stdio/POSIX, so a host (e.g. fmruby) can back them
   * with a virtual filesystem (littlefs / HAL) where POSIX paths do not exist.
   * The handle is opaque (void*), stored in sp_File.fp / sp_Dir.dp; the
   * default backend below stores a FILE or DIR pointer there. Console streams
   * (stdout/stderr/stdin) bypass the backend (identified by fp == std stream).
   * Defaults to the libc/POSIX backend (sp_io_posix_*) when the config leaves
   * a slot NULL, so an un-hooked instance behaves exactly like the default
   * build. Minimal byte-level contract; gets/read-all/eof are reimplemented on
   * top of read/seek/tell in the runtime, so a backend need only provide these. */
  void  *io_ud;
  void  *(*io_open)(void *ud, const char *path, const char *mode);        /* NULL on error */
  long   (*io_read)(void *ud, void *h, char *buf, long n);                /* bytes, <0 error */
  long   (*io_write)(void *ud, void *h, const char *buf, long n);         /* bytes, <0 error */
  long   (*io_seek)(void *ud, void *h, long off, int whence);            /* 0=SET 1=CUR 2=END; new pos, <0 error */
  long   (*io_tell)(void *ud, void *h);                                   /* pos, <0 error */
  int    (*io_close)(void *ud, void *h);
  int    (*io_stat)(void *ud, const char *path, long *size, int *is_dir, int *is_reg); /* 0 ok, -1 absent */
  void  *(*io_opendir)(void *ud, const char *path);                       /* NULL on error */
  int    (*io_readdir)(void *ud, void *dh, char *namebuf, int cap);       /* 1 = filled namebuf, 0 = end */
  int    (*io_closedir)(void *ud, void *dh);
} sp_ctx;

/* ------------------------------------------------------------------------- */
#ifdef SP_MULTI_CTX

#ifdef SP_THREADS
#error "SP_MULTI_CTX and SP_THREADS are mutually exclusive (see multi-instance.md)"
#endif

/* Platform-provided current-instance accessor. Reference impl in sp_ctx.c uses
 * a __thread pointer; the ESP-IDF port (Phase 5) will use a FreeRTOS
 * task-local storage pointer. */
sp_ctx *sp_ctx_current(void);
void    sp_ctx_set_current(sp_ctx *ctx);
#define SP_CTX() sp_ctx_current()

/* Instance lifecycle. */
typedef struct {
  size_t gc_threshold;        /* 0 = default (256 KiB) */
  size_t str_threshold;       /* 0 = default */
  int    root_stack_entries;  /* 0 = default (SP_GC_STACK_MAX) */
  void  *mem_ud;              /* opaque, passed to the hooks below */
  void *(*alloc)(void *ud, size_t);          /* NULL = calloc default; MUST zero */
  void *(*realloc_fn)(void *ud, void *, size_t);
  void  (*dealloc)(void *ud, void *);
  /* I/O backend (VFS). Any NULL slot falls back to the libc/POSIX backend, so a
   * config that sets none behaves like the default build. See sp_ctx above. */
  void  *io_ud;
  void  *(*io_open)(void *ud, const char *path, const char *mode);
  long   (*io_read)(void *ud, void *h, char *buf, long n);
  long   (*io_write)(void *ud, void *h, const char *buf, long n);
  long   (*io_seek)(void *ud, void *h, long off, int whence);
  long   (*io_tell)(void *ud, void *h);
  int    (*io_close)(void *ud, void *h);
  int    (*io_stat)(void *ud, const char *path, long *size, int *is_dir, int *is_reg);
  void  *(*io_opendir)(void *ud, const char *path);
  int    (*io_readdir)(void *ud, void *dh, char *namebuf, int cap);
  int    (*io_closedir)(void *ud, void *dh);
} sp_instance_config;

sp_ctx *sp_instance_create(const sp_instance_config *cfg);
void    sp_instance_destroy(sp_ctx *ctx);

/* A generated TU installs its per-program hooks (GC globals-mark, JSON/poly
 * vtable) via constructors in the default build. Those write per-instance ctx
 * fields, which do not exist at process-constructor time (SP_CTX()==NULL), so
 * under SP_MULTI_CTX the installers are plain functions and the program entry
 * calls sp_tu_ctx_init() once the host has made an instance current. */
#define SP_TU_CTOR /* not a constructor; called explicitly from the entry */

/* --- name-compatibility macros: original global -> ctx field --- */
#define sp_str_heap            (SP_CTX()->str_heap)
#define sp_str_heap_bytes      (SP_CTX()->str_heap_bytes)
#define sp_str_threshold       (SP_CTX()->str_threshold)
#define sp_str_threshold_init  (SP_CTX()->str_threshold_init)
#define sp_str_stress_checked  (SP_CTX()->str_stress_checked)
#define sp_gc_threshold        (SP_CTX()->gc_threshold)
#define sp_gc_threshold_init   (SP_CTX()->gc_threshold_init)
#define sp_gc_stress_checked   (SP_CTX()->gc_stress_checked)
#define sp_gc_heap             (SP_CTX()->gc_heap)
#define sp_gc_old_heap         (SP_CTX()->gc_old_heap)
#define sp_gc_bytes            (SP_CTX()->gc_bytes)
#define sp_gc_old_bytes        (SP_CTX()->gc_old_bytes)
#define sp_gc_cycle            (SP_CTX()->gc_cycle)
#define sp_gc_verify           (SP_CTX()->gc_verify)
#define sp_gc_mark_stack       (SP_CTX()->gc_mark_stack)
#define sp_gc_mark_top         (SP_CTX()->gc_mark_top)
#define sp_gc_vsnap            (SP_CTX()->gc_vsnap)
#define sp_gc_vsnap_n          (SP_CTX()->gc_vsnap_n)
#define sp_gc_vsnap_cap        (SP_CTX()->gc_vsnap_cap)
#define sp_gc_max_bytes        (SP_CTX()->gc_max_bytes)
#define sp_gc_max_bytes_init   (SP_CTX()->gc_max_bytes_init)
#define sp_gc_dbg_ctx          (SP_CTX()->gc_dbg_ctx)
#define sp_gc_roots            (SP_CTX()->gc_roots)
#define sp_gc_nroots           (SP_CTX()->gc_nroots)
#define sp_gc_mark_globals_hook          (SP_CTX()->gc_mark_globals_hook)
#define sp_gc_str_sweep_hook             (SP_CTX()->gc_str_sweep_hook)
#define sp_gc_mark_suspended_fibers_hook (SP_CTX()->gc_mark_suspended_fibers_hook)

/* regexp last-match ($~) state */
#define sp_re_captures    (SP_CTX()->re_captures)
#define sp_re_caps        (SP_CTX()->re_caps)
#define sp_re_last_str    (SP_CTX()->re_last_str)
#define sp_re_match_str   (SP_CTX()->re_match_str)
#define sp_re_match_pre   (SP_CTX()->re_match_pre)
#define sp_re_match_post  (SP_CTX()->re_match_post)
#define sp_re_last_ncap   (SP_CTX()->re_last_ncap)
#define sp_re_last_pat    (SP_CTX()->re_last_pat)

/* RNG state */
#define sp_krand_state      (SP_CTX()->krand_state)
#define sp_krand_seeded     (SP_CTX()->krand_seeded)
#define sp_random_default   (SP_CTX()->random_default)
#define sp_kernel_seed      (SP_CTX()->kernel_seed)

/* value-introspection vtable (per program) */
#define sp_sym_name_fn         (SP_CTX()->sym_name_fn)
#define sp_json_kind_fn        (SP_CTX()->json_kind_fn)
#define sp_json_len_fn         (SP_CTX()->json_len_fn)
#define sp_json_aref_fn        (SP_CTX()->json_aref_fn)
#define sp_json_hpair_fn       (SP_CTX()->json_hpair_fn)
#define sp_json_mk_hash_fn     (SP_CTX()->json_mk_hash_fn)
#define sp_json_sym_intern_fn  (SP_CTX()->json_sym_intern_fn)
#define sp_json_hash_set_fn    (SP_CTX()->json_hash_set_fn)
#define sp_poly_inspect_fn     (SP_CTX()->poly_inspect_fn)
#define sp_obj_to_hash_fn      (SP_CTX()->obj_to_hash_fn)
#define sp_obj_inspect_fn      (SP_CTX()->obj_inspect_fn)
#define sp_obj_to_s_fn         (SP_CTX()->obj_to_s_fn)

/* Root-stack capacity: dynamic per instance. */
#define SP_GC_ROOTS_CAP (SP_CTX()->gc_roots_cap)

/* TU-provided per-program state relocated into the ctx (T4-0, data). The
 * runtime .c files reach these through these macros; sp_runtime.h drops the
 * corresponding definitions under SP_MULTI_CTX. */
#define _sp_proc_poly_ret   (SP_CTX()->proc_poly_ret)
#define _sp_proc_poly_args   (SP_CTX()->proc_poly_args)
#define sp_trap_state        (SP_CTX()->trap_state)
#define sp_trap_proc         (SP_CTX()->trap_proc)

/* TU functions routed per-instance (T4-0). The runtime .c files call these
 * names; the macros send them to the current instance's registered pointer.
 * sp_runtime.h #undefs these (its own definitions/calls use the direct names)
 * and registers the definitions in sp_tu_ctx_init. */
#define sp_sprintf               (SP_CTX()->fn_sprintf)
#define sp_box_proc              (SP_CTX()->fn_box_proc)
#define sp_bigint_raise_zerodiv  (SP_CTX()->fn_bigint_raise_zerodiv)
#define sp_proc_call             (SP_CTX()->fn_proc_call)
#define sp_exc_ctx_new           (SP_CTX()->fn_exc_ctx_new)
#define sp_exc_ctx_free          (SP_CTX()->fn_exc_ctx_free)
#define sp_exc_ctx_save          (SP_CTX()->fn_exc_ctx_save)
#define sp_exc_ctx_load          (SP_CTX()->fn_exc_ctx_load)
#define sp_exc_ctx_mark          (SP_CTX()->fn_exc_ctx_mark)
#define sp_exc_arm               (SP_CTX()->fn_exc_arm)
#define sp_exc_disarm            (SP_CTX()->fn_exc_disarm)
#define sp_exc_cur_cls           (SP_CTX()->fn_exc_cur_cls)
#define sp_exc_cur_msg           (SP_CTX()->fn_exc_cur_msg)
#define sp_exc_cur_obj           (SP_CTX()->fn_exc_cur_obj)
#define sp_exc_stage_recv        (SP_CTX()->fn_exc_stage_recv)
#define sp_fiber_reraise         (SP_CTX()->fn_fiber_reraise)
#define sp_raise_cls             (SP_CTX()->fn_raise_cls)
#define sp_raise_stop_iteration  (SP_CTX()->fn_raise_stop_iteration)
#define sp_signal_resolve        (SP_CTX()->fn_signal_resolve)
#define sp_signal_signame        (SP_CTX()->fn_signal_signame)

/* TU keeps private definitions of the routed functions (per-instance copies). */
#define SP_TU_STATIC static

/* The libc allocation names are remapped to per-instance wrappers by
 * sp_mem_override.h, force-included into every mc TU (see that header and
 * sp_ctx.c). Nothing to declare here. */

#else  /* !SP_MULTI_CTX -- default: inert, globals stay as-is */

#define SP_GC_ROOTS_CAP SP_GC_STACK_MAX

/* Default build: TU hook installers run before main as process constructors. */
#define SP_TU_CTOR __attribute__((constructor))

/* Default build: routed TU functions keep external linkage (single program per
 * binary, resolved directly). */
#define SP_TU_STATIC

#endif /* SP_MULTI_CTX */

#endif /* SP_CTX_H */
