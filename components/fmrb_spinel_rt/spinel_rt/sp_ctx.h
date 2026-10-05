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
#include "sp_types.h"    /* sp_int, sp_sym */
#include "sp_random.h"   /* sp_Random (by value below); pulls only sp_types.h */
#include "sp_argf.h"     /* sp_Argv / sp_Argf (by value below); pulls only sp_types.h */

struct sp_gc_hdr;
struct sp_str_hdr;
struct mrb_regexp_pattern;   /* re_* engine handle (sp_re.h) */
struct sp_Proc;              /* proc handle (sp_runtime.h); trap_proc[] below */
#ifndef SP_PROC_ARG_SLOTS
#define SP_PROC_ARG_SLOTS 64   /* token-identical to sp_proc.h */
#endif

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
  sp_Random_box random_default_box;   /* guarded like the default build's static */
  sp_int    kernel_seed;

  /* --- value-introspection vtable (was sp_gc.c; set per program by the
   *     generated TU init, so per-instance). sp_marshal_v stays shared for now
   *     (by-value sp_marshal_vt; Marshal is rarely used concurrently). --- */
  const char *(*sym_name_fn)(sp_sym);
  int         (*json_kind_fn)(sp_RbVal);
  sp_int     (*json_len_fn)(sp_RbVal);
  sp_RbVal    (*json_aref_fn)(sp_RbVal, sp_int);
  void        (*json_hpair_fn)(sp_RbVal, sp_int, sp_RbVal *, sp_RbVal *);
  sp_RbVal    (*json_mk_hash_fn)(void);
  sp_sym      (*json_sym_intern_fn)(const char *);
  void        (*json_hash_set_fn)(sp_RbVal, const char *, sp_RbVal);
  const char *(*poly_inspect_fn)(sp_RbVal);
  sp_RbVal    (*obj_to_hash_fn)(sp_RbVal);
  const char *(*obj_inspect_fn)(int cls_id, void *p);
  const char *(*obj_to_s_fn)(int cls_id, void *p);
  /* added upstream after the fork point; same role, same per-program owner */
  const char *(*poly_to_s_fn)(sp_RbVal);
  const char *(*obj_to_json_fn)(sp_RbVal);
  sp_RbVal    (*obj_to_h_fn)(sp_RbVal);
  sp_RbVal    (*obj_to_a_fn)(sp_RbVal);
  sp_RbVal    (*obj_to_ary_fn)(sp_RbVal);
  sp_RbVal    (*obj_deconstruct_fn)(sp_RbVal);
  int         (*obj_is_data_fn)(int);
  sp_RbVal    (*obj_with_fn)(sp_RbVal, sp_RbVal);
  sp_int      (*obj_to_int_fn)(int cls_id, void *p, int *ok);
  const char *(*obj_to_str_fn)(int cls_id, void *p);
  const char *(*obj_to_path_fn)(int cls_id, void *p);
  int         (*obj_conv_fn)(int cls_id, void *p, int which, sp_RbVal *out);
  const char *(*obj_cls_name_fn)(int cls_id);
  int         (*class_le_id_fn)(int sub, int super);

  /* --- TU-provided per-program state, relocated for multi-program linking
   *     (T4-0). These are defined non-static in sp_runtime.h, so two generated
   *     TUs in one binary would collide; under SP_MULTI_CTX they live here
   *     instead (data below; the ~20 TU functions become per-ctx pointers). --- */
  sp_RbVal        proc_poly_ret;         /* was _sp_proc_poly_ret */
  sp_RbVal        proc_poly_args[SP_PROC_ARG_SLOTS]; /* was _sp_proc_poly_args */
  const char     *trap_state[SP_SIG_MAX];/* was sp_trap_state */
  struct sp_Proc *trap_proc[SP_SIG_MAX]; /* was sp_trap_proc */

  /* Has this instance already cleared its program's file-scope statics?
   *
   * Only read by a program compiled with --persistent-statics. Normally the
   * entry clears the statics every time it runs, because one entry call is one
   * run of the program and the previous run's pointers are stale. A program
   * called repeatedly as a library wants the opposite: its objects should
   * outlive the call the way an ordinary Ruby object outlives a method. The
   * flag draws the line at the instance instead of the call -- cleared once
   * for a fresh instance (memset by sp_instance_create), skipped after.
   *
   * One flag per instance, not per program, so --persistent-statics is only
   * sound when a single generated TU owns the instance. That is the same
   * restriction the file-scope statics themselves impose: two programs sharing
   * an instance would already be sharing each other's civ_ slots. */
  int             statics_inited;

  /* The runtime header's own lazily allocated TU state (the break-scope
   * stack, the frozen-string dedup table) is allocated from whichever
   * instance first needs it and dies with that instance, so it must start
   * empty in every new instance -- but stay put across entry calls of the
   * same one, where clearing it would drop a live allocation. sp_tu_ctx_init
   * clears it while this is 0 (memset by sp_instance_create) and sets it. */
  int             tu_lazy_inited;

  /* --- state upstream added after the fork point (P2a inventory) ---------
   * Everything below was a process global (or a TU definition the runtime
   * reaches) in upstream 01521b1e. Each is per-program or per-heap, so two
   * instances sharing it would corrupt each other; see docs/internals/
   * multi-instance.md for the full table, including what stays shared. */

  /* generational collector (was sp_gc.c) */
  unsigned gc_mark_gen;          /* an object is marked iff its stamp equals this */
  int      gc_minor;             /* this cycle marks the young generation only */
  int      gc_mark_cap;          /* capacity of gc_mark_stack (which is per-instance) */
  size_t   gc_mk_bytes, gc_mk_young_bytes, gc_mk_str_bytes, gc_mk_str_young_bytes, gc_mk_promo_bytes;
  size_t   gc_mkl_marked, gc_mkl_bytes, gc_mkl_young, gc_mkl_str, gc_mkl_str_young, gc_mkl_promo;
  size_t   gc_old_live, gc_npromoted, gc_young_kept_bytes;
  int      gc_age_survivors, gc_minors_since_full, gc_fulls_at_min, gc_full_interval;
  double   gc_last_per_minor;
  void   **gc_remembered;        /* sized by sp_instance_config.remembered_entries */
  int      gc_remembered_cap, gc_nremembered, gc_rem_overflow, gc_rem_peak;
  void   **gc_pinned;            /* sized by sp_instance_config.pinned_entries */
  int      gc_pinned_cap, gc_npinned, gc_pin_overflow;
  int      gc_str_minor_only, gc_young_probe_on, gc_young_probe_hit;
  int      gc_verify_probe_on, gc_verify_probe_hit, gc_verify_gen_fail;
  unsigned gc_verify_probe;
  int      gc_root_phase, gc_sweep_full_now;
  size_t   gc_parked_acc, gc_ct_swept, gc_ct_marked;
  struct sp_gc_hdr **gc_vg_cand;
  size_t   gc_vg_n, gc_vg_cap;
  unsigned gc_vg_gen;
  unsigned long long gc_stat_collections, gc_stat_fulls;   /* GC.stat */
  double   gc_stat_seconds;
  int      gc_full_runs;

  /* string heap generations and budgets (was sp_alloc.c) */
  struct sp_str_hdr *str_old;
  size_t   str_old_bytes, str_old_threshold, str_old_threshold_init;
  int      str_major_interval, str_major_forced;
  unsigned str_sweep_cycle;
  size_t   str_old_slab_bytes, str_gate_before, str_gate_old;
  size_t   gc_str_majors, gc_obj_alpha1024;
  int      gc_stress_pin;
  void    *str_lcache;           /* struct sp_str_lcache_entry[SP_STR_LCACHE_SIZE] */
  void    *ret_strbuf;           /* was _sp_ret_strbuf (deep-return side channel) */
  struct sp_gc_hdr *polyarr_pool_head;
  long     polyarr_pool_count;
  int      ffi_bin_len;          /* :binstr / :cbinstr byte count side channel */
  const char **str_vcand;
  size_t   str_vcand_n, str_vcand_cap;

  /* program-wide objects and flags (was sp_cold.c / sp_inspect.c / sp_exc.c /
     sp_re.c / sp_system.c / sp_marshal.c) */
  sp_StrArray *argv_array_cache;
  void    *main_obj;             /* top-level self */
  unsigned char *class_frozen_map;   /* [4096], allocated on the first freeze */
  sp_bool  convert_soft, convert_failed;
  int      glob_dotmatch;
  void    *user_to_io_hook;      /* sp_File *(*)(sp_RbVal) */
  sp_bool  warn_flags[4];        /* Warning[category] */
  int      bt_enabled;
  const char *bt_srcfile;
  void    *poly_recur_stack;     /* sp_poly_recur_frame * */
  int      poly_recur_top, poly_recur_cap;
  void    *poly_recur_ix;        /* sp_poly_recur_slot * */
  int      poly_recur_ixcap, poly_recur_ixused, poly_recur_ixtop;
  const char *const *(*user_exc_modules_fn)(const char *);
  const char *(*user_exc_parent_fn)(const char *);
  int      re_pp_span[2];        /* $` / $' span in re_last_str */
  const char *re_startup_err;
  int      last_status;          /* $? */
  void    *mar_active;           /* Marshal.load's reader chain */
  void    *marshal_v;            /* sp_marshal_vt, filled by the program's init */
  sp_Argv  argv;                 /* was the TU's sp_argv */
  sp_Argf  argf_obj;             /* was the TU's sp_argf_obj */
  sp_RbVal pending_exc_recv, pending_exc_key, pending_exc_val;
  unsigned char pending_exc_flags;
  void   (*stack_overflow_raise_fn)(void);   /* the program's SystemStackError raise */
  void   (*re_error_handler)(const char *);  /* the program's regexp compile-error handler */

  /* --- TU functions the runtime calls, routed per-instance (T4-0). The TU
   *     keeps its own static definitions (sp_runtime.h) and registers them via
   *     sp_tu_ctx_init; the runtime .c files reach them through the name macros
   *     below. Same scheme as the introspection vtable above. --- */
  const char *(*fn_sprintf)(const char *, ...);
  sp_RbVal    (*fn_box_proc)(void *);
  void        (*fn_bigint_raise_zerodiv)(const char *);
  sp_int     (*fn_proc_call)(struct sp_Proc *, sp_int, sp_int *);
  void       *(*fn_exc_ctx_new)(void);
  void        (*fn_exc_ctx_free)(void *);
  void        (*fn_exc_ctx_save)(void *);
  void        (*fn_exc_ctx_load)(void *);
  void        (*fn_exc_ctx_mark)(void *);
  void        (*fn_exc_arm)(jmp_buf);
  void        (*fn_exc_disarm)(void);
  void        (*fn_exc_hw)(int *exc_hw, int *catch_hw); /* stack-depth high-waters (sizing aid) */
  const char *(*fn_exc_cur_cls)(void);
  const char *(*fn_exc_cur_msg)(void);
  void       *(*fn_exc_cur_obj)(void);
  void        (*fn_exc_stage_recv)(sp_RbVal);
  void        (*fn_fiber_reraise)(const char *, const char *, void *);
  SP_NORETURN void (*fn_raise_cls)(const char *, const char *);
  SP_NORETURN void (*fn_raise_stop_iteration)(sp_RbVal);
  int         (*fn_signal_resolve)(sp_RbVal);
  const char *(*fn_signal_signame)(sp_int);

  /* --- allocation backend (T3-2 sp_mem_* hooks) --- */
  void  *mem_ud;
  void *(*mem_alloc)(void *ud, size_t);    /* MUST zero-fill */
  void *(*mem_realloc)(void *ud, void *, size_t);
  void  (*mem_dealloc)(void *ud, void *);

  /* --- I/O backend (VFS hooks) ---
   * Under SP_MULTI_CTX every File/Dir op that names a path routes through
   * these instead of raw stdio/POSIX, so a host (e.g. fmruby) can back them
   * with a virtual filesystem (littlefs / HAL) where POSIX paths do not exist,
   * and so the host sees -- and can serialize -- every filesystem access the
   * program makes. The ops this contract cannot express (links, permissions,
   * times, truncation by path, the process cwd, raw descriptors) raise
   * NotImplementedError there instead of reaching the host filesystem behind
   * the backend's back.
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
  /* Path operations that open nothing. Same convention: 0 ok, <0 error
   * (errno set when the backend knows it). File.delete / File.rename /
   * Dir.mkdir / Dir.rmdir, and every path helper built on them. */
  int    (*io_remove)(void *ud, const char *path);                        /* a file, not a directory */
  int    (*io_rename)(void *ud, const char *from, const char *to);
  int    (*io_mkdir)(void *ud, const char *path);
  int    (*io_rmdir)(void *ud, const char *path);
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
  /* Generational-collector sets (upstream's write barrier): old objects that
   * were stored into since the last collection, and holders lent by-reference
   * String cells. Overflow is safe -- the next collection marks the whole
   * heap -- so a port trades RAM for more full marks, never correctness.
   * 0 = default (SP_MC_REMEMBERED_DEFAULT / SP_MC_PINNED_DEFAULT); a negative
   * value means none (every collection after a store is a full mark). */
  int    remembered_entries;
  int    pinned_entries;
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
  int    (*io_remove)(void *ud, const char *path);
  int    (*io_rename)(void *ud, const char *from, const char *to);
  int    (*io_mkdir)(void *ud, const char *path);
  int    (*io_rmdir)(void *ud, const char *path);
} sp_instance_config;

