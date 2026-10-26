/*
 * port_hosted.h - what the cooperative port offers to code that does work
 * outside FreeRTOS (doc/linux_native/report/l2n.md).
 *
 * The port runs one task at a time, so a task must never sit in a system
 * call that waits; such a call is handed to a thread outside FreeRTOS (the
 * blocking-call gate, hosted/gate/). That thread may not touch the kernel,
 * so the port gives the gate two places of its own that do run in FreeRTOS
 * context:
 *
 *   poll hook    called where the port feeds the kernel its ticks (every
 *                context switch and every idle turn), outside any critical
 *                section. It may use the FromISR API to wake tasks and
 *                returns pdTRUE when one of higher priority became ready.
 *   delete hook  called when a task is deleted, before its thread is
 *                reaped, in the context of whoever deleted it.
 *
 * Neither is set unless a build installs one (the browser build does not).
 */
#pragma once

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef BaseType_t ( * PortHostedPollHook_t )( void );
typedef void ( * PortHostedDeleteHook_t )( TaskHandle_t xTask );

void vPortHostedSetPollHook( PortHostedPollHook_t pxHook );
void vPortHostedSetDeleteHook( PortHostedDeleteHook_t pxHook );

/* pdTRUE on the thread of a FreeRTOS task (not idle) that may block on a
 * kernel object right now: the scheduler is running and the task is outside
 * every critical section. pdFALSE on any other thread. */
BaseType_t xPortHostedTaskMayBlock( void );

/* The longest stretch, in ticks (ms), that the machine went without a
 * context switch or an idle turn since the start or the last reset: how long
 * one task kept every other one waiting. Readable from any thread (a
 * diagnostic; a torn read is harmless). */
uint32_t ulPortGetMaxTickBurst( void );
void vPortResetMaxTickBurst( void );

#ifdef __cplusplus
}
#endif
