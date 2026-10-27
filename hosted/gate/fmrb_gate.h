/*
 * fmrb_gate.h - the blocking-call gate of the cooperative port
 * (doc/linux_native/report/l2n.md).
 *
 * The cooperative FreeRTOS port (hosted/port/) runs one task at a time, so a
 * task that waits inside a system call -- recv, connect, a name lookup, a TLS
 * handshake -- stops every other task with it. The gate runs such a call on
 * a helper thread outside FreeRTOS while the calling task sleeps on a
 * FreeRTOS semaphore; when the call returns, the port's poll hook (in
 * FreeRTOS context) gives the semaphore and the task carries on with the
 * result. Other tasks run meanwhile.
 *
 * Helper threads never call FreeRTOS, the fmrb allocators or the log: they
 * run the call and put the job on a done list under a pthread mutex, nothing
 * else.
 *
 * Ending the app that waits: the waiting task looks at its app's exit flag
 * (fmrb_app_task_context_t.should_exit, set by fmrb_app_kill) while it
 * sleeps. When it is set, every descriptor the job named is shut down
 * (shutdown(2) wakes a recv, an accept, a connect or a poll on it), and the
 * call returns with whatever the system call said (an error or an end of
 * stream), so the app goes on to its own exit. A call that cannot be woken
 * (a name lookup) is abandoned after a short wait instead: such a job holds
 * only memory of its own, which the gate frees when the helper is done. If
 * the task is deleted while it waits (the forced kill), the same happens
 * from the delete hook, which waits for a call that writes into the task's
 * memory to end before the memory goes, and then tells the listener (below)
 * so that what the task left open can be closed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Descriptors one job can name to be shut down when it is cancelled. */
#define FMRB_GATE_MAX_FDS 4
/* Bytes of record a job can carry inside the gate (see scratch below). */
#define FMRB_GATE_SCRATCH 1280

typedef enum {
    FMRB_GATE_DONE = 0,     /* the call ran to its end; its result is valid */
    FMRB_GATE_ABANDONED,    /* gave up waiting (the app is ending); the call
                             * finishes on the gate's copy of scratch, and
                             * discard runs then */
    FMRB_GATE_UNAVAILABLE,  /* not usable from here; call directly */
} fmrb_gate_result_t;

typedef struct {
    /* Runs on the helper thread. Must not call FreeRTOS or fmrb APIs. It gets
     * arg, or when arg is NULL the gate's own copy of scratch. */
    void (*run)(void *arg);
    void *arg;
    /* Non-NULL makes the job abandonable, and then arg must be NULL: the
     * call works on the gate's copy of scratch only, so it may outlive the
     * caller. The gate calls discard (in the port's poll hook: no fmrb
     * allocator, no FreeRTOS call that waits) once the helper is done with
     * an abandoned job, to release what the call itself allocated. */
    void (*discard)(void *scratch);
    int fds[FMRB_GATE_MAX_FDS];
    int nfds;
    /* The record of a job without arg. Copied in before the call and back
     * out when it is done. */
    _Alignas(16) unsigned char scratch[FMRB_GATE_SCRATCH];
} fmrb_gate_job_t;

/* Start the gate: installs the port's hooks. Call once, from FreeRTOS
 * context or before the scheduler starts. */
void fmrb_gate_init(void);

/* true when the calling thread is a FreeRTOS task that may sleep on the gate
 * (the gate is up, the scheduler runs, not idle, not in a critical
 * section). Everywhere else a would-be-blocking call is simply made. */
bool fmrb_gate_usable(void);

/* Run job->run on a helper thread and sleep until it is done; job->scratch
 * holds the record's final state on FMRB_GATE_DONE. FMRB_GATE_UNAVAILABLE
 * when fmrb_gate_usable() is false or no job slot is free (the caller then
 * makes the call itself). */
fmrb_gate_result_t fmrb_gate_call(fmrb_gate_job_t *job);

/* fn(task) runs when a FreeRTOS task is deleted (any task, in the deleter's
 * context, after the gate has finished with the task's jobs): the place to
 * release what the task held outside FreeRTOS. Up to
 * FMRB_GATE_MAX_LISTENERS, called in the order they were added (the socket
 * registry, the Linux programs of sys_linux_proc.c). */
#define FMRB_GATE_MAX_LISTENERS 4
void fmrb_gate_add_task_deleted_listener(void (*fn)(void *task));

typedef struct {
    unsigned calls;         /* fmrb_gate_call that ran a job */
    unsigned busy;          /* jobs not finished (queued or running) */
    unsigned orphans;       /* abandoned jobs whose helper is still busy */
    unsigned helpers;       /* helper threads alive */
    unsigned helpers_idle;  /* of which waiting for work */
    unsigned cancels;       /* jobs cancelled because their app was ending */
    unsigned deleted;       /* jobs whose task was deleted while waiting */
} fmrb_gate_stats_t;

void fmrb_gate_get_stats(fmrb_gate_stats_t *out);

#ifdef __cplusplus
}
#endif
