/*
 * fmrb_gate.c - the blocking-call gate (fmrb_gate.h).
 *
 * Three parties, and what each may touch:
 *
 *   the waiting task   FreeRTOS context. Takes a slot, queues it, sleeps on
 *                      the slot's semaphore, frees the slot.
 *   a helper thread    outside FreeRTOS. Takes a queued slot, runs the call,
 *                      moves the slot to the done list. Only the queue, the
 *                      done list, a slot's state and the counters below,
 *                      all under g_lock.
 *   the poll hook      FreeRTOS context (the port calls it where it feeds the
 *                      ticks). Empties the done list and gives each waiter
 *                      its semaphore with the FromISR call, which never
 *                      switches tasks itself.
 *
 * The flags that only FreeRTOS context reads and writes (delivered, orphan,
 * waiter) need no lock: the cooperative port runs one task at a
 * time, and the hooks run on the task that is running.
 */

#define _GNU_SOURCE  /* pthread_setname_np */

#include "fmrb_gate.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "port_hosted.h"
#include "fmrb_app.h"
#include "fmrb_log.h"

static const char *TAG = "gate";

/* Jobs in flight at once (one per waiting task; the apps and the system
 * tasks together stay well under this). A call that finds no free slot is
 * made directly, as before the gate. */
#define GATE_MAX_SLOTS 64
/* Helper threads at most; one per job in flight is what a blocking call
 * needs, so the same number. */
#define GATE_MAX_HELPERS GATE_MAX_SLOTS
/* A helper with nothing to do for this long ends. */
#define GATE_HELPER_IDLE_MS 5000
/* How often a waiting task looks at its app's exit flag. */
#define GATE_EXIT_POLL_MS 50
/* After a cancel, how long to wait for an abandonable call before leaving it
 * to finish on its own. */
#define GATE_ABANDON_MS 200
/* After a delete, how long the deleter waits for a call that writes into the
 * deleted app's memory (it was shut down, so this is normally milliseconds). */
#define GATE_DELETE_WAIT_MS 5000

typedef enum {
    SLOT_FREE = 0,
    SLOT_QUEUED,
    SLOT_RUNNING,
    SLOT_FINISHED,
} slot_state_t;

typedef struct gate_slot {
    fmrb_gate_job_t job;
    slot_state_t state;           /* g_lock */
    struct gate_slot *next;       /* g_lock: queue or done list */
    TaskHandle_t waiter;          /* FreeRTOS context */
    SemaphoreHandle_t sem;        /* FreeRTOS context */
    bool delivered;               /* FreeRTOS context */
    bool orphan;                  /* FreeRTOS context */
} gate_slot_t;

static gate_slot_t g_slots[GATE_MAX_SLOTS];

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_finished = PTHREAD_COND_INITIALIZER;
static gate_slot_t *g_queue_head;
static gate_slot_t *g_queue_tail;
static gate_slot_t *g_done;
static unsigned g_helpers;
static unsigned g_helpers_idle;
/* Set by a helper when it adds to the done list, so the poll hook can skip
 * the lock on every context switch when there is nothing to hand over. */
static int g_done_pending;

static bool g_ready;
static void (*g_task_deleted)(void *task);
static unsigned g_calls;
static unsigned g_cancels;
static unsigned g_deleted;

/* ------------------------------------------------------------ helpers */

static void timespec_after_ms(struct timespec *ts, unsigned ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static void *helper_main(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&g_lock);
    for (;;) {
        while (g_queue_head == NULL) {
            struct timespec until;
            timespec_after_ms(&until, GATE_HELPER_IDLE_MS);
            g_helpers_idle++;
            int rc = pthread_cond_timedwait(&g_work, &g_lock, &until);
            g_helpers_idle--;
            if (rc == ETIMEDOUT && g_queue_head == NULL) {
                g_helpers--;
                pthread_mutex_unlock(&g_lock);
                return NULL;
            }
        }
        gate_slot_t *s = g_queue_head;
        g_queue_head = s->next;
        if (g_queue_head == NULL) {
            g_queue_tail = NULL;
        }
        s->next = NULL;
        s->state = SLOT_RUNNING;
        pthread_mutex_unlock(&g_lock);

        s->job.run(s->job.arg != NULL ? s->job.arg : (void *)s->job.scratch);

        pthread_mutex_lock(&g_lock);
        s->state = SLOT_FINISHED;
        s->next = g_done;
        g_done = s;
        __atomic_store_n(&g_done_pending, 1, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&g_finished);
    }
}

