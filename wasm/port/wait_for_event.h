/*
 * SPDX-FileCopyrightText: 2021 Amazon.com, Inc. or its affiliates
 *
 * SPDX-License-Identifier: MIT
 *
 * Derived from the FreeRTOS Kernel POSIX port's utils/wait_for_event.h
 * (ESP-IDF copy). The timed variant is dropped: this port never waits on an
 * event with a deadline -- timeouts are the kernel's business, and the port's
 * only wait is "park until someone hands me the CPU".
 */

#ifndef WAIT_FOR_EVENT_H
#define WAIT_FOR_EVENT_H

#include <stdbool.h>

struct event;

struct event * event_create( void );
void event_delete( struct event * ev );
bool event_wait( struct event * ev );
void event_signal( struct event * ev );

#endif /* WAIT_FOR_EVENT_H */