sp_ctx *sp_instance_create(const sp_instance_config *cfg);
/* Host-side reach into the current instance for C that cannot include this
   header (it would clash with the host's own types): the FFI :binstr length
   an FFI function publishes, and $?. */
int    *sp_ctx_ffi_bin_len(void);
int    *sp_ctx_last_status(void);
void    sp_instance_destroy(sp_ctx *ctx);
/* Depth high-waters of the instance's begin/rescue and catch stacks, for
 * port-side sizing of SP_EXC_STACK_MAX / SP_CATCH_STACK_MAX. Zeroes until the
 * TU has registered (sp_tu_ctx_init). */
void    sp_instance_exc_hw(sp_ctx *ctx, int *exc_hw, int *catch_hw);

/* A generated TU installs its per-program hooks (GC globals-mark, JSON/poly
 * vtable) via constructors in the default build. Those write per-instance ctx
 * fields, which do not exist at process-constructor time (SP_CTX()==NULL), so
 * under SP_MULTI_CTX the installers are plain functions and the program entry
 * calls sp_tu_ctx_init() once the host has made an instance current. */
#define SP_TU_CTOR /* not a constructor; called explicitly from the entry */

/* The --persistent-statics gate, used only by an entry compiled with it. */
#define SP_CTX_STATICS_INITED()      (SP_CTX()->statics_inited)
#define SP_CTX_MARK_STATICS_INITED() (SP_CTX()->statics_inited = 1)

