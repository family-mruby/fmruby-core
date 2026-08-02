/* fmrb_spinel_host.h -- plain-C boundary for creating a Spinel runtime instance
 * on a task's estalloc pool.
 *
 * The kernel/app spawn path (fmrb_kernel.c, fmrb_app.c) cannot include the
 * Spinel runtime headers (sp_ctx.h etc.) directly: those carry their own
 * mrb_bool / value types that clash with the mruby headers already in main/.
 * So instance setup lives in fmrb_spinel_host.c (built inside this component,
 * where SP_MULTI_CTX and the spinel_rt includes are in scope) and is reached
 * only through the two functions below, which take/return void* and plain ints.
 */
#ifndef FMRB_SPINEL_HOST_H
#define FMRB_SPINEL_HOST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create an estalloc pool in [pool, pool+pool_size), build a Spinel instance
 * backed by it, and make that instance current on the calling task. All of the
 * instance's allocations (heap, GC roots, string heap) then come from this
 * pool. gc_threshold / str_threshold are the collector trigger sizes (must be
 * well below pool_size).
 *
 * Returns the ESTALLOC* (as void*) to store in ctx->est so `ps` can report the
 * pool's stats the same way it does for mruby tasks; NULL on failure. */
void *fmrb_spinel_instance_begin(void *pool, size_t pool_size,
                                 size_t gc_threshold, size_t str_threshold);

/* Tear the current instance down and release the pool. `est` is the handle
 * returned by fmrb_spinel_instance_begin (may be NULL). */
void fmrb_spinel_instance_end(void *est);

/* Depth high-waters of the instance's begin/rescue and catch stacks, keyed by
 * the est handle (what fmrb_app stores). For sizing SP_EXC_STACK_MAX /
 * SP_CATCH_STACK_MAX from observation (doc/internal_ram_budget.md, T7-1).
 * Returns 0 on success, -1 when est is unknown (outputs set to 0). Safe to
 * call from another task: it reads two ints the instance task only grows. */
int fmrb_spinel_instance_exc_hw(void *est, int *exc_hw, int *catch_hw);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPINEL_HOST_H */
