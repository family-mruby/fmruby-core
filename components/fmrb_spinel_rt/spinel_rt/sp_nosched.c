/* sp_nosched.c -- stand-ins for sp_fiber.c / sp_sched.c on a port that leaves
 * them out.
 *
 * An SP_NO_MMAN port does not compile sp_fiber.c (it maps its coroutine
 * stacks) or sp_sched.c (the green-thread scheduler, built on poll(2)). The
 * rest of the runtime still reaches a few of their functions from code every
 * program links -- the class-name and dispatch tables name Mutex and Queue,
 * File.open's FIFO wait yields with Thread.pass, IO#close tells the scheduler
 * a descriptor is gone, GC.start goes through the scheduler's barrier -- so
 * with the two files gone those references do not resolve and the port does
 * not link. This file supplies them:
 *
 *   - where the answer without threads is well defined, that answer:
 *     GC.start collects, Thread.pass and the descriptor notice do nothing
 *     (there is no other thread and no readiness set), the class names read
 *     the object as sp_sched.c does;
 *   - where the operation needs a thread or a fiber, a NotImplementedError,
 *     so a program that reaches one says so instead of misbehaving.
 *
 * Compiled to nothing unless SP_NO_MMAN is defined, so a hosted build, which
 * links the real files, is unaffected.
 */
#ifdef SP_NO_MMAN
#include "sp_alloc.h"   /* sp_RbVal, sp_raise_cls */
#include "sp_gc.h"      /* sp_gc_collect */
#include "sp_fiber.h"
#include "sp_sched.h"

void sp_gc_collect_request(void) { sp_gc_collect(); }
void sp_Thread_pass(void) { }
void sp_sched_ev_forget(int fd) { (void)fd; }
const char *sp_Mutex_class_name(sp_mutex *m) {
  return (m && m->reentrant) ? "Monitor" : "Thread::Mutex";
}
const char *sp_Queue_class_name(sp_queue *q) {
  return (q && q->max > 0) ? "Thread::SizedQueue" : "Thread::Queue";
}

static SP_NORETURN void sp_nosched_unsupported(const char *what) {
  sp_raise_cls("NotImplementedError", what);
}
void sp_Queue_push(sp_queue *q, sp_RbVal v) {
  (void)q; (void)v; sp_nosched_unsupported("Thread::Queue is not supported on this port");
}
sp_thread *sp_Thread_join(sp_thread *t) {
  (void)t; sp_nosched_unsupported("Thread is not supported on this port");
}
sp_RbVal sp_Thread_value(sp_thread *t) {
  (void)t; sp_nosched_unsupported("Thread is not supported on this port");
}
sp_RbVal sp_Fiber_resume(sp_Fiber *f, sp_RbVal val) {
  (void)f; (void)val; sp_nosched_unsupported("Fiber is not supported on this port");
}
sp_RbVal sp_Fiber_yield(sp_RbVal val) {
  (void)val; sp_nosched_unsupported("Fiber is not supported on this port");
}
#else
typedef int sp_nosched_unused;   /* a translation unit must declare something */
#endif