#ifndef SP_MC_REMEMBERED_DEFAULT
#define SP_MC_REMEMBERED_DEFAULT 1024   /* upstream's process-wide set holds 65536 */
#endif
#ifndef SP_MC_PINNED_DEFAULT
#define SP_MC_PINNED_DEFAULT 256        /* upstream's holds 16384 */
#endif

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
#define sp_poly_to_s_fn        (SP_CTX()->poly_to_s_fn)
#define sp_obj_to_json_fn      (SP_CTX()->obj_to_json_fn)
#define sp_obj_to_h_fn         (SP_CTX()->obj_to_h_fn)
#define sp_obj_to_a_fn         (SP_CTX()->obj_to_a_fn)
#define sp_obj_to_ary_fn       (SP_CTX()->obj_to_ary_fn)
#define sp_obj_deconstruct_fn  (SP_CTX()->obj_deconstruct_fn)
#define sp_obj_is_data_fn      (SP_CTX()->obj_is_data_fn)
#define sp_obj_with_fn         (SP_CTX()->obj_with_fn)
#define sp_obj_to_int_fn       (SP_CTX()->obj_to_int_fn)
#define sp_obj_to_str_fn       (SP_CTX()->obj_to_str_fn)
#define sp_obj_to_path_fn      (SP_CTX()->obj_to_path_fn)
#define sp_obj_conv_fn         (SP_CTX()->obj_conv_fn)
#define sp_obj_cls_name_fn     (SP_CTX()->obj_cls_name_fn)
#define sp_class_le_id_fn      (SP_CTX()->class_le_id_fn)

