/*
 * SPDX-FileCopyrightText: 2021 Amazon.com, Inc. or its affiliates
 *
 * SPDX-License-Identifier: MIT
 *
 * Derived from the FreeRTOS Kernel POSIX port's utils/wait_for_event.c
 * (ESP-IDF copy). Unchanged in substance: a mutex, a condition variable and a
 * sticky "you have been signalled" flag. The flag is what makes the handoff in
 * prvSwitchThread() safe -- the resumed thread may signal the suspending one
 * back before it has reached its own event_wait().
 *
 * Under Emscripten these are futexes over the shared memory (Atomics.wait /
 * Atomics.notify), so a parked task's Worker really sleeps.
 */

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "wait_for_event.h"

struct event
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool event_triggered;
};

struct event * event_create( void )
{
    struct event * ev = malloc( sizeof( struct event ) );

    assert( ev != NULL );
    ev->event_triggered = false;
    pthread_mutex_init( &ev->mutex, NULL );
    pthread_cond_init( &ev->cond, NULL );
    return ev;
}

void event_delete( struct event * ev )
{
    pthread_mutex_destroy( &ev->mutex );
    pthread_cond_destroy( &ev->cond );
    free( ev );
}

bool event_wait( struct event * ev )
{
    pthread_mutex_lock( &ev->mutex );

    while( ev->event_triggered == false )
    {
        pthread_cond_wait( &ev->cond, &ev->mutex );
    }

    ev->event_triggered = false;
    pthread_mutex_unlock( &ev->mutex );
    return true;
}

void event_signal( struct event * ev )
{
    pthread_mutex_lock( &ev->mutex );
    ev->event_triggered = true;
    pthread_cond_signal( &ev->cond );
    pthread_mutex_unlock( &ev->mutex );
}
