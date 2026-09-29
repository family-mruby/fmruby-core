/* sp_ctx.c -- per-instance runtime context implementation (SP_MULTI_CTX).
 * Inert in the default (single-context) build. See sp_ctx.h and
 * docs/internals/multi-instance.md. */
#include "sp_gc.h"   /* sp_RbVal etc. for the sp_ctx struct; also pulls sp_ctx.h */
#include "sp_ctx.h"

#ifdef SP_MULTI_CTX
#include "sp_io.h"   /* sp_io_posix_* default backend */
#include <stdlib.h>
#include <string.h>

/* This file DEFINES the allocation wrappers that sp_mem_override.h (force-
 * included into every mc TU, this one included) maps the libc names onto, so
 * the bodies below must reach the real libc, not themselves. Undo the remap. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup

/* Defined in sp_alloc.c: the string sweep / retune hooks and the env floors
 * the default build installs from a process constructor, run per instance
 * below instead. */
void sp_alloc_instance_init(void);
/* Defined in sp_gc.c: the collector's per-instance arrays and defaults. */
void sp_gc_instance_init(int remembered_entries, int pinned_entries);

/* Reference current-instance accessor: a thread-local pointer. One instance
 * runs per OS thread (programs are internally single-threaded). The ESP-IDF
 * port (Phase 5) swaps this for a FreeRTOS task-local storage pointer. */
static __thread sp_ctx *g_sp_ctx = NULL;

sp_ctx *sp_ctx_current(void)          { return g_sp_ctx; }
int    *sp_ctx_last_status(void)      { return &g_sp_ctx->last_status; }   /* $? (sp_system.h) */
/* The :binstr / :cbinstr byte count an FFI function publishes, for host C that
   does not include the runtime's headers (it cannot name the ctx field): set
   *sp_ctx_ffi_bin_len() where the default build sets sp_ffi_bin_len. */
int    *sp_ctx_ffi_bin_len(void)      { return &g_sp_ctx->ffi_bin_len; }
/* the regexp engine's compile-error handler (re_compile.c cannot include this
   header's types, so it asks for the slot) */
void  (**sp_ctx_re_error_handler(void))(const char *) { return &g_sp_ctx->re_error_handler; }
void    sp_ctx_set_current(sp_ctx *c) { g_sp_ctx = c; }

/* --- allocation wrappers (targets of the sp_mem_override.h macros) ---
 *
 * Route through the current instance's backend when one is set; otherwise fall
 * back to libc. In a running program an instance is always current with a
 * backend (sp_instance_create sets one, and the host calls sp_ctx_set_current
 * before the entry), so the fallback only covers any stray pre-entry
 * allocation -- which must not then be freed across the boundary.
 *
 * The backend MUST zero-fill (sp_instance_config contract), so sp_mem_malloc
 * yields zeroed memory too: the runtime's calloc-style assumptions hold
 * uniformly and malloc/calloc collapse onto one hook (the modest zero-fill cost
 * is tracked in `make bench`). Exhaustion goes through sp_oom_die rather than
 * returning NULL into a caller that would deref it. */
void *sp_mem_malloc(size_t n) {
  if (n == 0) n = 1;
  sp_ctx *c = g_sp_ctx;
  void *p = (c && c->mem_alloc) ? c->mem_alloc(c->mem_ud, n) : calloc(1, n);
  if (!p) sp_oom_die();
  return p;
}
void *sp_mem_calloc(size_t nmemb, size_t size) {
  size_t n;
  if (__builtin_mul_overflow(nmemb, size, &n)) sp_oom_die();
  return sp_mem_malloc(n);
}
void *sp_mem_realloc(void *p, size_t n) {
  sp_ctx *c = g_sp_ctx;
  void *r = (c && c->mem_realloc) ? c->mem_realloc(c->mem_ud, p, n) : realloc(p, n);
  if (n && !r) sp_oom_die();
  return r;
}
void sp_mem_free(void *p) {
  if (!p) return;
  sp_ctx *c = g_sp_ctx;
  if (c && c->mem_dealloc) c->mem_dealloc(c->mem_ud, p);
  else free(p);
}
/* realloc through the instance's backend that reports exhaustion instead of
   ending the program: for a caller with a fallback (the GC mark stack, which
   recurses when it cannot grow). */
