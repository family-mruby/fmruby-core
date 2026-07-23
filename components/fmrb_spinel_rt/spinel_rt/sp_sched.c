/* sp_sched.c -- cooperative M:N thread scheduler bodies, Phase 0 (N=1).
 * See sp_sched.h. Built on the sp_fiber context switch: the main thread runs on
 * the root fiber and pumps a run queue of green threads whenever it blocks. */
#include "sp_sched.h"
#include "sp_alloc.h"   /* sp_box_nil / sp_box_obj */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>     /* sysconf (worker count) */
#include <time.h>       /* clock_gettime (Kernel#sleep) */
#include <errno.h>      /* EINTR (sleep fallback) */
#include <signal.h>     /* preemption signal (SIGURG by default) */
#include <strings.h>    /* strcasecmp (SPINEL_PREEMPT_SIGNAL by name) */
#include <stdint.h>     /* intptr_t (worker id passed via pthread arg) */
#include <poll.h>       /* poll (scheduler-aware I/O) */
#include <fcntl.h>      /* fcntl O_NONBLOCK (monitor wake pipe) */

/* Reached by name (defined in lib/sp_alloc.c or the generated TU), exactly as
   lib/sp_fiber.c reaches them. */
void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *));
void sp_re_push_match_roots(void);   /* lib/sp_re.c: STW match-register publishing */
SP_NORETURN void sp_raise_cls(const char *cls, const char *msg);
void sp_fiber_reraise(const char *cls, const char *msg, void *obj);
/* Per-context exception handler stack (defined in the generated TU). */
void *sp_exc_ctx_new(void);
void  sp_exc_ctx_save(void *p);
void  sp_exc_ctx_load(void *p);
void  sp_exc_ctx_free(void *p);

/* Safepoint (design 5.1). Set while a collector wants the world stopped; polled
   at loop back-edges (codegen) and at blocking points. Defined unconditionally
   (see sp_sched.h) so a threaded program's poll links against either archive;
   the flag is only ever set in the threaded build. sp_safepoint() parks the
   worker at the GC barrier (defined below, after the lock). At N=1 the flag is
   never set by another worker, so a single worker never parks here. */
volatile int sp_safepoint_flag = 0;
void (*sp_safepoint_publish_hook)(void) = NULL;   /* set by the generated TU (sp_sched.h) */

/* ---- scheduler lock (design 3, Appendix B) ----
 * One mutex guards all scheduler/sync metadata: the run queue, the live-thread
 * registry, every wait list (joiners, Queue/Mutex/ConditionVariable waiters) and
 * the Queue ring buffers. A worker holds it only while touching that metadata
 * and ALWAYS drops it across a fiber transfer (running a green thread or parking
 * itself). The held region therefore never polls a safepoint, allocates from the
 * GC heap, or runs Ruby -- which is what lets a stop-the-world collector make
 * progress: a worker blocked on this lock releases it within a bounded critical
 * section and then reaches its next safepoint. Internal helpers (rq_*, reg_*,
 * sp_sched_wake_one, sp_sched_unpark, sp_thread_wake_joiners) run with the lock
 * already held by their caller; sp_sched_block / sp_sched_pump / sp_sched_pass
 * are entered and left with the lock held, bracketing their transfers. In the
 * single-threaded archive the macros are no-ops, so that build is byte-identical
 * and the N=1 path is unchanged save for the (uncontended) lock calls. */
#define SP_MAX_WORKERS 256   /* both builds: sizes the per-worker run-queue array */
#ifdef SP_THREADS
#include <pthread.h>
static pthread_mutex_t g_sched_lock = PTHREAD_MUTEX_INITIALIZER;
#define SCHED_LOCK()    pthread_mutex_lock(&g_sched_lock)
#define SCHED_UNLOCK()  pthread_mutex_unlock(&g_sched_lock)

/* ---- stop-the-world GC barrier (design 6.2) ----
 * All state guarded by g_sched_lock. g_stw_active is the authoritative
 * "collection in progress" predicate; sp_safepoint_flag is a lock-free hint the
 * codegen polls at loop back-edges so a running worker checks in cheaply. The
 * triggering worker (over the alloc threshold) either becomes the sole collector
 * or, if one is already running, parks like everyone else -- there is no
 * separate collector lock to block on, so a worker that crossed the threshold
 * can never stall the collector by being un-parkable. */
static int            g_nworkers = 1;   /* worker count; C-3b raises it past 1 */
static int            g_nparked  = 0;   /* workers parked at the barrier right now */
/* The fiber each parked worker was running when it published its roots (a green
   thread, or the worker's root fiber for an idle/main worker). The collector
   marks these: green-thread fibers are also reached via sp_fiber_list_head, but a
   worker's root fiber is not on that list, so without this a helper collector
   would miss the main thread's top-level roots. */
