/*
 * FreeRTOS port layer for Emscripten (WebAssembly), cooperative.
 *
 * Shape of the port (the whole idea in one place):
 *
 *   - One task is one pthread, which under Emscripten is one Web Worker over a
 *     SharedArrayBuffer. Exactly one of them runs at a time; every other task's
 *     thread is parked on its own condition variable inside prvSuspendSelf().
 *     A context switch is "signal the next one's condition variable, then park
 *     on my own" -- the same skeleton the upstream POSIX port uses.
 *
 *   - There are no interrupts, because wasm has no signal delivery. So the
 *     critical-section and interrupt-mask macros are just a nesting counter:
 *     there is nothing asynchronous to keep out. The counter still matters,
 *     because the port refuses to advance the tick while it is non-zero (see
 *     port.c).
 *
 *   - Consequently there is no preemption by the tick. A task that neither
 *     blocks nor yields keeps the CPU. This is the accepted limitation of the
 *     wasm target (doc/wasm/plan.md); mruby's timeslicing is moved onto the VM
 *     thread itself in P2 so that Ruby code is still interruptible.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------- Type definitions */

#define portCHAR                 char
#define portFLOAT                float
#define portDOUBLE               double
#define portLONG                 long
#define portSHORT                short
/* uint8_t, as on the device: it is what makes xTaskCreate's stack depth a byte
 * count in ESP-IDF, so the numbers in .app.toml and fmrb_task_create_ex mean the
 * same thing here. (The upstream POSIX port uses unsigned long, which silently
 * multiplies every stack size by eight -- a quirk of the Linux simulation we do
 * not want to carry over.) */
#define portSTACK_TYPE           uint8_t
#define portBASE_TYPE            long
#define portPOINTER_SIZE_TYPE    intptr_t

typedef portSTACK_TYPE   StackType_t;
typedef portBASE_TYPE    BaseType_t;
typedef unsigned portBASE_TYPE UBaseType_t;

typedef unsigned long    TickType_t;
#define portMAX_DELAY               ( TickType_t ) ULONG_MAX

#define portTICK_TYPE_IS_ATOMIC     1

/* ------------------------------------------------------ Architecture bits */

#define portSTACK_GROWTH               ( -1 )
/* 1 only so that pxPortInitialiseStack() is handed pxEndOfStack and can work
 * out how big a stack the task asked for; configCHECK_FOR_STACK_OVERFLOW is 0
 * and no canary is planted (see FreeRTOSConfig.h). */
#define portHAS_STACK_OVERFLOW_CHECKING ( 1 )
#define portTICK_PERIOD_MS             ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portTICK_RATE_MICROSECONDS     ( ( TickType_t ) 1000000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT             8

/* --------------------------------------------------- Scheduler utilities */

extern void vPortYield( void );

#define portYIELD()                                   vPortYield()
#define portEND_SWITCHING_ISR( xSwitchRequired )      if( ( xSwitchRequired ) != pdFALSE ) vPortYield()
#define portYIELD_FROM_ISR( x )                       portEND_SWITCHING_ISR( x )

/* ---------------------------------------------- Critical section handling */

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern void vPortDisableInterrupts( void );
extern void vPortEnableInterrupts( void );
extern BaseType_t xPortSetInterruptMask( void );
extern void vPortClearInterruptMask( BaseType_t xMask );

#define portSET_INTERRUPT_MASK()                 ( vPortDisableInterrupts() )
#define portCLEAR_INTERRUPT_MASK()               ( vPortEnableInterrupts() )
#define portSET_INTERRUPT_MASK_FROM_ISR()        xPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR( x )   vPortClearInterruptMask( x )
#define portDISABLE_INTERRUPTS()                 portSET_INTERRUPT_MASK()
#define portENABLE_INTERRUPTS()                  portCLEAR_INTERRUPT_MASK()

/*
 * There is no second core and no ISR, so a spinlock has nothing to lock. The
 * type is kept because ESP-IDF code (fmrb_midi_sched.c, and tasks.c itself)
 * declares portMUX_TYPE objects and hands them to these macros.
 */
typedef int portMUX_TYPE;

#define portMUX_FREE_VAL                         0
#define portMUX_INITIALIZER_UNLOCKED             0
#define portMUX_INITIALIZE( mux )                do { *( mux ) = 0; } while( 0 )

#define portENTER_CRITICAL( mux )                { ( void ) ( mux ); vPortEnterCritical(); }
#define portEXIT_CRITICAL( mux )                 { ( void ) ( mux ); vPortExitCritical(); }
#define portENTER_CRITICAL_SAFE( mux )           portENTER_CRITICAL( mux )
#define portEXIT_CRITICAL_SAFE( mux )            portEXIT_CRITICAL( mux )
#define portENTER_CRITICAL_ISR( mux )            portENTER_CRITICAL( mux )
#define portEXIT_CRITICAL_ISR( mux )             portEXIT_CRITICAL( mux )

/* ------------------------------------------------------------ Task hooks */

extern void vPortThreadDying( void * pxTaskToDelete,
                              volatile BaseType_t * pxPendYield );
extern void vPortCleanUpTCB( void * pxTCB );

#define portPRE_TASK_DELETE_HOOK( pvTaskToDelete, pxPendYield ) \
    vPortThreadDying( ( pvTaskToDelete ), ( pxPendYield ) )
#define portCLEAN_UP_TCB( pxTCB )    vPortCleanUpTCB( pxTCB )

#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )    void vFunction( void * pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )          void vFunction( void * pvParameters )

/* ------------------------------------------------------- ESP-IDF surface */

/* Single "core", and no ISR context to be in. */
static inline BaseType_t xPortGetCoreID( void )
{
    return ( BaseType_t ) 0;
}

#define portGET_CORE_ID()          xPortGetCoreID()

static inline BaseType_t xPortInIsrContext( void )
{
    return ( BaseType_t ) 0;
}

#define portCHECK_IF_IN_ISR()      xPortInIsrContext()
#define portASSERT_IF_IN_ISR()     do { } while( 0 )

/* One flat heap: any pointer that malloc returned is valid for anything. */
#define portVALID_LIST_MEM( ptr )     ( ( ptr ) != NULL )
#define portVALID_TCB_MEM( ptr )      ( ( ptr ) != NULL )
#define portVALID_STACK_MEM( ptr )    ( ( ptr ) != NULL )

/*
 * Context switches go through pthread condition variables, which are full
 * memory barriers, so only the compiler needs to be held back here.
 */
#define portMEMORY_BARRIER()       __asm volatile ( "" ::: "memory" )

extern unsigned long ulPortGetRunTime( void );
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()    /* no-op */
#define portGET_RUN_TIME_COUNTER_VALUE()            ulPortGetRunTime()

void vPortSetStackWatchpoint( void * pxStackStart );

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