/* state upstream added after the fork point (P2a inventory) */
#define sp_gc_mark_gen            (SP_CTX()->gc_mark_gen)
#define sp_gc_minor               (SP_CTX()->gc_minor)
#define sp_gc_mark_cap            (SP_CTX()->gc_mark_cap)
#define sp_gc_mk_bytes            (SP_CTX()->gc_mk_bytes)
#define sp_gc_mk_young_bytes      (SP_CTX()->gc_mk_young_bytes)
#define sp_gc_mk_str_bytes        (SP_CTX()->gc_mk_str_bytes)
#define sp_gc_mk_str_young_bytes  (SP_CTX()->gc_mk_str_young_bytes)
#define sp_gc_mk_promo_bytes      (SP_CTX()->gc_mk_promo_bytes)
#define sp_gc_mkl_marked          (SP_CTX()->gc_mkl_marked)
#define sp_gc_mkl_bytes           (SP_CTX()->gc_mkl_bytes)
#define sp_gc_mkl_young           (SP_CTX()->gc_mkl_young)
#define sp_gc_mkl_str             (SP_CTX()->gc_mkl_str)
#define sp_gc_mkl_str_young       (SP_CTX()->gc_mkl_str_young)
#define sp_gc_mkl_promo           (SP_CTX()->gc_mkl_promo)
#define sp_gc_old_live            (SP_CTX()->gc_old_live)
#define sp_gc_npromoted           (SP_CTX()->gc_npromoted)
#define sp_gc_young_kept_bytes    (SP_CTX()->gc_young_kept_bytes)
#define sp_gc_age_survivors       (SP_CTX()->gc_age_survivors)
#define sp_gc_minors_since_full   (SP_CTX()->gc_minors_since_full)
#define sp_gc_fulls_at_min        (SP_CTX()->gc_fulls_at_min)
#define sp_gc_full_interval       (SP_CTX()->gc_full_interval)
#define sp_gc_last_per_minor      (SP_CTX()->gc_last_per_minor)
#define sp_gc_remembered          (SP_CTX()->gc_remembered)
#define sp_gc_remembered_cap      (SP_CTX()->gc_remembered_cap)
#define sp_gc_nremembered         (SP_CTX()->gc_nremembered)
#define sp_gc_rem_overflow        (SP_CTX()->gc_rem_overflow)
#define sp_gc_rem_peak            (SP_CTX()->gc_rem_peak)
#define sp_gc_pinned              (SP_CTX()->gc_pinned)
#define sp_gc_pinned_cap          (SP_CTX()->gc_pinned_cap)
#define sp_gc_npinned             (SP_CTX()->gc_npinned)
#define sp_gc_pin_overflow        (SP_CTX()->gc_pin_overflow)
#define sp_gc_str_minor_only      (SP_CTX()->gc_str_minor_only)
#define sp_gc_young_probe_on      (SP_CTX()->gc_young_probe_on)
#define sp_gc_young_probe_hit     (SP_CTX()->gc_young_probe_hit)
#define sp_gc_verify_probe_on     (SP_CTX()->gc_verify_probe_on)
#define sp_gc_verify_probe_hit    (SP_CTX()->gc_verify_probe_hit)
#define sp_gc_verify_gen_fail     (SP_CTX()->gc_verify_gen_fail)
#define sp_gc_verify_probe        (SP_CTX()->gc_verify_probe)
#define sp_gc_root_phase          (SP_CTX()->gc_root_phase)
#define sp_gc_sweep_full_now      (SP_CTX()->gc_sweep_full_now)
#define sp_gc_parked_acc          (SP_CTX()->gc_parked_acc)
#define sp_gc_ct_swept            (SP_CTX()->gc_ct_swept)
#define sp_gc_ct_marked           (SP_CTX()->gc_ct_marked)
#define sp_gc_vg_cand             (SP_CTX()->gc_vg_cand)
#define sp_gc_vg_n                (SP_CTX()->gc_vg_n)
#define sp_gc_vg_cap              (SP_CTX()->gc_vg_cap)
#define sp_gc_vg_gen              (SP_CTX()->gc_vg_gen)
#define sp_gc_stat_collections    (SP_CTX()->gc_stat_collections)
#define sp_gc_stat_fulls          (SP_CTX()->gc_stat_fulls)
#define sp_gc_stat_seconds        (SP_CTX()->gc_stat_seconds)
#define sp_gc_full_runs           (SP_CTX()->gc_full_runs)
#define sp_str_old                (SP_CTX()->str_old)
#define sp_str_old_bytes          (SP_CTX()->str_old_bytes)
#define sp_str_old_threshold      (SP_CTX()->str_old_threshold)
#define sp_str_old_threshold_init (SP_CTX()->str_old_threshold_init)
#define sp_str_major_interval     (SP_CTX()->str_major_interval)
#define sp_str_major_forced       (SP_CTX()->str_major_forced)
#define sp_str_sweep_cycle        (SP_CTX()->str_sweep_cycle)
#define sp_str_old_slab_bytes     (SP_CTX()->str_old_slab_bytes)
#define sp_str_gate_before        (SP_CTX()->str_gate_before)
#define sp_str_gate_old           (SP_CTX()->str_gate_old)
#define sp_gc_str_majors          (SP_CTX()->gc_str_majors)
#define sp_gc_obj_alpha1024       (SP_CTX()->gc_obj_alpha1024)
#define sp_gc_stress_pin          (SP_CTX()->gc_stress_pin)
#define sp_str_lcache             ((struct sp_str_lcache_entry *)SP_CTX()->str_lcache)
#define _sp_ret_strbuf            (SP_CTX()->ret_strbuf)
#define sp_polyarr_pool_head      (SP_CTX()->polyarr_pool_head)
#define sp_polyarr_pool_count     (SP_CTX()->polyarr_pool_count)
#define sp_ffi_bin_len            (SP_CTX()->ffi_bin_len)
#define sp_str_vcand              (SP_CTX()->str_vcand)
#define sp_str_vcand_n            (SP_CTX()->str_vcand_n)
#define sp_str_vcand_cap          (SP_CTX()->str_vcand_cap)
#define sp_argv_array_cache       (SP_CTX()->argv_array_cache)
#define sp_main_obj               (SP_CTX()->main_obj)
#define sp_class_frozen_map       (SP_CTX()->class_frozen_map)
#define sp_convert_soft           (SP_CTX()->convert_soft)
#define sp_convert_failed         (SP_CTX()->convert_failed)
#define sp_glob_dotmatch          (SP_CTX()->glob_dotmatch)
#define sp_user_to_io_hook        (*(sp_File *(**)(sp_RbVal))&SP_CTX()->user_to_io_hook)
#define sp_warn_flags             (SP_CTX()->warn_flags)
#define sp_bt_enabled             (SP_CTX()->bt_enabled)
#define sp_bt_srcfile             (SP_CTX()->bt_srcfile)
#define sp_poly_recur_stack       (*(sp_poly_recur_frame **)&SP_CTX()->poly_recur_stack)
#define sp_poly_recur_top         (SP_CTX()->poly_recur_top)
#define sp_poly_recur_cap         (SP_CTX()->poly_recur_cap)
#define sp_poly_recur_ix          (*(sp_poly_recur_slot **)&SP_CTX()->poly_recur_ix)
#define sp_poly_recur_ixcap       (SP_CTX()->poly_recur_ixcap)
#define sp_poly_recur_ixused      (SP_CTX()->poly_recur_ixused)
#define sp_poly_recur_ixtop       (SP_CTX()->poly_recur_ixtop)
#define sp_user_exc_modules_fn    (SP_CTX()->user_exc_modules_fn)
#define sp_user_exc_parent_fn     (SP_CTX()->user_exc_parent_fn)
#define sp_re_pp_span             (SP_CTX()->re_pp_span)
#define sp_re_startup_err         (SP_CTX()->re_startup_err)
#define sp_mar_active             (*(sp_mar_rd **)&SP_CTX()->mar_active)
#define sp_marshal_v              (*(sp_marshal_vt *)SP_CTX()->marshal_v)
#define sp_argv                   (SP_CTX()->argv)
#define sp_argf_obj               (SP_CTX()->argf_obj)
#define sp_pending_exc_recv       (SP_CTX()->pending_exc_recv)
#define sp_pending_exc_key        (SP_CTX()->pending_exc_key)
#define sp_pending_exc_val        (SP_CTX()->pending_exc_val)
#define sp_pending_exc_flags      (SP_CTX()->pending_exc_flags)
#define sp_stack_overflow_raise_fn (SP_CTX()->stack_overflow_raise_fn)

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