static sp_Fiber      *g_parked_fiber[2 * SP_MAX_WORKERS];   /* up to 2 per worker: current + root */
static int            g_n_parked_fiber = 0;
static int            g_stw_active = 0; /* a collection is in progress */
static unsigned       g_stw_epoch = 0;  /* bumped each collection; scopes g_nparked to one */
static SP_TLS int     g_collector_active = 0;  /* this worker is mid-collection (re-entrancy guard) */
static int            g_shutdown = 0;   /* set at drain so helper workers exit their loop */
static pthread_cond_t g_sched_work = PTHREAD_COND_INITIALIZER;   /* idle workers wait for runnable work */
static pthread_cond_t g_stw_request = PTHREAD_COND_INITIALIZER;  /* collector waits for parks */
static pthread_cond_t g_stw_release = PTHREAD_COND_INITIALIZER;  /* parked workers wait for clear */
static pthread_cond_t g_sysmon_cv = PTHREAD_COND_INITIALIZER;    /* the monitor thread waits here */
static pthread_t      g_sysmon;                                  /* monitor: wakes sleepers + preempts */
static int            g_sysmon_started = 0;
static int            g_sysmon_idle = 0; /* monitor is parked on g_sysmon_cv (signal it to start ticking) */
static int            g_sysmon_pipe[2] = { -1, -1 };  /* self-pipe: wake the monitor out of poll() */
/* Wake the monitor whether it idles on the condvar or blocks in poll(). PRE: lock held. */
static void sp_sysmon_wake(void) {
  if (g_sysmon_idle) pthread_cond_signal(&g_sysmon_cv);
  else if (g_sysmon_pipe[1] >= 0) { char c = 1; ssize_t r = write(g_sysmon_pipe[1], &c, 1); (void)r; }
}
static double sp_monotonic_now(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ---- preemption (design §5): timeslice tracking + the one safepoint flag ----
 * The monitor watches how long each worker has run its current green thread; past
 * a quantum it sets that thread's preempt_request, raises the safepoint flag, and
 * SIGURGs the worker. The thread yields at its next safepoint poll -- which the
 * codegen emits at loop back-edges, i.e. points that hold no runtime lock, so a
 * preempting thread always parks at a safe point. g_npreempt counts outstanding
 * requests so the flag (shared with GC stop-the-world) is cleared only when no
 * reason remains. All of this is touched solely under g_sched_lock. */
#define SP_PREEMPT_QUANTUM 0.010   /* a green thread runs ~10ms before it must yield */
#define SP_PREEMPT_TICK    0.005   /* monitor re-checks worker timeslices this often while busy */
static int g_npreempt = 0;         /* preempt_requests set but not yet consumed */
static int g_preempt_sig = SIGURG; /* the signal the monitor sends; SPINEL_PREEMPT_SIGNAL overrides */
typedef struct { pthread_t tid; sp_thread *cur; double since; int active; } sp_wslot;
static sp_wslot   g_wslot[SP_MAX_WORKERS];   /* per-worker: the green thread it runs + when it started */
static void sp_recompute_safepoint_flag(void) {   /* PRE: g_sched_lock held */
  SP_SAFEPOINT_SET(g_stw_active || g_npreempt > 0);
}
/* The preemption signal lands on the target worker's own stack. Re-assert the
   flag so the worker sees it even if it was about to clear the lock-free read;
   the actual yield happens cooperatively at the next safepoint poll (kept minimal
   and async-signal-safe -- a lone relaxed atomic store). */
static void sp_preempt_handler(int sig) { (void)sig; SP_SAFEPOINT_SET(1); }
#define SCHED_WAKE()    pthread_cond_signal(&g_sched_work)   /* nudge one idle worker after enqueue/wake */
#define SCHED_WAKE_ALL() pthread_cond_broadcast(&g_sched_work)  /* wake every waiter to re-check state */

/* Park the calling worker at the barrier until the collection finishes,
   publishing its running green thread's roots first. PRE: g_sched_lock held. */
static void sp_stw_park_locked(void) {
  /* Publish the shadow-stack roots plus this worker's live match registers (TLS,
     so the collector's globals hook does not reach them) into the green thread's
     saved snapshot, then restore our own root depth -- the snapshot keeps them. */
  int saved_nroots = sp_gc_nroots;
  sp_re_push_match_roots();
  if (sp_safepoint_publish_hook) sp_safepoint_publish_hook();   /* TU in-flight exc / proc homes */
  sp_fiber_publish_current_roots();
  sp_gc_nroots = saved_nroots;
  /* The collection about to run may recycle a string's address; drop this
     worker's pointer-keyed length cache so it cannot return a stale length for a
     reused address after the sweep (the collector clears its own via the sweep). */
  sp_str_lcache_clear();
  /* Record the fibers the collector must mark for this worker: the green thread
     it is running (sp_fiber_current) AND its root fiber. The root fiber holds the
     worker's own suspended context -- for the main thread that is the top-level
     locals, saved when it transferred into the green thread it is pumping -- and
     it is not on the global fiber list, so without this it would be missed. */
  sp_Fiber *root = sp_fiber_worker_root();
  if (sp_fiber_current && g_n_parked_fiber < 2 * SP_MAX_WORKERS)
    g_parked_fiber[g_n_parked_fiber++] = sp_fiber_current;   /* collector marks these */
  if (root && root != sp_fiber_current && g_n_parked_fiber < 2 * SP_MAX_WORKERS)
    g_parked_fiber[g_n_parked_fiber++] = root;
  /* Park for the current collection (epoch). g_nparked counts only this epoch's
     parkers: when the next collection starts it bumps the epoch and resets the
     count, and a straggler from a finished collection (epoch mismatch) must not
     touch the new count -- otherwise the new collector could see a stale count
     and proceed before anyone has actually parked, marking an incomplete root
     set (then sweeping a still-live root). */
  unsigned my_epoch = g_stw_epoch;
  g_nparked++;
  if (g_nparked >= g_nworkers - 1) pthread_cond_signal(&g_stw_request);
  while (g_stw_active && g_stw_epoch == my_epoch) pthread_cond_wait(&g_stw_release, &g_sched_lock);
  if (g_stw_epoch == my_epoch) g_nparked--;
}
#else
#define SCHED_LOCK()    ((void)0)
#define SCHED_UNLOCK()  ((void)0)
#define SCHED_WAKE()    ((void)0)
#define SCHED_WAKE_ALL() ((void)0)
#endif

#ifdef SP_THREADS
static void sp_safepoint_preempt(void);   /* defined after the scheduler state below */
#endif

/* Safepoint poll body: codegen emits `if (sp_safepoint_flag) sp_safepoint();` at
   loop back-edges. Park if a stop-the-world is in progress, then yield if the
   monitor flagged this green thread as over its timeslice (design §5). */
void sp_safepoint(void) {
#ifdef SP_THREADS
  SCHED_LOCK();
  if (g_stw_active) sp_stw_park_locked();
  sp_safepoint_preempt();
  SCHED_UNLOCK();
#endif
}

/* Stop the world and collect (design 6.2). Called by sp_gc_alloc once over the
   threshold, with the heap lock released. Either become the collector -- set the
   barrier, wait for every other worker to park, then mark+sweep with exclusive
   heap access -- or, if a collection is already running, just park through it.
   At N=1 there are no other workers, so the wait is a no-op and this is exactly
   today's inline collect, routed through the barrier. */
void sp_stw_collect(void) {
#ifdef SP_THREADS
  /* Re-entrancy guard: a finalizer run during the sweep may allocate and cross
     the threshold again. We are already the collector with the world stopped
     (exclusive heap access), so just let that allocation proceed -- re-entering
     the barrier here would park the collector waiting on itself (deadlock). */
  if (g_collector_active) return;
  SCHED_LOCK();
  if (g_stw_active) { sp_stw_park_locked(); SCHED_UNLOCK(); return; }
  if (!sp_gc_collection_wanted()) { SCHED_UNLOCK(); return; }  /* another worker just collected */
  g_stw_active = 1;
  g_stw_epoch++;     /* new epoch; a previous collection's stragglers won't be counted */
  g_nparked = 0;     /* this collection's park count starts fresh */
  SP_SAFEPOINT_SET(1);
  /* wake idle workers (and main waiting in its pump) so they park at the barrier
     rather than sit through the collection without publishing their roots. */
  pthread_cond_broadcast(&g_sched_work);
  while (g_nparked < g_nworkers - 1) pthread_cond_wait(&g_stw_request, &g_sched_lock);
  /* Our own root fiber holds this worker's suspended context (the main thread's
     top-level locals if it triggered the collection while pumping a green
     thread). We do not park, so record it here for the mark like a parked worker
     does -- our current fiber's roots are reached directly from our TLS stack. */
  {
    sp_Fiber *croot = sp_fiber_worker_root();
    if (croot && croot != sp_fiber_current && g_n_parked_fiber < 2 * SP_MAX_WORKERS)
      g_parked_fiber[g_n_parked_fiber++] = croot;
  }
  SCHED_UNLOCK();
  /* exclusive: every other worker is parked at a safepoint with roots published */
  g_collector_active = 1;
  sp_gc_collect_retune_all();   /* sweeps both heaps; marks parked fibers via sp_sched_globals_mark */
  g_collector_active = 0;
  SCHED_LOCK();
  g_n_parked_fiber = 0;
  g_stw_active = 0;
  sp_recompute_safepoint_flag();   /* keep the flag set if a preempt is still pending */
  pthread_cond_broadcast(&g_stw_release);
  SCHED_UNLOCK();
#else
  sp_gc_collect_retune();
#endif
}

/* ---- scheduler state (single OS worker, so plain globals) ---- */
static sp_thread  g_main_thread;         /* the main thread: runs on root, fiber == NULL */
static SP_TLS sp_thread *g_current = NULL;   /* per-worker: the green thread this worker runs now */
/* Run queues (design 3.1). A worker requeues a thread it just ran onto its OWN
   local queue (g_lrq[wid]) so a yielding thread reruns on the same worker (warm
   cache); spawns and wakeups, which have no worker affinity, land on the shared
   global queue (g_grq). A worker picks local-first, then global, then steals one
   from another worker before parking. All queues are guarded by g_sched_lock --
   this gives locality and load balancing without a second lock; reducing the
   lock itself would mean reworking the off-cpu handshake (deferred, see git log).
   g_runnable is the total parked-runnable count across every queue, for the
   quiescence/deadlock predicate. */
static SP_TLS int g_worker_id = 0;       /* this worker's run-queue slot (0 = main); both builds */
typedef struct { sp_thread *head, *tail; } sp_runq;
static sp_runq    g_grq;                 /* global run queue: spawned + woken threads */
static sp_runq    g_lrq[SP_MAX_WORKERS]; /* per-worker local run queues (rerun locality) */
static int        g_runnable = 0;        /* threads sitting in any run queue right now */
static int        g_nrunning = 0;        /* workers currently executing a green thread (quiescence) */
static sp_thread *g_sleepers = NULL;     /* threads parked in Kernel#sleep, woken by deadline */
static sp_thread *g_io_waiters = NULL;    /* threads parked on a fd, woken by the monitor's poll */
static struct pollfd *g_pfds = NULL;     /* monitor's poll set, rebuilt from g_io_waiters each tick */
static sp_thread    **g_pths = NULL;     /* parallel to g_pfds: the thread waiting on each fd */
static int            g_pcap = 0;        /* capacity of g_pfds / g_pths */
static sp_thread *g_all = NULL;          /* registry of live threads, for GC rooting */
static unsigned   g_next_id = 1;
static unsigned char g_report_default = 1;  /* Thread.report_on_exception default */

static void runq_push(sp_runq *q, sp_thread *t) {
  t->state = SP_TH_RUNNABLE; t->rq_next = NULL;
  if (q->tail) q->tail->rq_next = t; else q->head = t;
  q->tail = t; g_runnable++;
}
static sp_thread *runq_pop(sp_runq *q) {
  sp_thread *t = q->head;
  if (t) { q->head = t->rq_next; if (!q->head) q->tail = NULL; t->rq_next = NULL; g_runnable--; }
  return t;
}
/* Spawn: the shared global queue (no worker affinity yet). */
static void rq_push(sp_thread *t)           { runq_push(&g_grq, t); }
/* Rerun a thread on the worker that just ran it (locality). */
static void lrq_push(int wid, sp_thread *t) { runq_push(&g_lrq[wid], t); }
/* Requeue/wake a thread that may have already run. A STARTED thread must go
   back to its home worker's local queue -- never the global queue -- because
   its live frames cache that worker's __thread addresses (see home_wid). An
   unstarted thread has no affinity and takes the global queue. */
static void runq_requeue(sp_thread *t) {
#ifdef SP_THREADS
  if (t->home_wid >= 0) { runq_push(&g_lrq[t->home_wid], t); return; }
#endif
  runq_push(&g_grq, t);
}
/* Next thread for worker `wid`: its own queue, then the global queue, then steal
   one from another worker. Every 61st pick checks the global queue first so a
   worker that keeps refilling its own queue (e.g. a thread spawning in a loop)
   cannot starve globally-requeued (preempted / woken) work. */
static SP_TLS unsigned g_pick_tick = 0;
static sp_thread *sched_pick(int wid) {
  sp_thread *t;
  if ((++g_pick_tick % 61u) == 0) { t = runq_pop(&g_grq); if (t) return t; }
  t = runq_pop(&g_lrq[wid]);
  if (t) return t;
  t = runq_pop(&g_grq);
  if (t) return t;
#ifdef SP_THREADS
  for (int i = 0; i < g_nworkers; i++) {   /* steal one from a busier worker */
    if (i == wid) continue;
    /* Only an UNSTARTED thread may be stolen: a started one is pinned to its
       home worker's TLS (home_wid) and must not resume elsewhere. Unstarted
       spawns sit at most one-per-spawner ahead of pinned reruns, so scan the
       queue rather than popping blindly. */
    sp_thread **pp = &g_lrq[i].head;
    while (*pp && (*pp)->home_wid >= 0) pp = &(*pp)->rq_next;
    if (*pp) {
      t = *pp;
      *pp = t->rq_next;
      if (g_lrq[i].tail == t) {
        g_lrq[i].tail = NULL;
        for (sp_thread *w = g_lrq[i].head; w; w = w->rq_next) g_lrq[i].tail = w;
      }
      t->rq_next = NULL; g_runnable--;
      return t;
    }
  }
#endif
  return NULL;
}

/* Wake worker(s) after enqueueing t. A STARTED thread is pinned to its home
   worker (see home_wid) and sched_pick's stealing skips pinned threads -- yet
   every idle worker waits on the one g_sched_work condvar, so a plain signal
   could wake only a worker that cannot run t; it finds nothing and re-sleeps,
   and the wakeup is lost while t's home worker stays parked. Broadcast for
   pinned threads so the home worker always rechecks; an unstarted thread can
   run anywhere, so a single signal suffices. PRE: sched lock held. */
static void sp_sched_wake_for(sp_thread *t) {
  if (t->home_wid >= 0) SCHED_WAKE_ALL();
  else SCHED_WAKE();
}

static void reg_add(sp_thread *t) {
  t->all_prev = NULL; t->all_next = g_all;
  if (g_all) g_all->all_prev = t;
  g_all = t;
}
static void reg_remove(sp_thread *t) {
  if (t->all_prev) t->all_prev->all_next = t->all_next;
  else if (g_all == t) g_all = t->all_next;
  if (t->all_next) t->all_next->all_prev = t->all_prev;
  t->all_prev = t->all_next = NULL;
}

/* GC: root every live green thread (and thus its fiber, stack roots, and
   pending result) so a fire-and-forget thread with no user reference is not
   collected mid-run. Chained ahead of whatever globals hook was installed. */
static void (*g_prev_globals_hook)(void) = NULL;
static void sp_sched_globals_mark(void) {
  for (sp_thread *t = g_all; t; t = t->all_next) sp_gc_mark(t);
#ifdef SP_THREADS
  /* Mark each parked worker's published roots. Reaches the per-worker root fibers
     (idle/main workers) that are not on sp_fiber_list_head; green-thread fibers
     are also covered here (harmless re-mark) and via the suspended-fibers hook. */
  for (int i = 0; i < g_n_parked_fiber; i++) sp_fiber_mark_roots(g_parked_fiber[i]);
#endif
  if (g_prev_globals_hook) g_prev_globals_hook();
}

#ifdef SP_THREADS
static void sp_sched_start_workers(void);   /* defined after run_thread_once */
#endif

void sp_sched_init(void) {
  /* Called from main() before any fiber/thread op. Adopt this OS thread (worker
     0) as the main green thread: its native stack is the per-worker root fiber. */
  sp_fiber_worker_init();
  memset(&g_main_thread, 0, sizeof g_main_thread);
  g_main_thread.fiber = NULL;
  g_main_thread.state = SP_TH_RUNNING;
  g_main_thread.report_on_exception = 1;
  g_main_thread.arg = sp_box_nil();
  g_main_thread.retval = sp_box_nil();
  g_main_thread.name = sp_box_nil();
  g_current = &g_main_thread;
  g_prev_globals_hook = sp_gc_mark_globals_hook;
  sp_gc_mark_globals_hook = sp_sched_globals_mark;
#ifdef SP_THREADS
  g_worker_id = 0;                          /* main is worker 0 */
  g_wslot[0].tid = pthread_self();
  g_wslot[0].active = 1;
  sp_sched_start_workers();   /* spawn N-1 helper OS workers (see below) */
#endif
}

static void sp_thread_report(sp_thread *t) {
  fprintf(stderr, "#<Thread:%u> terminated with exception: %s (%s)\n",
          t->id, t->exc_msg ? t->exc_msg : "", t->exc_cls ? t->exc_cls : "Exception");
}

/* Park/wake primitives (defined below; used by join here). */
static void       sp_sched_block(sp_thread **waitlist);
static sp_thread *sp_sched_wake_one(sp_thread **waitlist);

/* A finished thread's parked joiners become runnable again (the main thread,
   if it was waiting, is released by the pump's target check, not the queue). */
static void sp_thread_wake_joiners(sp_thread *t) {
  while (sp_sched_wake_one(&t->joiners)) { }
}

/* Run runnable green threads until `target` is DEAD, until the main thread is
   woken back to RUNNABLE (it had blocked on a primitive), or until the run queue
   drains. Runs on the root fiber (the main thread). */
/* Run thread t for one timeslice: transfer into its fiber until it yields back
   (parked on a wait-list, or re-queued itself via Thread.pass) or its body
   returns. On termination, publish the result/exception and wake joiners. */
/* Quiescence: nothing left to run anywhere. A main thread parked in its pump
   (#join / drain) waits on exactly this, so the worker that empties the last of
   the work wakes it. Counting RUNNING workers (not idle ones) makes this robust:
   a worker that merely wakes spuriously and re-idles never touches g_nrunning,
   so it cannot momentarily perturb the predicate the way an idle count would. */
static void sp_sched_signal_if_quiescent(void) {
  if (g_nrunning == 0 && g_runnable == 0) SCHED_WAKE_ALL();
}

static void run_thread_once(sp_thread *t) {   /* PRE/POST: sched lock held */
  sp_thread *saved = g_current;
  g_current = t;
  g_nrunning++;
  t->state = SP_TH_RUNNING;
  t->off_cpu = 0;        /* on-cpu now: no other worker may pick it up */
  if (t->home_wid < 0) t->home_wid = (short)g_worker_id;   /* pin to this worker (TLS affinity) */
#ifdef SP_THREADS
  /* Publish to the monitor that this worker is now running t, and when -- it uses
     this to enforce the timeslice. Nudge the monitor if it is idle so it starts
     ticking. */
  g_wslot[g_worker_id].cur = t;
  g_wslot[g_worker_id].since = sp_monotonic_now();
  sp_sysmon_wake();
#endif
  int raised = 0;
  const char *ec = NULL, *em = NULL;
  void *eo = NULL;
  /* On the body's first entry the block's param reads the fiber's resumed
     value, so hand it Thread.new's argument then; later resumes pass nil. */
  sp_RbVal in = (t->fiber->state == 0) ? t->arg : sp_box_nil();
  /* Run the green thread with the lock dropped: it executes Ruby (and may park
     on, or wake, other threads, which re-take the lock themselves). */
  SCHED_UNLOCK();
  sp_Fiber_transfer_catch(t->fiber, in, &raised, &ec, &em, &eo);
  SCHED_LOCK();
  g_current = saved;
#ifdef SP_THREADS
  g_wslot[g_worker_id].cur = NULL;   /* no longer timing t on this worker */
  /* If the monitor flagged t but it yielded/blocked/died before reaching a poll,
     retire the request here so the flag does not stay stuck set. */
  if (t->preempt_request) { t->preempt_request = 0; g_npreempt--; sp_recompute_safepoint_flag(); }
#endif
  if (t->fiber->state == 3) {   /* the body returned (terminated) */
    t->retval = t->fiber->yielded_value;
    t->state = SP_TH_DEAD;
    int do_report = 0;
    if (raised) {
      t->has_exc = 1; t->exc_cls = ec; t->exc_msg = em; t->exc_obj = eo;
      do_report = t->report_on_exception;
    }
    sp_thread_wake_joiners(t);
    reg_remove(t);   /* collectable once no user reference remains */
    g_nrunning--;
    SCHED_WAKE_ALL();   /* wake a pump-waiting main (joining on this thread) and idle workers */
    if (do_report) { SCHED_UNLOCK(); sp_thread_report(t); SCHED_LOCK(); }
    return;
  }
  /* t yielded back and is now fully off its stack. Only NOW is it safe for
     another worker to run it, so this is where it (re-)enters the run queue:
     - BLOCKED: it parked on a wait list. If a waker raced in while it was still
       switching out, it deferred the enqueue to us (wake_pending); do it now.
     - otherwise (still RUNNING): it yielded via Thread.pass and wants to keep
       running, so requeue it. */
  t->off_cpu = 1;
  if (t->state == SP_TH_BLOCKED) {
    if (t->wake_pending) {
      t->wake_pending = 0;
      t->state = SP_TH_RUNNABLE;
      runq_requeue(t);
      SCHED_WAKE();
    }
  } else {
    /* Thread.pass / preempt: requeue. A started thread is pinned to its home
       worker (TLS affinity), so it lands on our own local queue TAIL -- other
       queued work still runs first, and the 61-tick global check keeps the
       global queue from starving. */
    runq_requeue(t);
    SCHED_WAKE();
  }
  g_nrunning--;
  sp_sched_signal_if_quiescent();   /* this thread blocked/passed; if nothing else runs, wake a waiting main */
}

#ifdef SP_THREADS
/* Cooperative preemption point (called from sp_safepoint with the lock held). If
   the monitor flagged the running green thread as over its timeslice, yield to
   the worker root exactly as Thread.pass does for a spawned thread: stay RUNNING
   so run_thread_once requeues us at the tail and the worker runs a sibling. The
   main thread is never flagged (the monitor only times green threads it runs via
   run_thread_once), so this only ever preempts a spawned thread. PRE/POST: lock
   held. */
static void sp_safepoint_preempt(void) {
  sp_thread *self = g_current;
  if (!self || self == &g_main_thread || !self->preempt_request) return;
  self->preempt_request = 0;
  g_npreempt--;
  sp_recompute_safepoint_flag();
  SCHED_UNLOCK();
  sp_Fiber_transfer(sp_fiber_worker_root(), sp_box_nil());
  sp_fiber_fire_inject_if_pending();   /* a #kill/#raise delivered while we were off-cpu */
  SCHED_LOCK();
}
#endif

/* Run runnable green threads on this (the main) worker. Returns when `target`
   dies, when the main thread is woken back to RUNNABLE, or when the run queue is
   empty. When may_wait is set and a helper worker is still busy (so it may yet
   enqueue work or wake us), block on g_sched_work instead of returning on an
   empty queue -- otherwise main would falsely declare a deadlock while a helper
   runs the very thread that will wake it. At N=1 g_nrunning is 0 between runs, so
   this returns on an empty queue exactly as before. PRE/POST: sched lock held. */
static void sp_sched_pump(sp_thread *target, int may_wait) {
  for (;;) {
#ifdef SP_THREADS
    if (g_stw_active) { sp_stw_park_locked(); continue; }   /* park main through STW too */
#endif
    if (target && target->state == SP_TH_DEAD) return;
    /* main blocked on a Queue/Mutex and a runnable thread just woke it */
    if (g_main_thread.state == SP_TH_RUNNABLE) { g_main_thread.state = SP_TH_RUNNING; return; }
    /* Run a green thread on the main worker only at N=1. With helpers present,
       main does NOT pump general green threads: running one means transferring
       off the root and back, which leaves the root fiber's published top-level
       roots a stale snapshot a collector could later mark. Main instead waits
       and lets a helper run the work. At N=1 there are no helpers, so it must
       pump. EXCEPTION: a thread pinned to main's worker (home_wid 0 -- it first
       ran here via a Thread.pass sweep, sp_sched_pass) can run NOWHERE else:
       helpers must not resume it (TLS affinity) and stealing skips it. If one
       is requeued while main pumps (a #kill/#raise or Queue wake unblocked it),
       main is the only worker that can finish it, so run it here -- otherwise
       the pump and that thread deadlock waiting on each other. */
#ifdef SP_THREADS
    if (g_nworkers > 1) {
      sp_thread *t = runq_pop(&g_lrq[0]);
      if (t) { run_thread_once(t); continue; }
    }
    else
#endif
    {
      sp_thread *t = sched_pick(g_worker_id);
      if (t) { run_thread_once(t); continue; }
    }
#ifdef SP_THREADS
    /* Queue empty for us. Wait while a helper is still running a green thread or
       work sits in the queue (a helper will pick it up) -- it may enqueue more or
       wake us; the worker that drops g_nrunning to zero with an empty queue
       broadcasts (sp_sched_signal_if_quiescent). Only when nothing runs and the
       queue is empty do we fall through -- drained, or a deadlock the caller
       observes. */
    if (may_wait && (g_nrunning > 0 || g_runnable > 0 || g_sleepers || g_io_waiters)) {
      pthread_cond_wait(&g_sched_work, &g_sched_lock);
      continue;
    }
#else
    (void)may_wait;
#endif
    return;
  }
}

/* One round-robin sweep for Thread.pass from the main thread: give each thread
   that is runnable *right now* exactly one timeslice, then return to main.
   Threads that re-queue themselves (their own Thread.pass) during the sweep land
   after the snapshot boundary and wait for the next sweep, so a sibling looping
   on Thread.pass cannot starve main (which would happen if main drained the
   queue here). */
static void sp_sched_pass(void) {
  /* Snapshot the runnable count and run exactly that many: a thread that
     re-queues itself (its own Thread.pass) during the sweep lands beyond the
     snapshot and waits for the next sweep, so it cannot starve main. */
  int n = g_runnable;
  while (n-- > 0) {
    sp_thread *t = sched_pick(g_worker_id);
    if (!t) return;
    run_thread_once(t);
  }
}

static void sp_thread_scan(void *p) {
  sp_thread *t = (sp_thread *)p;
  if (t->fiber) sp_gc_mark(t->fiber);
  sp_mark_rbval(t->arg);
  sp_mark_rbval(t->retval);
  sp_mark_rbval(t->name);
  if (t->exc_obj) sp_gc_mark(t->exc_obj);
  if (t->tls) sp_gc_mark(t->tls);
}

sp_thread *sp_Thread_spawn_fiber(sp_Fiber *f, sp_RbVal arg) {
  SP_GC_ROOT(f);   /* root the freshly-built fiber across the allocation below */
  SP_GC_ROOT_RBVAL(arg);
  sp_thread *volatile t = (sp_thread *)sp_gc_alloc(sizeof(sp_thread), NULL, sp_thread_scan);
  memset(t, 0, sizeof *t);
  t->home_wid = -1;              /* not yet started: any worker may pick it up */
  t->fiber = f;
  t->arg = arg;
  t->retval = sp_box_nil();
  t->name = sp_box_nil();
  t->report_on_exception = g_report_default;
  SCHED_LOCK();
  t->id = g_next_id++;
  reg_add(t);
  /* Prefer the spawning worker's local queue (locality) -- but only when that
     worker actually drains it. Main does not run green threads at N>1, so a
     thread it spawns would starve in its local queue (stealing only kicks in
     when the global queue is empty); send those to the global queue. */
#ifdef SP_THREADS
  if (g_current == &g_main_thread && g_nworkers > 1) rq_push(t);
  else
#endif
  lrq_push(g_worker_id, t);
  SCHED_WAKE();   /* a helper worker may be idle: hand it the new thread */
  SCHED_UNLOCK();
  return t;
}

/* Block the calling thread until `t` is dead. The main thread pumps the queue;
   a spawned thread parks on t's joiners and yields to the scheduler. */
static void sp_thread_await(sp_thread *t) {
  SCHED_LOCK();
  if (t->state == SP_TH_DEAD) { SCHED_UNLOCK(); return; }
  sp_thread *self = g_current;
  if (self == &g_main_thread) {
    sp_sched_pump(t, 1);
    int dead = (t->state == SP_TH_DEAD);
    SCHED_UNLOCK();
    if (!dead) sp_raise_cls("ThreadError", "deadlock detected: no runnable thread");
  } else {
    sp_sched_block(&t->joiners);   /* parks on t's joiners; resumes once t is dead */
    SCHED_UNLOCK();
  }
}

/* CRuby: #join and #value re-raise the thread's unhandled exception in the
   joining thread. */
static void sp_thread_reraise_if_exc(sp_thread *t) {
  if (t->has_exc) sp_fiber_reraise(t->exc_cls, t->exc_msg, t->exc_obj);
}

sp_thread *sp_Thread_join(sp_thread *t) {
  sp_thread_await(t);
  sp_thread_reraise_if_exc(t);
  return t;
}

sp_RbVal sp_Thread_value(sp_thread *t) {
  sp_thread_await(t);
  sp_thread_reraise_if_exc(t);
  return t->retval;
}

void sp_Thread_pass(void) {
  SCHED_LOCK();
  sp_thread *self = g_current;
  if (self == &g_main_thread) {
    sp_sched_pass();   /* one round-robin sweep, then main resumes (not a drain) */
    SCHED_UNLOCK();
  } else {
    /* Yield but stay runnable. Do NOT enqueue ourselves here: a second worker
       could pop and run our fiber while we are still mid-context-switch. We keep
       our state RUNNING and transfer to our worker's root; run_thread_once
       requeues us once we are fully off-cpu. */
    SCHED_UNLOCK();
    sp_Fiber_transfer(sp_fiber_worker_root(), sp_box_nil());
    sp_fiber_fire_inject_if_pending();   /* a #kill/#raise delivered while paused */
  }
}

sp_thread *sp_Thread_current(void) { return g_current; }

mrb_bool sp_Thread_alive(sp_thread *t) { return t->state != SP_TH_DEAD; }

/* Thread.report_on_exception=(v): set the default for threads spawned after.
   Thread.report_on_exception: read the default. Per-thread #report_on_exception
   reads/sets the thread's own flag. */
mrb_bool sp_Thread_set_report_default(mrb_bool v) { g_report_default = v ? 1 : 0; return v; }
mrb_bool sp_Thread_get_report_default(void) { return g_report_default; }
mrb_bool sp_Thread_set_report(sp_thread *t, mrb_bool v) { t->report_on_exception = v ? 1 : 0; return v; }
mrb_bool sp_Thread_get_report(sp_thread *t) { return t->report_on_exception; }

sp_thread *sp_Thread_main(void) { return &g_main_thread; }

sp_RbVal sp_Thread_get_name(sp_thread *t) { return t->name; }
sp_RbVal sp_Thread_set_name(sp_thread *t, sp_RbVal v) { t->name = v; return v; }

/* Thread.list enumeration: the main thread followed by every live spawned
   thread (dead ones are off the registry). The generated TU builds the array
   over these accessors since it owns sp_PolyArray. */
mrb_int sp_Thread_list_count(void) {
  mrb_int n = 1;   /* the main thread */
  for (sp_thread *t = g_all; t; t = t->all_next) n++;
  return n;
}
sp_thread *sp_Thread_list_at(mrb_int i) {
  if (i <= 0) return &g_main_thread;
  sp_thread *t = g_all;
  for (mrb_int k = 1; t && k < i; k++) t = t->all_next;
  return t ? t : &g_main_thread;
}

/* #status: "run" while runnable/running, "sleep" while blocked, false when it
   finished normally, nil when it died with an unhandled exception. */
sp_RbVal sp_Thread_status(sp_thread *t) {
  switch (t->state) {
    case SP_TH_RUNNING: case SP_TH_RUNNABLE: return sp_box_str(&("\xff" "run")[1]);
    case SP_TH_BLOCKED:                       return sp_box_str(&("\xff" "sleep")[1]);
    default:                                  return t->has_exc ? sp_box_nil() : sp_box_bool(0);
  }
}

/* ---- thread-local storage (Thread#[] / #[]=), a small sym->value map ---- */
typedef struct { sp_sym *keys; sp_RbVal *vals; mrb_int len, cap; } sp_tls_map;
static void sp_tls_scan(void *p) { sp_tls_map *m = (sp_tls_map *)p; for (mrb_int i = 0; i < m->len; i++) sp_mark_rbval(m->vals[i]); }
static void sp_tls_fin(void *p)  { sp_tls_map *m = (sp_tls_map *)p; free(m->keys); free(m->vals); }

sp_RbVal sp_Thread_tls_get(sp_thread *t, sp_sym k) {
  sp_tls_map *m = (sp_tls_map *)t->tls;
  if (m) for (mrb_int i = 0; i < m->len; i++) if (m->keys[i] == k) return m->vals[i];
  return sp_box_nil();
}
mrb_bool sp_Thread_tls_key(sp_thread *t, sp_sym k) {
  sp_tls_map *m = (sp_tls_map *)t->tls;
  if (m) for (mrb_int i = 0; i < m->len; i++) if (m->keys[i] == k) return 1;
  return 0;
}
sp_RbVal sp_Thread_tls_set(sp_thread *t, sp_sym k, sp_RbVal v) {
  sp_tls_map *m = (sp_tls_map *)t->tls;
  if (m) for (mrb_int i = 0; i < m->len; i++) if (m->keys[i] == k) { m->vals[i] = v; return v; }
  if (!m) {
    SP_GC_ROOT(t); SP_GC_ROOT_RBVAL(v);
    m = (sp_tls_map *)sp_gc_alloc(sizeof(sp_tls_map), sp_tls_fin, sp_tls_scan);
    m->cap = 4; m->len = 0;
    m->keys = (sp_sym *)malloc(sizeof(sp_sym) * m->cap);
    m->vals = (sp_RbVal *)malloc(sizeof(sp_RbVal) * m->cap);
    if (!m->keys || !m->vals) sp_raise_cls("NoMemoryError", "failed to allocate thread storage");
    t->tls = m;
  }
  if (m->len == m->cap) {
    mrb_int nc = m->cap * 2;
    sp_sym *nk = (sp_sym *)realloc(m->keys, sizeof(sp_sym) * nc);
    if (!nk) sp_raise_cls("NoMemoryError", "failed to grow thread storage");
    m->keys = nk;
    sp_RbVal *nv = (sp_RbVal *)realloc(m->vals, sizeof(sp_RbVal) * nc);
    if (!nv) sp_raise_cls("NoMemoryError", "failed to grow thread storage");
    m->vals = nv; m->cap = nc;
  }
  m->keys[m->len] = k; m->vals[m->len] = v; m->len++;
  return v;
}

#ifdef SP_THREADS
/* ---- helper OS workers (design 3.2, Appendix B) ---- */
static pthread_t g_worker_threads[SP_MAX_WORKERS];

/* min(online cores, SPINEL_WORKERS); the env var overrides the autodetect. */
static int sp_worker_count(void) {
  const char *e = getenv("SPINEL_WORKERS");
  int n;
  if (e && *e) { n = atoi(e); if (n < 1) n = 1; }
  else { long c = sysconf(_SC_NPROCESSORS_ONLN); n = (c > 0) ? (int)c : 1; }
  if (n > SP_MAX_WORKERS) n = SP_MAX_WORKERS;
  return n;
}

/* The monitor thread (sysmon, design §5). Two jobs, both off the workers' backs:
   (1) wake Kernel#sleep sleepers when their deadline passes, and (2) enforce the
   timeslice -- when a worker has run the same green thread past the quantum, flag
   it for preemption and SIGURG the worker so it yields at its next safepoint. It
   idles on g_sysmon_cv when nothing sleeps and no worker runs a green thread, and
   otherwise ticks on a short nanosleep. It never participates in GC. */
static void *sp_sysmon_main(void *arg) {
  (void)arg;
  /* The monitor must never field a preemption signal itself. */
  sigset_t blk; sigemptyset(&blk); sigaddset(&blk, g_preempt_sig);
  pthread_sigmask(SIG_BLOCK, &blk, NULL);
  SCHED_LOCK();
  for (;;) {
    if (g_shutdown) break;
    /* Stay out of the scheduler state while a collection has the world stopped:
       the monitor is not a GC participant (it holds no roots) but it must not
       move threads between lists concurrently with the collector. */
    if (g_stw_active) { pthread_cond_wait(&g_stw_release, &g_sched_lock); continue; }
    double now = sp_monotonic_now();
    double nearest = 0.0;
    for (sp_thread **pp = &g_sleepers; *pp; ) {
      sp_thread *t = *pp;
      if (t->wake_deadline <= now) {
        *pp = t->wait_next; t->wait_next = NULL; t->wait_head = NULL;
        if (t == &g_main_thread) { t->state = SP_TH_RUNNABLE; SCHED_WAKE_ALL(); }  /* must reach main, not a helper */
        else if (t->off_cpu) { t->state = SP_TH_RUNNABLE; runq_requeue(t); sp_sched_wake_for(t); }
        else t->wake_pending = 1;   /* mid-switch; its worker enqueues it (run_thread_once) */
      } else {
        if (nearest == 0.0 || t->wake_deadline < nearest) nearest = t->wake_deadline;
        pp = &t->wait_next;
      }
    }
    /* Timeslice enforcement: flag any worker over the quantum, once per slice. */
    int busy = 0;
    for (int i = 0; i < g_nworkers; i++) {
      sp_thread *r = g_wslot[i].active ? g_wslot[i].cur : NULL;
      if (!r) continue;
      busy = 1;
      if (!r->preempt_request && (now - g_wslot[i].since) >= SP_PREEMPT_QUANTUM) {
        r->preempt_request = 1;
        g_npreempt++;
        sp_recompute_safepoint_flag();
        pthread_kill(g_wslot[i].tid, g_preempt_sig);   /* nudge it to its next safepoint poll */
      }
    }
    /* Build the I/O poll set: slot 0 is the wake pipe (a registering thread
       writes a byte to break us out of poll early), the rest are parked fds. */
    int npf = 1;
    for (sp_thread *w = g_io_waiters; w; w = w->wait_next) {
      if (npf >= g_pcap) {
        int nc = g_pcap ? g_pcap * 2 : 16;
        struct pollfd *np = (struct pollfd *)realloc(g_pfds, sizeof(struct pollfd) * nc);
        sp_thread **nh = (sp_thread **)realloc(g_pths, sizeof(sp_thread *) * nc);
        if (np) g_pfds = np; if (nh) g_pths = nh;
        if (!np || !nh) break;
        g_pcap = nc;
      }
      g_pfds[npf].fd = w->io_fd; g_pfds[npf].events = w->io_events; g_pfds[npf].revents = 0;
      g_pths[npf] = w; npf++;
    }
    if (g_pcap < 1) {   /* ensure room for slot 0 even with no I/O waiters */
      g_pfds = (struct pollfd *)realloc(g_pfds, sizeof(struct pollfd) * 16);
      g_pths = (sp_thread **)realloc(g_pths, sizeof(sp_thread *) * 16);
      if (g_pfds && g_pths) g_pcap = 16;
    }
    int have_io = (npf > 1);
    if (nearest == 0.0 && !busy && !have_io) {
      /* nothing to time or watch: sleep until a thread sleeps / waits on I/O /
         is picked up by a worker (a registrant signals g_sysmon_cv). */
      g_sysmon_idle = 1;
      pthread_cond_wait(&g_sysmon_cv, &g_sched_lock);
      g_sysmon_idle = 0;
    } else {
      /* Poll the parked fds, timing out at the quantum (while preempting), near
         the nearest sleeper deadline, or 50ms otherwise (poll returns earlier on
         fd activity or a wake-pipe byte). */
      double dt = busy ? SP_PREEMPT_TICK : (nearest != 0.0 ? nearest - now : 0.05);
      if (nearest != 0.0 && nearest - now < dt) dt = nearest - now;
      if (dt > 0.05) dt = 0.05;
      if (dt < 0.0005) dt = 0.0005;
      int tmo = (int)(dt * 1000.0); if (tmo < 1) tmo = 1;
      if (!g_pfds) {   /* allocation failed: degrade to a plain timed wait */
        SCHED_UNLOCK();
        struct timespec req = { (time_t)dt, (long)((dt - (time_t)dt) * 1e9) };
        nanosleep(&req, NULL);
        SCHED_LOCK();
        continue;
      }
      g_pfds[0].fd = g_sysmon_pipe[0]; g_pfds[0].events = POLLIN; g_pfds[0].revents = 0;
      SCHED_UNLOCK();
      int pr = poll(g_pfds, (nfds_t)npf, tmo);
      SCHED_LOCK();
      if (g_pfds[0].revents & POLLIN) {   /* drain the wake pipe */
        char buf[64]; while (read(g_sysmon_pipe[0], buf, sizeof buf) > 0) {}
      }
      if (pr > 0) {
        for (int i = 1; i < npf; i++) {
          if (!g_pfds[i].revents) continue;
          sp_thread *t = g_pths[i];
          if (t->wait_head != &g_io_waiters) continue;   /* unparked meanwhile (e.g. #kill) */
          for (sp_thread **pp = &g_io_waiters; *pp; pp = &(*pp)->wait_next)
            if (*pp == t) { *pp = t->wait_next; break; }
          t->wait_next = NULL; t->wait_head = NULL; t->io_revents = g_pfds[i].revents; t->io_fd = -1;
          if (t == &g_main_thread) { t->state = SP_TH_RUNNABLE; SCHED_WAKE_ALL(); }
          else if (t->off_cpu) { t->state = SP_TH_RUNNABLE; runq_requeue(t); sp_sched_wake_for(t); }
          else t->wake_pending = 1;
        }
      }
    }
  }
  SCHED_UNLOCK();
  return NULL;
}

void sp_sched_sleep(double seconds) {
  if (!(seconds > 0.0)) return;
  if (!g_sysmon_started) {   /* the monitor was not started: plain blocking sleep */
    struct timespec req; req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1e9);
    if (req.tv_nsec < 0) req.tv_nsec = 0; if (req.tv_nsec >= 1000000000L) req.tv_nsec = 999999999L;
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {}
    return;
  }
  SCHED_LOCK();
  sp_thread *self = g_current;
  /* Same pending-inject check as sp_sched_block: a #kill/#raise that arrived
     while we were RUNNING must fire now, not after the full sleep. */
  if (self != &g_main_thread && self->fiber && sp_fiber_inject_pending(self->fiber)) {
    SCHED_UNLOCK();
    sp_fiber_fire_inject_if_pending();   /* raises; does not return */
    SCHED_LOCK();
  }
  self->wake_deadline = sp_monotonic_now() + seconds;
  self->state = SP_TH_BLOCKED;
  self->off_cpu = 0;
  self->wake_pending = 0;
  self->wait_next = g_sleepers; self->wait_head = &g_sleepers; g_sleepers = self;
  sp_sysmon_wake();   /* let the monitor recompute its timeout */
  if (self == &g_main_thread) {
    sp_sched_pump(NULL, 1);   /* main waits (and pumps at N=1) until the monitor wakes it */
    SCHED_UNLOCK();
  } else {
    /* Same exception-context snapshot as sp_sched_block: the symmetric transfer
       clobbers our handler stack on resume. */
    void *exc_snap = sp_exc_ctx_new();
    sp_exc_ctx_save(exc_snap);
    SCHED_UNLOCK();
    sp_Fiber_transfer(sp_fiber_worker_root(), sp_box_nil());
    sp_exc_ctx_load(exc_snap);
    sp_exc_ctx_free(exc_snap);
    sp_fiber_fire_inject_if_pending();   /* a #kill/#raise delivered while sleeping */
  }
}