/* g_lock held. One more helper when every live one is busy. */
static void ensure_helper_locked(void)
{
    if (g_helpers_idle > 0 || g_helpers >= GATE_MAX_HELPERS) {
        return;
    }
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, helper_main, NULL) == 0) {
        g_helpers++;
        (void)pthread_setname_np(th, "fmrb-gate");
    }
    pthread_attr_destroy(&attr);
}

/* ------------------------------------------------- FreeRTOS context */

static void shutdown_fds(const fmrb_gate_job_t *job)
{
    for (int i = 0; i < job->nfds; i++) {
        if (job->fds[i] >= 0) {
            (void)shutdown(job->fds[i], SHUT_RDWR);
        }
    }
}

static void release_slot(gate_slot_t *s)
{
    if (s->orphan && s->job.discard != NULL) {
        s->job.discard(s->job.scratch);
    }
    pthread_mutex_lock(&g_lock);
    s->state = SLOT_FREE;
    pthread_mutex_unlock(&g_lock);
}

/* The port's poll hook: hand finished jobs to their waiters. */
static BaseType_t gate_poll_hook(void)
{
    if (__atomic_load_n(&g_done_pending, __ATOMIC_ACQUIRE) == 0) {
        return pdFALSE;
    }
    pthread_mutex_lock(&g_lock);
    gate_slot_t *list = g_done;
    g_done = NULL;
    __atomic_store_n(&g_done_pending, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_lock);

    BaseType_t woken = pdFALSE;
    while (list != NULL) {
        gate_slot_t *s = list;
        list = s->next;
        s->next = NULL;
        if (s->orphan) {
            release_slot(s);
            continue;
        }
        s->delivered = true;
        BaseType_t w = pdFALSE;
        (void)xSemaphoreGiveFromISR(s->sem, &w);
        if (w != pdFALSE) {
            woken = pdTRUE;
        }
    }
    return woken;
}

/* The port's delete hook: a task deleted while it waited (the forced kill).
 * Its memory is about to be released, so a call that writes into it must be
 * over first. Then the listener (the socket registry of net_wrap.c) closes
 * what the task left open, since its own cleanup never runs. */
static void gate_delete_hook(TaskHandle_t task)
{
    for (int i = 0; i < GATE_MAX_SLOTS; i++) {
        gate_slot_t *s = &g_slots[i];
        pthread_mutex_lock(&g_lock);
        const slot_state_t st = s->state;
        pthread_mutex_unlock(&g_lock);
        if (st == SLOT_FREE || s->waiter != task || s->orphan) {
            continue;
        }
        g_deleted++;
        if (s->delivered) {
            /* Finished, but the task never got to collect it. */
            release_slot(s);
            continue;
        }
        s->orphan = true;
        shutdown_fds(&s->job);
        if (s->job.discard != NULL) {
            continue;  /* holds only memory of its own; freed when done */
        }
        pthread_mutex_lock(&g_lock);
        struct timespec until;
        timespec_after_ms(&until, GATE_DELETE_WAIT_MS);
        int rc = 0;
        while (s->state != SLOT_FINISHED && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&g_finished, &g_lock, &until);
        }
        const bool finished = s->state == SLOT_FINISHED;
        pthread_mutex_unlock(&g_lock);
        if (!finished) {
            FMRB_LOGW(TAG, "a call of a deleted task is still running after %u ms",
                      (unsigned)GATE_DELETE_WAIT_MS);
        }
    }
    if (g_task_deleted != NULL) {
        g_task_deleted(task);
    }
}

