// Linux simulator only: make the logging locks safe against the tick signal.
//
// The FreeRTOS Linux port preempts the running task from a signal handler and
// picks the highest-priority READY task afterwards. IDF's log path takes two
// glibc-backed locks the scheduler cannot see -- the tag-level pthread mutex
// (log_lock.c) and glibc's tzset lock inside localtime() (the wall-clock
// timestamp) -- so a low-priority task preempted while holding one leaves a
// higher-priority logger blocked forever: the scheduler keeps choosing the
// blocked "running" task and the holder never gets the CPU back. Both were
// caught live with gdb (doc/sim_log_deadlock.md, 2026-08-16); the sustained
// cross-priority traffic of a Pub/Sub turn loop is what made the windows hit.
//
// The fix wraps the lock-holding regions in the port's critical section,
// which masks all signals (vPortEnterCritical -> pthread_sigmask). A holder
// that cannot be preempted releases promptly, and the inversion cannot form.
// Wrapping is done at the linker (-Wl,--wrap=...) so every caller is covered,
// IDF components included.
//
// This file is only compiled for IDF_TARGET=linux. On the device the log lock
// is a FreeRTOS mutex and none of this applies.
//
// 2026-08-25: a third lock of the same family was caught, and it was the one
// the original note had written off as "covered by the wraps above" -- glibc's
// stdio FILE lock. It is not covered: esp_log_va takes the tag lock, releases
// it, and only then calls the output function, so the vprintf that locks
// stdout runs outside every wrapped region. gdb caught a task parked inside
// write() with the lock held while two higher-priority loggers waited on it,
// which is the same inversion as before with a different mutex. The output
// call is now wrapped too, through esp_log_set_vprintf.

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void __real_esp_log_impl_lock(void);
bool __real_esp_log_impl_lock_timeout(void);
void __real_esp_log_impl_unlock(void);
char *__real_esp_log_system_timestamp(void);

// The critical section stays held from lock to unlock, so the tick cannot
// land between them. Nesting is fine (uxCriticalNesting).
void __wrap_esp_log_impl_lock(void)
{
    vPortEnterCritical();
    __real_esp_log_impl_lock();
}

bool __wrap_esp_log_impl_lock_timeout(void)
{
    vPortEnterCritical();
    return __real_esp_log_impl_lock_timeout();
}

void __wrap_esp_log_impl_unlock(void)
{
    __real_esp_log_impl_unlock();
    vPortExitCritical();
}

// localtime() takes glibc's tzset lock; hold it only inside a critical
// section so the holder cannot be parked mid-conversion.
char *__wrap_esp_log_system_timestamp(void)
{
    vPortEnterCritical();
    char *ts = __real_esp_log_system_timestamp();
    vPortExitCritical();
    return ts;
}

// The output itself. vprintf takes stdout's FILE lock and then writes, and
// both halves are windows: a task parked between them holds a lock every
// other logger needs.
//
// Installed with esp_log_set_vprintf rather than a linker wrap, because only
// the log path needs this -- an app's own printf is not part of the
// cross-priority traffic that forms the inversion, and putting every printf
// in the machine inside a critical section would be a much larger promise.
//
// The write happens with signals masked, so a slow reader on the other end of
// stdout delays the tick for that long. In this simulator stdout is a pipe
// docker drains continuously; against a hang, that is the better risk.
static int sim_log_guarded_vprintf(const char *format, va_list args)
{
    vPortEnterCritical();
    int n = vprintf(format, args);
    // Flushed inside the guard as well: leaving a partial line in the buffer
    // means the next logger completes someone else's write while holding the
    // same lock, which is the window this is closing.
    fflush(stdout);
    vPortExitCritical();
    return n;
}

void fmrb_sim_log_guard_init(void)
{
    esp_log_set_vprintf(sim_log_guarded_vprintf);
}