int sp_sched_wait_io(int fd, short events) {
  if (fd < 0 || !g_sysmon_started) {   /* no monitor: plain blocking single-fd poll */
    struct pollfd pf; pf.fd = fd; pf.events = events; pf.revents = 0;
    for (;;) { int pr = poll(&pf, 1, 1000); if (pr > 0) return 1; if (pr == 0 || errno == EINTR) continue; return 0; }
  }
  SCHED_LOCK();
  sp_thread *self = g_current;
  /* Same pending-inject check as sp_sched_block. */
  if (self != &g_main_thread && self->fiber && sp_fiber_inject_pending(self->fiber)) {
    SCHED_UNLOCK();
    sp_fiber_fire_inject_if_pending();   /* raises; does not return */
    SCHED_LOCK();
  }
  self->io_fd = fd; self->io_events = events; self->io_revents = 0;
  self->state = SP_TH_BLOCKED;
  self->off_cpu = 0;
  self->wake_pending = 0;
  self->wait_next = g_io_waiters; self->wait_head = &g_io_waiters; g_io_waiters = self;
  sp_sysmon_wake();   /* let the monitor rebuild its poll set */
  if (self == &g_main_thread) {
    sp_sched_pump(NULL, 1);   /* main waits (and pumps at N=1) until the monitor wakes it */
    int rev = self->io_revents; self->io_revents = 0; self->io_fd = -1;
    SCHED_UNLOCK();
    return rev ? 1 : 0;
  }
  /* Same exception-context snapshot as sp_sched_block/sleep: the symmetric
     transfer clobbers our handler stack on resume. */
  void *exc_snap = sp_exc_ctx_new();
  sp_exc_ctx_save(exc_snap);
  SCHED_UNLOCK();
  sp_Fiber_transfer(sp_fiber_worker_root(), sp_box_nil());
  sp_exc_ctx_load(exc_snap);
  sp_exc_ctx_free(exc_snap);
  sp_fiber_fire_inject_if_pending();   /* a #kill/#raise delivered while waiting on I/O */
  int rev = self->io_revents; self->io_revents = 0; self->io_fd = -1;
  return rev ? 1 : 0;
}

