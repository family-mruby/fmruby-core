/*
 * port_clock_wasm.c - port_clock.h for the browser build (Emscripten).
 *
 * The clock is the wall clock (Date.now()), not emscripten_get_now().
 *
 * Every task runs on a Worker of its own, and emscripten_get_now() is
 * performance.timeOrigin + performance.now() of the calling Worker. Chromium
 * (Linux) ties a Worker's clock to the wall clock at the moment the Worker
 * first touches `performance` and then advances it on the monotonic clock,
 * so wherever the two clocks drift apart (WSL2 steps its wall clock forward
 * by 50-70 ms about every 30 s) each Worker reads a different time, and a
 * task that first read the clock later reads a later one: minutes after
 * boot an app's task was 400 ms ahead of the idle task. The port derives the kernel tick from whichever
 * task calls it, so a task that ran pushed the tick ahead to its own time
 * and the idle task, behind it, fed no ticks until its clock caught up:
 * every timed wait took that much longer (a 100 ms sleep took 400 ms; the
 * further from boot an app was started, the slower it ran;
 * doc/wasm/report/worker_clock.md).
 *
 * Date.now() is one clock for every Worker, and it is the clock the rest of
 * the machine already uses (fmrb_hal_time_get_us() is gettimeofday(), which
 * is Date.now() here). Its resolution is a millisecond, which is the tick.
 * A wall clock can be set back; a step back is absorbed here (the clock
 * holds still for no time at all and carries on from where it was), so the
 * value never goes backwards. A step forward reaches the kernel as a burst
 * of ticks, as a long stretch without yielding already does.
 */

#include <stdatomic.h>
#include <stdint.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>

#include "port_clock.h"

/* Shared by every Worker (the module's memory is a SharedArrayBuffer). */
static _Atomic int64_t llLastMs;    /* the latest value handed out */
static _Atomic int64_t llBackMs;    /* steps back absorbed so far */

double dPortClockNowMs( void )
{
    int64_t llRaw = ( int64_t ) emscripten_date_now();

    for( ; ; )
    {
        int64_t llBack = atomic_load( &llBackMs );
        int64_t llNow = llRaw + llBack;
        int64_t llLast = atomic_load( &llLastMs );

        if( llNow >= llLast )
        {
            if( atomic_compare_exchange_weak( &llLastMs, &llLast, llNow ) )
            {
                return ( double ) llNow;
            }

            continue; /* another Worker moved it; read again */
        }

        /* The wall clock was set back: shift it forward by the step. */
        ( void ) atomic_compare_exchange_strong( &llBackMs, &llBack, llBack + ( llLast - llNow ) );
    }
}

void vPortClockSleepMs( double ms )
{
    emscripten_thread_sleep( ms );
}