void fmrb_gate_set_task_deleted_listener(void (*fn)(void *task))
{
    g_task_deleted = fn;
}

static bool exit_requested(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx != NULL && ctx->should_exit;
}

void fmrb_gate_init(void)
{
    if (g_ready) {
        return;
    }
    vPortHostedSetPollHook(gate_poll_hook);
    vPortHostedSetDeleteHook(gate_delete_hook);
    g_ready = true;
}

bool fmrb_gate_usable(void)
{
    return g_ready && xPortHostedTaskMayBlock() != pdFALSE;
}

static gate_slot_t *take_slot(void)
{
    gate_slot_t *found = NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < GATE_MAX_SLOTS; i++) {
        if (g_slots[i].state == SLOT_FREE) {
            found = &g_slots[i];
            found->state = SLOT_QUEUED;  /* reserved; queued below */
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return found;
}

fmrb_gate_result_t fmrb_gate_call(fmrb_gate_job_t *job)
{
    if (!fmrb_gate_usable() || job == NULL || job->run == NULL ||
        job->nfds < 0 || job->nfds > FMRB_GATE_MAX_FDS ||
        (job->discard != NULL && job->arg != NULL)) {
        return FMRB_GATE_UNAVAILABLE;
    }
    gate_slot_t *s = take_slot();
    if (s == NULL) {
        return FMRB_GATE_UNAVAILABLE;
    }
    if (s->sem == NULL) {
        s->sem = xSemaphoreCreateBinary();
        if (s->sem == NULL) {
            pthread_mutex_lock(&g_lock);
            s->state = SLOT_FREE;
            pthread_mutex_unlock(&g_lock);
            return FMRB_GATE_UNAVAILABLE;
        }
    }
    (void)xSemaphoreTake(s->sem, 0);  /* a give left over from the last use */
    s->job = *job;
    s->waiter = xTaskGetCurrentTaskHandle();
    s->delivered = false;
    s->orphan = false;
    g_calls++;

    pthread_mutex_lock(&g_lock);
    s->next = NULL;
    if (g_queue_tail != NULL) {
        g_queue_tail->next = s;
    } else {
        g_queue_head = s;
    }
    g_queue_tail = s;
    ensure_helper_locked();
    pthread_cond_signal(&g_work);
    pthread_mutex_unlock(&g_lock);

    bool cancelled = false;
    TickType_t cancelled_at = 0;
    for (;;) {
        (void)xSemaphoreTake(s->sem, pdMS_TO_TICKS(GATE_EXIT_POLL_MS));
        if (s->delivered) {
            break;
        }
        if (!cancelled && exit_requested()) {
            cancelled = true;
            cancelled_at = xTaskGetTickCount();
            g_cancels++;
            shutdown_fds(&s->job);
        }
        if (cancelled && s->job.discard != NULL &&
            (xTaskGetTickCount() - cancelled_at) >= pdMS_TO_TICKS(GATE_ABANDON_MS)) {
            s->orphan = true;
            return FMRB_GATE_ABANDONED;
        }
    }
    if (s->job.arg == NULL) {
        memcpy(job->scratch, s->job.scratch, sizeof(job->scratch));
    }
    release_slot(s);
    return FMRB_GATE_DONE;
}

void fmrb_gate_get_stats(fmrb_gate_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < GATE_MAX_SLOTS; i++) {
        if (g_slots[i].state == SLOT_QUEUED || g_slots[i].state == SLOT_RUNNING) {
            out->busy++;
            if (g_slots[i].orphan) {
                out->orphans++;
            }
        }
    }
    out->helpers = g_helpers;
    out->helpers_idle = g_helpers_idle;
    pthread_mutex_unlock(&g_lock);
    out->calls = g_calls;
    out->cancels = g_cancels;
    out->deleted = g_deleted;
}