/* A helper worker: adopt its native stack as a per-worker root fiber, then pull
   runnable green threads off the GRQ forever. It parks at the GC barrier when a
   collection is in progress and exits when main signals shutdown at drain. */
static void *sp_worker_main(void *arg) {
  int wid = (int)(intptr_t)arg;
  g_worker_id = wid;          /* TLS: read only by this worker */
  sp_fiber_worker_init();
  SCHED_LOCK();
  g_wslot[wid].tid = pthread_self();   /* publish under the lock; the monitor reads it there */
  g_wslot[wid].active = 1;
  for (;;) {
    if (g_stw_active) { sp_stw_park_locked(); continue; }
    if (g_shutdown) break;
    sp_thread *t = sched_pick(wid);   /* own queue, then global, then steal */
    if (t) { run_thread_once(t); continue; }  /* run_thread_once signals quiescence on the last one */
    pthread_cond_wait(&g_sched_work, &g_sched_lock);   /* idle; woken by an enqueue (SCHED_WAKE) or shutdown */
  }
  SCHED_UNLOCK();
  return NULL;
}

#ifndef NSIG
#define NSIG 65
#endif

/* Resolve SPINEL_PREEMPT_SIGNAL to the signal the monitor sends. Accepts a number
   (so real-time signals work, e.g. `kill -l SIGRTMIN`) or a name with or without
   the SIG prefix (URG/USR1/USR2/IO/WINCH). The default, SIGURG, is chosen because
   real programs essentially never use it (its nominal job is TCP out-of-band data)
   and its default disposition is "ignore", so it is safe to repurpose; override it
   only if the program itself needs SIGURG, or wants a real-time signal. An
   unrecognized or uncatchable value warns and falls back to SIGURG. */
