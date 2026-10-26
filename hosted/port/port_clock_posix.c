/*
 * port_clock_posix.c - port_clock.h for the native build (Linux).
 *
 * nanosleep only ever sleeps the calling thread, and no signal is ever sent
 * to it by this port, so an EINTR here can only come from the host (a
 * debugger attaching, say); the remaining time is slept again.
 */

#include <errno.h>
#include <time.h>

#include "port_clock.h"

double dPortClockNowMs( void )
{
    struct timespec ts;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ( double ) ts.tv_sec * 1000.0 + ( double ) ts.tv_nsec / 1000000.0;
}

void vPortClockSleepMs( double ms )
{
    struct timespec req;
    struct timespec rem;

    if( ms <= 0.0 )
    {
        return;
    }

    req.tv_sec = ( time_t ) ( ms / 1000.0 );
    req.tv_nsec = ( long ) ( ( ms - ( double ) req.tv_sec * 1000.0 ) * 1000000.0 );

    while( nanosleep( &req, &rem ) != 0 && errno == EINTR )
    {
        req = rem;
    }
}
