/* fmrb_spinel_host.c -- create/destroy a Spinel runtime instance on an estalloc
 * pool. See fmrb_spinel_host.h for why this boundary exists (header isolation
 * from the mruby side). Built inside the fmrb_spinel_rt component, so it sees
 * SP_MULTI_CTX and the spinel_rt headers; it deliberately includes NO mruby /
 * main headers so the runtime's mrb_bool etc. do not collide. */
#include <string.h>
#include "fmrb_spinel_host.h"

#ifdef SP_MULTI_CTX
#include "sp_gc.h"   /* pulls sp_ctx.h: sp_instance_config, lifecycle */

/* estalloc entry points (picoruby, lib/estalloc). Declared here with opaque
 * (void*) handles so this file need not include estalloc.h. est_calloc zero-
 * fills, satisfying the sp_instance_config "alloc MUST zero" contract. */
extern void *est_init(void *ptr, unsigned int size);
extern void *est_calloc(void *est, unsigned int nmemb, unsigned int size);
extern void *est_realloc(void *est, void *ptr, unsigned int size);
extern void  est_free(void *est, void *ptr);
extern void  est_cleanup(void *est);

static void *est_alloc_hook(void *ud, size_t n)            { return est_calloc(ud, 1u, (unsigned int)n); }
static void *est_realloc_hook(void *ud, void *p, size_t n) { return est_realloc(ud, p, (unsigned int)n); }
static void  est_free_hook(void *ud, void *p)             { est_free(ud, p); }

void *fmrb_spinel_instance_begin(void *pool, size_t pool_size,
                                 size_t gc_threshold, size_t str_threshold) {
    void *est = est_init(pool, (unsigned int)pool_size);
    if (!est) return NULL;
    sp_instance_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.mem_ud     = est;
    cfg.alloc      = est_alloc_hook;
    cfg.realloc_fn = est_realloc_hook;
    cfg.dealloc    = est_free_hook;
    cfg.gc_threshold  = gc_threshold;
    cfg.str_threshold = str_threshold;
    sp_ctx *c = sp_instance_create(&cfg);
    if (!c) { est_cleanup(est); return NULL; }
    sp_ctx_set_current(c);
    return est;
}

void fmrb_spinel_instance_end(void *est) {
    sp_ctx *c = sp_ctx_current();
    if (c) sp_instance_destroy(c);
    sp_ctx_set_current(NULL);
    if (est) est_cleanup(est);
}

#else  /* !SP_MULTI_CTX: single-context build has no per-instance API. */

void *fmrb_spinel_instance_begin(void *pool, size_t pool_size,
                                 size_t gc_threshold, size_t str_threshold) {
    (void)pool; (void)pool_size; (void)gc_threshold; (void)str_threshold;
    return NULL;
}
void fmrb_spinel_instance_end(void *est) { (void)est; }

#endif /* SP_MULTI_CTX */