static int sp_resolve_preempt_signal(void) {
  const char *e = getenv("SPINEL_PREEMPT_SIGNAL");
  if (!e || !*e) return SIGURG;
  char *end;
  long n = strtol(e, &end, 10);
  if (*end == '\0') {
    if (n > 0 && n < NSIG) return (int)n;
  } else {
    const char *name = e;
    if (strncasecmp(name, "SIG", 3) == 0) name += 3;
    static const struct { const char *n; int s; } tab[] = {
      { "URG", SIGURG }, { "USR1", SIGUSR1 }, { "USR2", SIGUSR2 },
      { "IO", SIGIO }, { "WINCH", SIGWINCH },
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
      if (strcasecmp(name, tab[i].n) == 0) return tab[i].s;
  }
  fprintf(stderr, "spinel: ignoring unrecognized SPINEL_PREEMPT_SIGNAL=%s; using SIGURG\n", e);
  return SIGURG;
}

static void sp_sched_start_workers(void) {
  /* Spawn the monitor thread (Kernel#sleep wakeups) first and set g_sysmon_started
     before any worker, so a green thread reading the flag in sleep never races
     its write (pthread_create of the workers is the happens-before edge). The
     monitor idles on g_sysmon_cv until a thread sleeps; if it fails to spawn,
     sleep falls back to a plain blocking nanosleep. */
  /* Fix the worker count before spawning anything, so the monitor and helpers
     read it through the pthread_create happens-before edge (no lock needed). */
  g_nworkers = sp_worker_count();
  /* Install the preemption signal handler before any worker can be targeted.
     SA_RESTART so an in-flight library syscall resumes rather than failing with
     EINTR -- the yield itself is cooperative (at the next safepoint poll), the
     signal only nudges the worker there. Resolve and pin g_preempt_sig here,
     before the monitor reads it through the pthread_create edge. */
  g_preempt_sig = sp_resolve_preempt_signal();
  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_handler = sp_preempt_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(g_preempt_sig, &sa, NULL) != 0) {   /* uncatchable signal: fall back */
    fprintf(stderr, "spinel: SPINEL_PREEMPT_SIGNAL=%d cannot be caught; using SIGURG\n", g_preempt_sig);
    g_preempt_sig = SIGURG;
    sigaction(g_preempt_sig, &sa, NULL);
  }
  /* Self-pipe so a thread registering for sleep/I/O can break the monitor out of
     poll() immediately. Both ends non-blocking: the writer never stalls on a full
     pipe (the bytes only signal, they are drained wholesale), the reader drains
     without blocking. Created before the monitor so it sees valid fds. */
  if (pipe(g_sysmon_pipe) == 0) {
    for (int e = 0; e < 2; e++) { int fl = fcntl(g_sysmon_pipe[e], F_GETFL, 0); if (fl >= 0) fcntl(g_sysmon_pipe[e], F_SETFL, fl | O_NONBLOCK); }
  } else { g_sysmon_pipe[0] = g_sysmon_pipe[1] = -1; }
  if (pthread_create(&g_sysmon, NULL, sp_sysmon_main, NULL) == 0) g_sysmon_started = 1;
  for (int i = 1; i < g_nworkers; i++)
    if (pthread_create(&g_worker_threads[i], NULL, sp_worker_main, (void *)(intptr_t)i) != 0) { g_nworkers = i; break; }
}
#endif

