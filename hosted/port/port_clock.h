/*
 * port_clock.h - the two things the cooperative port takes from its host.
 *
 * Everything else in hosted/port/ is plain pthreads and is the same on every
 * hosted build. Reading a monotonic clock and sleeping the calling thread are
 * not: Emscripten has its own calls for both (a Worker must sleep on a futex,
 * not in nanosleep), so each build links one implementation of this header:
 *
 *   port_clock_wasm.c   Date.now (held monotonic) / emscripten_thread_sleep
 *   port_clock_posix.c  clock_gettime(CLOCK_MONOTONIC) / nanosleep
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Milliseconds on a monotonic clock that reads the same on every thread (the
 * tick is derived from whichever task calls it), with a resolution of a
 * millisecond or better. Only differences are meaningful. */
double dPortClockNowMs( void );

/* Put the calling thread to sleep for about ms milliseconds. */
void vPortClockSleepMs( double ms );

#ifdef __cplusplus
}
#endif
