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
 * SP_CATCH_STACK_MAX from observation (doc/reference/internal_ram_budget.md, T7-1).
 * Returns 0 on success, -1 when est is unknown (outputs set to 0). Safe to
 * call from another task: it reads two ints the instance task only grows. */
int fmrb_spinel_instance_exc_hw(void *est, int *exc_hw, int *catch_hw);

/* ---- A Spinel gem's single instance and the task that owns it ----
 *
 * A Spinel gem (raycast, fft, spinel_hello) keeps ONE instance of its program
 * for the whole firmware, while every mruby app has its own VM and so its own
 * Ruby-side reference count. The instance is current only on the task that
 * created it, and a generated program cannot run twice at once
 * (doc/spinel_multi_instance). So the first app task to open it owns it until
 * it closes it or ends; any other task is told the instance is busy and the
 * gem's Ruby runs that app on its non-Spinel backend instead
 * (doc/spinel_multi_instance/report/g1.md).
 *
 * The gem owns the struct (a file-scope static in its receiver, zeroed) and
 * fills `name` and `pool_free` statically. `owner` is changed only by
 * fmrb_spinel_gem_claim (compare-and-swap from NULL) and by whoever ends the
 * instance; everything else is written by the owner task alone, or by the
 * killer after the owner task has been deleted. */
#define FMRB_SPINEL_GEM_APP_NAME 32

typedef struct fmrb_spinel_gem {
    const char *name;                 /* "raycast", for the logs */
    void      (*pool_free)(void *);   /* how the gem's pool goes back */
    void       *owner;                /* task holding the instance, NULL when free */
    void       *pool;                 /* the instance's memory, NULL when closed */
    void       *est;                  /* fmrb_spinel_instance_begin's handle, NULL when closed */
    int         owner_pid;            /* for the logs */
    char        owner_app[FMRB_SPINEL_GEM_APP_NAME];
    struct fmrb_spinel_gem *next;     /* on the list fmrb_spinel_gem_task_ended walks */
    int         linked;
} fmrb_spinel_gem_t;

/* Results of fmrb_spinel_gem_claim. */
#define FMRB_SPINEL_GEM_CLAIMED  1   /* taken now: the caller builds the instance */
#define FMRB_SPINEL_GEM_MINE     0   /* `task` already holds it */
#define FMRB_SPINEL_GEM_BUSY   (-1)  /* another task holds it */

/* Take the gem for `task` (an fmrb task handle). `pid` / `app` name the
 * caller in later logs. After CLAIMED the caller sets pool and est as it
 * builds the instance, and calls fmrb_spinel_gem_release if it fails. */
int fmrb_spinel_gem_claim(fmrb_spinel_gem_t *g, void *task, int pid, const char *app);

/* Is `task` the owner of an open instance? The gate for every entry call. */
int fmrb_spinel_gem_is_open_on(const fmrb_spinel_gem_t *g, void *task);

/* Give the gem back: clears pool/est and then the owner. The owner calls this
 * after it has torn its instance down (or failed to build one). */
void fmrb_spinel_gem_release(fmrb_spinel_gem_t *g);

/* `task` has ended (fmrb_app's cleanup, both the normal and the forced path).
 * Any gem instance it still owns is dropped: the instance is removed from the
 * table fmrb_spinel_instance_exc_hw reads, its pool is released, and the gem
 * is free for the next app. Safe to call from another task once `task` is
 * deleted -- it does not touch the calling task's current instance. A no-op
 * for a task that owns nothing. Returns how many instances it dropped. */
int fmrb_spinel_gem_task_ended(void *task);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPINEL_HOST_H */