void sp_sched_drain(void) {
  /* main() is finishing: run remaining runnable threads so fire-and-forget side
     effects happen, then shut the helper workers down. Only the main thread
     drains. pump(NULL, 1) returns once the queue is empty and every helper is
     idle -- i.e. all runnable work is done (at N=1 it returns on an empty queue
     exactly as before). */
  if (g_current != &g_main_thread) return;
  SCHED_LOCK();
  sp_sched_pump(NULL, 1);
#ifdef SP_THREADS
  g_shutdown = 1;
  pthread_cond_broadcast(&g_sched_work);
  sp_sysmon_wake();   /* wake the monitor (idle or in poll) so it sees shutdown */
  int sysmon_running = g_sysmon_started;
  SCHED_UNLOCK();
  for (int i = 1; i < g_nworkers; i++) pthread_join(g_worker_threads[i], NULL);
  if (sysmon_running) pthread_join(g_sysmon, NULL);
  return;
#endif
  SCHED_UNLOCK();
}

/* ---- generic park / wake on a primitive wait list ---- */

/* Block the current green thread on `*waitlist` until a wake moves it back to
   runnable. The main thread pumps the scheduler (it cannot transfer away from
   root); a spawned thread transfers back to the scheduler hub. */
static void sp_sched_block(sp_thread **waitlist) {   /* PRE/POST: sched lock held */
  sp_thread *self = g_current;
  /* A #kill/#raise delivered while this thread was RUNNING left its inject
     pending on the fiber (sp_thread_deliver found no wait list to unpark).
     Parking now would sleep through it forever -- nobody will wake us. Fire it
     here instead (it raises out of the blocking call, ensures running). The
     deliver and this check both run under the sched lock, so no window. */
  if (self != &g_main_thread && self->fiber && sp_fiber_inject_pending(self->fiber)) {
    SCHED_UNLOCK();
    sp_fiber_fire_inject_if_pending();   /* raises; does not return */
    SCHED_LOCK();
  }
  self->state = SP_TH_BLOCKED;
  self->off_cpu = 0;         /* still on-cpu until our worker confirms the switch-out */
  self->wake_pending = 0;
  self->wait_next = *waitlist;
  self->wait_head = waitlist;   /* so #kill/#raise can unlink it */
  *waitlist = self;
  if (self == &g_main_thread) {
    sp_sched_pump(NULL, 1);   /* returns (lock held) once a waker marks main RUNNABLE */
    if (self->state != SP_TH_RUNNING) {
      SCHED_UNLOCK();
      sp_raise_cls("ThreadError", "deadlock detected: all threads blocked");
    }
  } else {
    /* The symmetric fiber transfer's exc bookkeeping clobbers this thread's
       handler stack when root resumes from the block, so snapshot it here and
       restore it on wake -- otherwise a #raise/#kill delivered while blocked
       would find an empty handler stack and escape unhandled. */
    void *exc_snap = sp_exc_ctx_new();
    sp_exc_ctx_save(exc_snap);
    SCHED_UNLOCK();   /* drop the lock across the transfer (we run no metadata while parked) */
    sp_Fiber_transfer(sp_fiber_worker_root(), sp_box_nil());
    sp_exc_ctx_load(exc_snap);
    sp_exc_ctx_free(exc_snap);
    /* resumed: a pending #kill/#raise fires here, in this thread's context (lock
       not held), so its ensure/rescue blocks unwind on its own stack. On the
       normal resume we re-take the lock so the caller continues holding it. */
    sp_fiber_fire_inject_if_pending();
    SCHED_LOCK();
  }
}