void *sp_mem_try_realloc(void *p, size_t n) {
  sp_ctx *c = g_sp_ctx;
  return (c && c->mem_realloc) ? c->mem_realloc(c->mem_ud, p, n) : realloc(p, n);
}
char *sp_mem_strdup(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s) + 1;
  char *d = (char *)sp_mem_malloc(len);
  memcpy(d, s, len);
  return d;
}

/* --- T4-0: real .a definitions of the routed functions that macro-less runtime
 *     TUs reference. The regexp engine and the bigint shim cannot include
 *     sp_ctx.h (their own mrb_bool / shim types conflict), so they cannot use
 *     the per-ctx name macros; they link these globals instead. The two raise
 *     functions forward to the current instance's registered copy; sp_sprintf is
 *     program-independent (vsnprintf + the ctx-routed string heap), so a single
 *     shared definition is correct. --- */
#include <stdarg.h>
#include <stdio.h>
#include "sp_alloc.h"   /* sp_str_alloc (static inline). Included while the
                           routing macros are still active so sp_alloc.h's own
                           inline helpers (e.g. sp_raise_frozen_array) resolve
                           sp_raise_cls through the macro. */
#undef sp_raise_cls
#undef sp_bigint_raise_zerodiv
#undef sp_sprintf
SP_NORETURN void sp_raise_cls(const char *cls, const char *msg) { g_sp_ctx->fn_raise_cls(cls, msg); }
void sp_bigint_raise_zerodiv(const char *msg) { g_sp_ctx->fn_bigint_raise_zerodiv(msg); }
const char *sp_sprintf(const char *fmt, ...) {
  char tmp[SP_SCRATCH(4096)]; va_list ap; va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
  if (n < 0) n = 0;
  char *b = sp_str_alloc((size_t)n);
  if (n < (int)sizeof(tmp)) memcpy(b, tmp, (size_t)n);
  else { va_start(ap, fmt); vsnprintf(b, (size_t)n + 1, fmt, ap); va_end(ap); }
  return b;
}

/* --- default libc backend (used when cfg->alloc is NULL) --- */
static void *dflt_alloc(void *ud, size_t n)            { (void)ud; return calloc(1, n); }
static void *dflt_realloc(void *ud, void *p, size_t n) { (void)ud; return realloc(p, n); }
static void  dflt_dealloc(void *ud, void *p)           { (void)ud; free(p); }

#ifndef SP_GC_STACK_MAX
#define SP_GC_STACK_MAX 65536
#endif

