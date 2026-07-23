/* sp_sched.h -- cooperative M:N thread scheduler, Phase 0 (N=1).
 *
 * A Ruby Thread is a green thread (sp_thread) wrapping an sp_Fiber that the
 * codegen builds exactly like a Fiber.new block (so all the capture/self/cell
 * machinery is shared); the block's result lands in fiber->yielded_value. The
 * main program runs on the root fiber and IS the scheduler hub: it executes
 * top-level code directly and, whenever it blocks (#join / #value /
 * Thread.pass) or at program exit (drain), it pumps a FIFO run queue of
 * runnable green threads. A spawned thread yields by transferring back to the
 * root fiber, where the pump loop -- or the fiber trampoline, on termination --
 * resumes.
 *
 * With a single OS worker there is no concurrent heap mutation, so the GC and
 * allocator are untouched. This is the Phase 0 cooperative core of
 * docs/thread-mn-design.md; N>1 parallelism, preemption, and the
 * Mutex/Queue/ConditionVariable primitives build on this scaffolding later.
 */
#ifndef SP_SCHED_H
#define SP_SCHED_H

#include "sp_fiber.h"

typedef enum { SP_TH_RUNNABLE, SP_TH_RUNNING, SP_TH_BLOCKED, SP_TH_DEAD } sp_thread_state;

typedef struct sp_thread {
  sp_Fiber         *fiber;       /* the green thread's coroutine; NULL for the main thread (root) */
  sp_RbVal          arg;         /* Thread.new(arg) -> the block's first param, on first run */
  sp_RbVal          retval;      /* block result (copied from fiber->yielded_value at death) */
  sp_RbVal          name;        /* #name / #name= (a string or nil) */
  sp_thread_state   state;
  int               has_exc;     /* body left an unhandled exception (re-raised at #join/#value) */
  const char       *exc_cls, *exc_msg;
  void             *exc_obj;
  unsigned char     report_on_exception;
  unsigned char     off_cpu;     /* set by the worker once this thread has fully switched off its
                                    stack; a waker may only enqueue it once off_cpu, otherwise it
                                    could be run on a second worker mid-context-switch */
  unsigned char     wake_pending; /* a wake arrived while still on-cpu; the worker enqueues it */
  unsigned char     preempt_request; /* sysmon set this thread over its timeslice; it yields at its
                                        next safepoint poll (cooperative preemption, §5) */
  short             home_wid;      /* worker this thread FIRST ran on, -1 before its first run.
                                      A started green thread is pinned there: its live C frames
                                      hold compiler-cached addresses of that worker's __thread
                                      data (GC shadow stack, exception stack, ...), so resuming
                                      it on another worker corrupts both workers' TLS. */
  struct sp_thread *rq_next;     /* run-queue link while RUNNABLE */
  struct sp_thread *joiners;     /* threads parked in #join/#value on this one */
  struct sp_thread *wait_next;   /* link within the wait list it is parked on */
  struct sp_thread **wait_head;  /* head of that wait list, so #kill/#raise can unpark it */
  struct sp_thread *all_next, *all_prev;  /* registry of live threads (GC roots) */
  void             *tls;         /* thread-local storage (Thread#[] / #[]=); lazily allocated */
  double            wake_deadline; /* CLOCK_MONOTONIC time to wake a sleeping thread (Kernel#sleep) */
  int               io_fd;       /* fd this thread is parked on for I/O (-1 = none, scheduler-aware I/O) */
  short             io_events;   /* poll events it is waiting for (POLLIN/POLLOUT) */
  short             io_revents;  /* poll result the monitor delivered when the fd became ready */
  unsigned          id;
} sp_thread;

/* Called once from main()'s prologue (on the root fiber, after sp_re_init) when
   the program uses threads. Adopts the running context as the main thread and
   chains a GC mark hook that roots every live green thread. */
void       sp_sched_init(void);

/* Thread.new { ... }: wrap a fiber (built by the codegen via emit_fiber_new) in
   a green thread and enqueue it RUNNABLE. It runs the next time the current
   thread yields, or at drain. Returns the thread (boxed SP_BUILTIN_THREAD). */
