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

#include <stdbool.h>
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