/* Move one thread off `*waitlist` back onto the run queue (or mark the main
   thread runnable so its pump returns). Returns the woken thread, or NULL. */
static sp_thread *sp_sched_wake_one(sp_thread **waitlist) {
  sp_thread *t = *waitlist;
  if (!t) return NULL;
  *waitlist = t->wait_next;
  t->wait_next = NULL;
  t->wait_head = NULL;
  if (t == &g_main_thread) { t->state = SP_TH_RUNNABLE; SCHED_WAKE_ALL(); return t; }  /* broadcast: a signal could wake a helper instead of main */
  if (t->off_cpu) { t->state = SP_TH_RUNNABLE; runq_requeue(t); }   /* fully parked: enqueue now */
  else { t->wake_pending = 1; }   /* still switching out: its worker enqueues it once off-cpu */
  sp_sched_wake_for(t);   /* wake an idle worker to run it (all of them if t is pinned) */
  return t;
}

/* Remove a parked thread from whatever wait list it sits on (for #kill/#raise). */
static void sp_sched_unpark(sp_thread *t) {
  if (!t->wait_head) return;
  for (sp_thread **pp = t->wait_head; *pp; pp = &(*pp)->wait_next)
    if (*pp == t) { *pp = t->wait_next; break; }
  t->wait_next = NULL;
  t->wait_head = NULL;
}

/* #kill / #raise: deliver an inject to the target so it terminates (running its
   ensures) or raises. The current thread acts on itself immediately; another
   thread gets the inject queued on its fiber and is made runnable, so the inject
   fires when the scheduler next runs it -- at body entry (never-run) or right
   after it unblocks (sp_sched_block / Thread.pass). */
static void sp_thread_deliver(sp_thread *t, int is_kill,
                              const char *cls, const char *msg, void *obj) {
  if (t == g_current) {
    if (t == &g_main_thread) return;   /* killing the main thread from itself: unsupported, no-op */
    if (is_kill) sp_fiber_raise_kill_self();   /* noreturn */
    sp_fiber_reraise(cls, msg, obj);           /* noreturn */
  }
  SCHED_LOCK();
  if (t->state == SP_TH_DEAD) { SCHED_UNLOCK(); return; }   /* raced with its death: no-op */
  /* #kill/#raise on the main thread from a sibling: main runs on the root
     fiber (t->fiber == NULL), which has no inject delivery points, so this is
     unsupported -- no-op rather than a NULL deref, matching the self-kill case
     above. (CRuby delivers to main; see the Thread row in docs/limitations.md.) */
  if (t == &g_main_thread || !t->fiber) { SCHED_UNLOCK(); return; }
  if (is_kill) sp_fiber_set_kill_inject(t->fiber);
  else sp_fiber_set_raise_inject(t->fiber, cls, msg, obj);
  if (t->state == SP_TH_BLOCKED) {
    sp_sched_unpark(t);
    /* runq_requeue, not rq_push: a STARTED thread is pinned to its home
       worker's TLS (home_wid) and must not resume on another worker. */
    if (t->off_cpu) { t->state = SP_TH_RUNNABLE; runq_requeue(t); sp_sched_wake_for(t); }
    else { t->wake_pending = 1; }   /* mid-switch: its worker enqueues it (see run_thread_once) */
  }
  /* RUNNABLE threads are already queued; the inject fires when they run. */
  SCHED_UNLOCK();
}