sp_thread *sp_Thread_spawn_fiber(sp_Fiber *f, sp_RbVal arg);

/* #join: block until the thread has finished, re-raise its unhandled exception
   in the caller, then return the thread. #value: same, but return its result. */
sp_thread *sp_Thread_join(sp_thread *t);
sp_RbVal   sp_Thread_value(sp_thread *t);

sp_thread *sp_Thread_kill(sp_thread *t);  /* #kill / #exit / #terminate */
sp_thread *sp_Thread_raise(sp_thread *t, const char *cls, const char *msg, void *obj);  /* #raise */
void       sp_Thread_pass(void);          /* Thread.pass: cooperative yield */
/* Kernel#sleep(seconds): park the calling thread and free its OS worker for other
   green threads; a monitor thread wakes it after the duration. Falls back to a
   plain blocking sleep only in the single-threaded build (sp_runtime.h). */
void       sp_sched_sleep(double seconds);
/* Scheduler-aware blocking I/O: park the calling green thread until `fd` is ready
   for `events` (POLLIN/POLLOUT), freeing its OS worker for other threads; the
   monitor polls the fd and wakes it. Returns 1 to retry the syscall (fd ready or
   errored), 0 to give up (shutdown). Falls back to a plain blocking poll in the
   single-threaded build / before the monitor starts. */
int        sp_sched_wait_io(int fd, short events);
sp_thread *sp_Thread_current(void);       /* Thread.current */
mrb_bool   sp_Thread_alive(sp_thread *t); /* #alive? */
mrb_bool   sp_Thread_set_report_default(mrb_bool v);  /* Thread.report_on_exception= */
mrb_bool   sp_Thread_get_report_default(void);        /* Thread.report_on_exception */
mrb_bool   sp_Thread_set_report(sp_thread *t, mrb_bool v); /* #report_on_exception= */
mrb_bool   sp_Thread_get_report(sp_thread *t);            /* #report_on_exception */
sp_thread *sp_Thread_main(void);          /* Thread.main */
mrb_int    sp_Thread_list_count(void);     /* Thread.list (built by the TU) */
sp_thread *sp_Thread_list_at(mrb_int i);
sp_RbVal   sp_Thread_get_name(sp_thread *t);                       /* #name */
sp_RbVal   sp_Thread_set_name(sp_thread *t, sp_RbVal v);           /* #name= */
sp_RbVal   sp_Thread_status(sp_thread *t); /* #status: "run"/"sleep"/false/nil */
sp_RbVal   sp_Thread_tls_get(sp_thread *t, sp_sym k);              /* Thread#[] */
sp_RbVal   sp_Thread_tls_set(sp_thread *t, sp_sym k, sp_RbVal v);  /* Thread#[]= */
mrb_bool   sp_Thread_tls_key(sp_thread *t, sp_sym k);             /* Thread#key? */

/* Run any remaining runnable threads to completion. Emitted at the end of
   main() so a fire-and-forget Thread still runs its body. */
void       sp_sched_drain(void);

/* Safepoint (design 5.1). codegen emits, at loop back-edges of a threaded
   program, `if (SP_UNLIKELY(SP_SAFEPOINT_POLL())) sp_safepoint();` so a worker
   in a long-running loop periodically checks whether a stop-the-world (GC)
   wants it to park. The flag is set only in the threaded runtime while a
   collector has requested STW; at N=1 it is never set, so the poll is a single
   predicted-not-taken load. Declared unconditionally (not under SP_THREADS) so
   the emitted poll also compiles when a threaded program's generated C is built
   against the single-threaded archive (e.g. the test harness's manual cc path),
   where it is an inert no-op. sp_safepoint() parks the worker at the GC barrier
   (the real body lands with the workers + STW).

   The flag is written by the collector (and the preempt signal handler) and
   read bare by every mutator; under SP_THREADS both sides go through relaxed
   atomics so the cross-thread poll is defined behavior (and TSan-clean).
   Relaxed is enough: a mutator that misses one update just polls again next
   back-edge, and sp_safepoint() itself acquires the sched lock, which orders
   everything that matters. */