sp_ctx *sp_instance_create(const sp_instance_config *cfg) {
  sp_instance_config z = {0};
  if (!cfg) cfg = &z;
  void *(*a)(void *, size_t)          = cfg->alloc      ? cfg->alloc      : dflt_alloc;
  void *(*re)(void *, void *, size_t) = cfg->realloc_fn ? cfg->realloc_fn : dflt_realloc;
  void  (*de)(void *, void *)         = cfg->dealloc    ? cfg->dealloc    : dflt_dealloc;

  sp_ctx *c = (sp_ctx *)a(cfg->mem_ud, sizeof(sp_ctx));
  if (!c) return NULL;
  memset(c, 0, sizeof(*c));
  /* The default Random is marked through a rooted pointer: the byte before
     it must say "no header here" (see sp_random.c). */
  c->random_default_box.guard[sizeof c->random_default_box.guard - 1] = (char)0xfd;
  c->mem_ud = cfg->mem_ud;
  c->mem_alloc = a; c->mem_realloc = re; c->mem_dealloc = de;

  /* I/O backend: any NULL slot falls back to the libc/POSIX backend, so an
     un-hooked instance reads/writes exactly like the default build. */
  c->io_ud       = cfg->io_ud;
  c->io_open     = cfg->io_open     ? cfg->io_open     : sp_io_posix_open;
  c->io_read     = cfg->io_read     ? cfg->io_read     : sp_io_posix_read;
  c->io_write    = cfg->io_write    ? cfg->io_write    : sp_io_posix_write;
  c->io_seek     = cfg->io_seek     ? cfg->io_seek     : sp_io_posix_seek;
  c->io_tell     = cfg->io_tell     ? cfg->io_tell     : sp_io_posix_tell;
  c->io_close    = cfg->io_close    ? cfg->io_close    : sp_io_posix_close;
  c->io_stat     = cfg->io_stat     ? cfg->io_stat     : sp_io_posix_stat;
  c->io_opendir  = cfg->io_opendir  ? cfg->io_opendir  : sp_io_posix_opendir;
  c->io_readdir  = cfg->io_readdir  ? cfg->io_readdir  : sp_io_posix_readdir;
  c->io_closedir = cfg->io_closedir ? cfg->io_closedir : sp_io_posix_closedir;
  c->io_remove    = cfg->io_remove    ? cfg->io_remove    : sp_io_posix_remove;
  c->io_rename    = cfg->io_rename    ? cfg->io_rename    : sp_io_posix_rename;
  c->io_mkdir     = cfg->io_mkdir     ? cfg->io_mkdir     : sp_io_posix_mkdir;
  c->io_rmdir     = cfg->io_rmdir     ? cfg->io_rmdir     : sp_io_posix_rmdir;

  size_t gct = cfg->gc_threshold  ? cfg->gc_threshold  : (size_t)256 * 1024;
  size_t sct = cfg->str_threshold ? cfg->str_threshold : (size_t)256 * 1024;
  c->gc_threshold = c->gc_threshold_init = gct;
  c->str_threshold = c->str_threshold_init = sct;

  int rn = cfg->root_stack_entries ? cfg->root_stack_entries : SP_GC_STACK_MAX;
  c->gc_roots = (void ***)a(cfg->mem_ud, (size_t)rn * sizeof(void **));
  if (!c->gc_roots) { de(cfg->mem_ud, c); return NULL; }
  c->gc_roots_cap = rn;
  c->gc_nroots = 0;

  /* Wire the string sweep into this instance's collector (the default build
   * does this in a process constructor; under SP_MULTI_CTX it is per-ctx).
   * The installer writes through the ctx macros, so make c current for it. */
  { sp_ctx *prev = g_sp_ctx; g_sp_ctx = c;
    int rem = cfg->remembered_entries ? cfg->remembered_entries : SP_MC_REMEMBERED_DEFAULT;
    int pin = cfg->pinned_entries ? cfg->pinned_entries : SP_MC_PINNED_DEFAULT;
    sp_gc_instance_init(rem, pin);
    sp_alloc_instance_init();
    g_sp_ctx = prev; }

  /* The rest of the upstream-added state whose default is not zero. */
  c->warn_flags[1] = 1;                          /* Warning[:deprecated] -- as sp_cold.c's table */
  c->re_pp_span[0] = c->re_pp_span[1] = -1;      /* no $` / $' span yet */
  c->bt_srcfile = "";

  /* GC verify: read the env here rather than in the process constructor, which
   * has no current instance to write into. */
  { const char *v = getenv("SPINEL_GC_VERIFY"); c->gc_verify = (v && *v && *v != '0'); }
  return c;
}

void sp_instance_destroy(sp_ctx *c) {
  if (!c) return;
  void (*de)(void *, void *) = c->mem_dealloc;
  void *ud = c->mem_ud;
  /* Free GC heaps. Objects with finalizers/recycle are torn down by the
   * collector's normal sweep; a hard teardown just frees the arenas. */
  de(ud, c->gc_roots);
  de(ud, c->gc_mark_stack);
  de(ud, c->gc_vsnap);
  de(ud, c->gc_remembered);
  de(ud, c->gc_pinned);
  de(ud, c->str_lcache);
  de(ud, c->marshal_v);
  de(ud, c->class_frozen_map);
  de(ud, c);
}

void sp_instance_exc_hw(sp_ctx *c, int *exc_hw, int *catch_hw) {
  if (c && c->fn_exc_hw) { c->fn_exc_hw(exc_hw, catch_hw); return; }
  if (exc_hw)   *exc_hw = 0;
  if (catch_hw) *catch_hw = 0;
}

#endif /* SP_MULTI_CTX */