/* The DEAD check lives inside sp_thread_deliver, under the sched lock: t may be
   running on another worker and die concurrently, so an unlocked read here
   would race with run_thread_once's state write. */
sp_thread *sp_Thread_kill(sp_thread *t) {
  sp_thread_deliver(t, 1, NULL, NULL, NULL);
  return t;
}
sp_thread *sp_Thread_raise(sp_thread *t, const char *cls, const char *msg, void *obj) {
  sp_thread_deliver(t, 0, cls, msg, obj);
  return t;
}

/* ---- Queue ---- */

static void sp_queue_scan(void *p) {
  sp_queue *q = (sp_queue *)p;
  for (mrb_int i = 0; i < q->len; i++)
    sp_mark_rbval(q->buf[(q->head + i) % q->cap]);
}
static void sp_queue_fin(void *p) { sp_queue *q = (sp_queue *)p; free(q->buf); }

sp_queue *sp_Queue_new(void) {
  sp_queue *q = (sp_queue *)sp_gc_alloc(sizeof(sp_queue), sp_queue_fin, sp_queue_scan);
  q->cap = 8;
  q->head = q->len = 0;
  q->max = 0;
  q->pop_waiters = NULL;
  q->push_waiters = NULL;
  q->closed = 0;
  q->buf = (sp_RbVal *)malloc(sizeof(sp_RbVal) * q->cap);
  if (!q->buf) sp_raise_cls("NoMemoryError", "failed to allocate queue");
  return q;
}

sp_queue *sp_SizedQueue_new(mrb_int max) {
  if (max <= 0) sp_raise_cls("ArgumentError", "queue size must be positive");
  sp_queue *q = sp_Queue_new();
  q->max = max;
  return q;
}

void sp_Queue_push(sp_queue *q, sp_RbVal v) {
  /* On a full SizedQueue, block until a #pop frees a slot. Root v across the
     block: it lives in this (possibly suspended) frame, and the parking
     thread's saved roots only cover the shadow stack. */
  SP_GC_ROOT_RBVAL(v);
  SCHED_LOCK();
  for (;;) {
    if (q->closed) { SCHED_UNLOCK(); sp_raise_cls("ClosedQueueError", "queue closed"); }
    if (q->max <= 0 || q->len < q->max) break;
    sp_sched_block(&q->push_waiters);   /* releases+reacquires the lock around its transfer */
  }
  if (q->len == q->cap) {
    mrb_int nc = q->cap * 2;
    sp_RbVal *nb = (sp_RbVal *)malloc(sizeof(sp_RbVal) * nc);
    if (!nb) { SCHED_UNLOCK(); sp_raise_cls("NoMemoryError", "failed to grow queue"); }
    for (mrb_int i = 0; i < q->len; i++) nb[i] = q->buf[(q->head + i) % q->cap];
    free(q->buf);
    q->buf = nb; q->cap = nc; q->head = 0;
  }
  q->buf[(q->head + q->len) % q->cap] = v;
  q->len++;
  sp_sched_wake_one(&q->pop_waiters);   /* hand the new value to a waiting popper */
  SCHED_UNLOCK();
}

sp_RbVal sp_Queue_pop(sp_queue *q) {
  /* Block until an element is available. A closed, drained queue returns nil
     rather than blocking forever (CRuby behaviour). */
  SCHED_LOCK();
  while (q->len == 0) {
    if (q->closed) { SCHED_UNLOCK(); return sp_box_nil(); }
    sp_sched_block(&q->pop_waiters);
  }
  sp_RbVal v = q->buf[q->head];
  q->head = (q->head + 1) % q->cap;
  q->len--;
  if (q->max > 0) sp_sched_wake_one(&q->push_waiters);   /* a slot freed up */
  SCHED_UNLOCK();
  return v;
}

mrb_int  sp_Queue_size(sp_queue *q)   { SCHED_LOCK(); mrb_int n = q->len;       SCHED_UNLOCK(); return n; }
mrb_bool sp_Queue_empty(sp_queue *q)  { SCHED_LOCK(); mrb_bool e = q->len == 0;  SCHED_UNLOCK(); return e; }
mrb_int  sp_Queue_max(sp_queue *q)    { SCHED_LOCK(); mrb_int m = q->max;        SCHED_UNLOCK(); return m; }
mrb_bool sp_Queue_closed(sp_queue *q) { SCHED_LOCK(); mrb_bool c = q->closed != 0; SCHED_UNLOCK(); return c; }
void     sp_Queue_clear(sp_queue *q)  {
  SCHED_LOCK();
  q->head = q->len = 0;
  if (q->max > 0) while (sp_sched_wake_one(&q->push_waiters)) { }   /* all slots free */
  SCHED_UNLOCK();
}

void sp_Queue_close(sp_queue *q) {
  SCHED_LOCK();
  q->closed = 1;
  /* wake every blocked popper (they return nil) and pusher (they raise) */
  while (sp_sched_wake_one(&q->pop_waiters)) { }
  while (sp_sched_wake_one(&q->push_waiters)) { }
  SCHED_UNLOCK();
}

/* ---- Mutex ----
 * A non-recursive lock. At N=1 there is no preemption, so a Mutex only matters
 * across a yield: a green thread holding the lock blocks (Queue/CondVar/IO) and
 * another tries to acquire it. unlock hands ownership directly to the next
 * waiter, so the wakeup is a clean transfer (no re-contention). */

sp_mutex *sp_Mutex_new(void) {
  sp_mutex *m = (sp_mutex *)sp_gc_alloc(sizeof(sp_mutex), NULL, NULL);
  m->owner = NULL;
  m->waiters = NULL;
  return m;
}

void sp_Mutex_lock(sp_mutex *m) {
  SCHED_LOCK();
  sp_thread *self = g_current;
  if (m->owner == self) { SCHED_UNLOCK(); sp_raise_cls("ThreadError", "deadlock; recursive locking"); }
  if (m->owner == NULL) { m->owner = self; SCHED_UNLOCK(); return; }
  /* unlock hands ownership to us (sets m->owner) before waking us. */
  sp_sched_block(&m->waiters);
  SCHED_UNLOCK();
}

void sp_Mutex_unlock(sp_mutex *m) {
  SCHED_LOCK();
  if (m->owner != g_current) {
    SCHED_UNLOCK();
    sp_raise_cls("ThreadError", "Attempt to unlock a mutex which is not locked");
  }
  m->owner = sp_sched_wake_one(&m->waiters);   /* hand off, or NULL => unlocked */
  SCHED_UNLOCK();
}

mrb_bool sp_Mutex_try_lock(sp_mutex *m) {
  SCHED_LOCK();
  mrb_bool r;
  if (m->owner != NULL) r = 0;
  else { m->owner = g_current; r = 1; }
  SCHED_UNLOCK();
  return r;
}
mrb_bool sp_Mutex_locked(sp_mutex *m) { SCHED_LOCK(); mrb_bool r = m->owner != NULL;      SCHED_UNLOCK(); return r; }
mrb_bool sp_Mutex_owned(sp_mutex *m)  { SCHED_LOCK(); mrb_bool r = m->owner == g_current;  SCHED_UNLOCK(); return r; }

/* ---- ConditionVariable ----
 * #wait releases the mutex, parks on the CV, and re-acquires the mutex on
 * wake; #signal wakes one waiter, #broadcast wakes all. */

sp_condvar *sp_CondVar_new(void) {
  sp_condvar *cv = (sp_condvar *)sp_gc_alloc(sizeof(sp_condvar), NULL, NULL);
  cv->waiters = NULL;
  return cv;
}

void sp_CondVar_wait(sp_condvar *cv, sp_mutex *m) {
  /* Release the mutex and park on the CV atomically under the one lock, so a
     concurrent #signal cannot slip in between and be lost. The mutex unlock is
     inlined (its hand-off + ownership check) since sp_Mutex_unlock would take
     the lock again on its own. */
  SCHED_LOCK();
  if (m->owner != g_current) {
    SCHED_UNLOCK();
    sp_raise_cls("ThreadError", "Attempt to unlock a mutex which is not locked");
  }
  m->owner = sp_sched_wake_one(&m->waiters);   /* hand off the mutex, or NULL */
  sp_sched_block(&cv->waiters);                /* park (drops+retakes the lock) */
  SCHED_UNLOCK();
  sp_Mutex_lock(m);   /* re-acquire (may block again on the mutex) */
}

void sp_CondVar_signal(sp_condvar *cv)    { SCHED_LOCK(); sp_sched_wake_one(&cv->waiters);            SCHED_UNLOCK(); }
void sp_CondVar_broadcast(sp_condvar *cv) { SCHED_LOCK(); while (sp_sched_wake_one(&cv->waiters)) { }  SCHED_UNLOCK(); }