extern volatile int sp_safepoint_flag;
#ifdef SP_THREADS
#define SP_SAFEPOINT_POLL() __atomic_load_n(&sp_safepoint_flag, __ATOMIC_RELAXED)
#define SP_SAFEPOINT_SET(v) __atomic_store_n(&sp_safepoint_flag, (v), __ATOMIC_RELAXED)
#else
#define SP_SAFEPOINT_POLL() (sp_safepoint_flag)
#define SP_SAFEPOINT_SET(v) (sp_safepoint_flag = (v))
#endif
void sp_safepoint(void);

/* Optional hook the generated TU installs so a worker parking at a safepoint
   also publishes its per-worker in-flight GC roots that live in the TU (pending
   exception objects, proc-return home values) onto its shadow stack, for the
   stop-the-world collector to mark. NULL when the program has none. */
extern void (*sp_safepoint_publish_hook)(void);

/* ---- Queue (thread-safe FIFO) ----
 * A producer/consumer hand-off. #pop on an empty queue blocks the calling green
 * thread (parking it, yielding to the scheduler) until a #push wakes it; this is
 * the coordination the lazy Thread model could not express. */
typedef struct sp_queue {
  sp_RbVal         *buf;          /* ring buffer of queued values */
  mrb_int           head, len, cap;
  mrb_int           max;          /* SizedQueue capacity; 0 = unbounded Queue */
  struct sp_thread *pop_waiters;  /* threads blocked in #pop on an empty queue */
  struct sp_thread *push_waiters; /* threads blocked in #push on a full SizedQueue */
  int               closed;
} sp_queue;

sp_queue  *sp_Queue_new(void);
sp_queue  *sp_SizedQueue_new(mrb_int max);          /* SizedQueue.new(max) */
void       sp_Queue_push(sp_queue *q, sp_RbVal v);  /* #push / #<< / #enq (blocks when full) */
sp_RbVal   sp_Queue_pop(sp_queue *q);               /* #pop / #shift / #deq (blocks when empty) */
mrb_int    sp_Queue_size(sp_queue *q);              /* #size / #length */
mrb_bool   sp_Queue_empty(sp_queue *q);             /* #empty? */
mrb_int    sp_Queue_max(sp_queue *q);               /* SizedQueue#max */
void       sp_Queue_close(sp_queue *q);             /* #close */
mrb_bool   sp_Queue_closed(sp_queue *q);            /* #closed? */
void       sp_Queue_clear(sp_queue *q);             /* #clear */

/* ---- Mutex (non-recursive lock; owner + wait list) ----
 * Threads are kept alive by the scheduler registry, so neither struct needs a
 * GC scan over its owner/waiter pointers. */
typedef struct sp_mutex {
  struct sp_thread *owner;     /* NULL = unlocked */
  struct sp_thread *waiters;   /* threads blocked in #lock */
} sp_mutex;

sp_mutex  *sp_Mutex_new(void);
void       sp_Mutex_lock(sp_mutex *m);
void       sp_Mutex_unlock(sp_mutex *m);
mrb_bool   sp_Mutex_try_lock(sp_mutex *m);   /* #try_lock: true if acquired */
mrb_bool   sp_Mutex_locked(sp_mutex *m);     /* #locked? */
mrb_bool   sp_Mutex_owned(sp_mutex *m);      /* #owned? */

/* ---- ConditionVariable (wait/signal/broadcast over a Mutex) ---- */
typedef struct sp_condvar {
  struct sp_thread *waiters;   /* threads blocked in #wait */
} sp_condvar;

sp_condvar *sp_CondVar_new(void);
void        sp_CondVar_wait(sp_condvar *cv, sp_mutex *m);  /* release m, park, re-acquire m */
void        sp_CondVar_signal(sp_condvar *cv);             /* #signal */
void        sp_CondVar_broadcast(sp_condvar *cv);          /* #broadcast */

#endif /* SP_SCHED_H */
