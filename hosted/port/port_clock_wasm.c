/*
 * port_clock_wasm.c - port_clock.h for the browser build (Emscripten).
 */

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

#include "port_clock.h"

double dPortClockNowMs( void )
{
    return emscripten_get_now();
}

void vPortClockSleepMs( double ms )
{
    emscripten_thread_sleep( ms );
}
