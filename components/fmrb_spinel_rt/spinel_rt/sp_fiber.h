/* sp_fiber.h -- Fiber runtime surface.
 *
 * The sp_Fiber struct (a cooperative coroutine) is shared between the generated
 * translation unit and lib/sp_fiber.c, which holds the function bodies. The
 * context switch lives in sp_fiber_ctx.h (a portable asm switch on x86_64 /
 * aarch64, ucontext elsewhere) -- no <ucontext.h> here, so OpenBSD compiles.
 * `storage` is an opaque pointer to fiber-local variable storage. */
#ifndef SP_FIBER_H
#define SP_FIBER_H

#include "sp_gc.h"   /* sp_RbVal */
#include "sp_fiber_ctx.h"


#define SP_FIBER_STACK_SIZE (64*1024)

/* ThreadSanitizer support: the asm context switch swaps stacks without TSan's
   knowledge, so a fiber program reports spurious "unexpected memory mapping"
   or false races. Under -fsanitize=thread we give each fiber a TSan fiber
   handle and call __tsan_switch_to_fiber at every swap so TSan follows the
   cooperative switch. SP_TSAN is set only in that build (a separate archive),
   so the fiber struct carries the two extra fields only there -- the archive
   and the generated TU agree because both are compiled with -fsanitize=thread. */
#ifndef __has_feature           /* gcc lacks __has_feature; clang has it */
#define __has_feature(x) 0
#endif
#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
#define SP_TSAN 1
#endif

typedef struct sp_Fiber{sp_fiber_ctx ctx;sp_fiber_ctx caller_ctx;char*stack;int state;int transferred;sp_RbVal yielded_value;sp_RbVal resumed_value;void(*body)(struct sp_Fiber*);void*user_data;int saved_exc_top;int saved_catch_top;void*exc_ctx;int raised;const char*raised_cls;const char*raised_msg;void*raised_obj;int inject;const char*inj_cls;const char*inj_msg;void*inj_obj;void*storage;void***saved_roots;int saved_nroots;int saved_roots_cap;struct sp_Fiber*fiber_next;struct sp_Fiber*fiber_prev;
#ifdef SP_TSAN
  void *tsan_fiber;                 /* __tsan fiber handle for this coroutine */
  struct sp_Fiber *caller_fiber;    /* who switched into us (the swap-out target) */
#endif
}sp_Fiber;

/* The currently-running fiber; the generated TU reads it directly for
   `Fiber.current` and as the implicit receiver of Fiber operations. Per-worker
   (SP_TLS) in the threaded build so each worker tracks the green thread it runs;
   the generated TU is compiled with the matching -DSP_THREADS. (The scheduler
   root fiber it is initialized to stays a shared static until per-worker root
   fibers land with the workers.) */
extern SP_TLS sp_Fiber *sp_fiber_current;

/* Adopt the calling OS thread's root fiber (its native scheduler stack) as its
   current coroutine, and read that per-worker root back. Worker 0 calls
   sp_fiber_worker_init from sp_sched_init; helper workers from their startup. In
   the single-threaded build the root is a shared static and these are inert. */
void      sp_fiber_worker_init(void);
sp_Fiber *sp_fiber_worker_root(void);
/* Publish the running worker's shadow-stack roots into its current green thread
   (for a stop-the-world collector to mark while the worker is parked). */
void      sp_fiber_publish_current_roots(void);
/* Mark a fiber's published (saved) roots; used by the collector to reach a
   parked worker's root fiber, which is not on the global fiber list. */
void      sp_fiber_mark_roots(sp_Fiber *f);

/* Public Fiber API (called from the generated TU). */
sp_Fiber *sp_Fiber_new(void (*body)(sp_Fiber *));
sp_RbVal sp_Fiber_resume(sp_Fiber *f, sp_RbVal val);
sp_RbVal sp_Fiber_yield(sp_RbVal val);
sp_RbVal sp_Fiber_transfer(sp_Fiber *f, sp_RbVal val);
/* Like sp_Fiber_transfer, but captures f's unhandled termination exception into
   the out-params (for the thread scheduler) instead of re-raising it. */
sp_RbVal sp_Fiber_transfer_catch(sp_Fiber *f, sp_RbVal val, int *out_raised,
                                 const char **out_cls, const char **out_msg, void **out_obj);
/* Fiber#raise: inject an exception at the fiber's suspension point (or at entry
   for an unstarted fiber). cls/msg describe a class+message; obj, when non-NULL,
   is a pre-built exception object to raise instead. Returns the next yielded
   value, or re-raises in the caller if the fiber does not handle it. */
sp_RbVal sp_Fiber_raise(sp_Fiber *f, const char *cls, const char *msg, void *obj);
/* Fiber#kill: terminate the fiber (running its ensure blocks); returns it. */
sp_Fiber *sp_Fiber_kill(sp_Fiber *f);
mrb_bool sp_Fiber_alive(sp_Fiber *f);
sp_RbVal sp_Fiber_storage_get(sp_Fiber *f, sp_sym k);
void sp_Fiber_storage_set(sp_Fiber *f, sp_sym k, sp_RbVal v);
/* Reached from sp_re_mark_globals in the generated TU during a GC pass. */
void sp_mark_fiber_root_storage(void);

/* Thread #kill / #raise: the scheduler queues a pending inject on a target
   fiber, then fires it when that thread next runs (in its own context). */
void sp_fiber_set_raise_inject(sp_Fiber *f, const char *cls, const char *msg, void *obj);
void sp_fiber_set_kill_inject(sp_Fiber *f);
void sp_fiber_fire_inject_if_pending(void);
int  sp_fiber_inject_pending(sp_Fiber *f);   /* lock-free acquire peek */
SP_NORETURN void sp_fiber_raise_kill_self(void);

#endif
