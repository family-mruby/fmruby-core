/*
 * FreeRTOS port for Emscripten (WebAssembly). Cooperative.
 *
 * Structure follows the upstream POSIX port (one pthread per task, parked on a
 * condition variable when not running), with the two things that port gets from
 * the operating system replaced:
 *
 *   - Preemption. The POSIX port delivers SIGALRM to the running task's thread
 *     and switches from the handler. wasm has no signal delivery, so there is
 *     none: a task keeps the CPU until it blocks or yields.
 *
 *   - The tick. Without a timer interrupt nobody calls xTaskIncrementTick(), so
 *     this port derives the tick from the wall clock and feeds the kernel the
 *     ticks that have elapsed, at the two moments where doing so is safe and
 *     useful:
 *
 *       1. vPortYield(), before choosing the next task -- so a task that is
 *          about to block sees a current tick count, and timeouts that came due
 *          while the previous task ran take effect at the switch.
 *       2. The idle hook. When every task is blocked, idle is the only thing
 *          running: it sleeps a millisecond and then hands over the elapsed
 *          ticks, which is what unblocks vTaskDelay() and expires timeouts.
 *
 *     Both go through prvCatchUpTicks(), which calls xTaskIncrementTick() once
 *     per elapsed millisecond -- never skipping any, since the kernel's delayed
 *     lists are walked one tick at a time. Ticks are counted from a wall-clock
 *     base, so a burst after a long CPU-bound stretch puts xTickCount back in
 *     step with real time rather than letting it drift behind for good.
 *
 *     prvCatchUpTicks() refuses to run inside a critical section, which is the
 *     same rule the hardware follows: a tick interrupt cannot land while
 *     interrupts are masked.
 *
 * What a task actually runs on is an Emscripten pthread stack, not the block
 * the kernel allocated for it. The kernel's block is used only to hold this
 * port's per-task record (the Thread_t below), the way the POSIX port does it;
 * the rest of it goes untouched. See prvGetStackBytes() for how the requested
 * size is turned into the pthread stack size.
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

#include "FreeRTOS.h"
#include "task.h"
/* For TlsDeleteCallbackFunction_t: FreeRTOS.h pulls this in only on ESP_PLATFORM. */
#include "freertos/idf_additions.h"

#include "wait_for_event.h"

/*-----------------------------------------------------------*/

/*
 * Smallest pthread stack a task may get. Device stack sizes are tuned for
 * Xtensa/RISC-V frames and go as low as 2KB (mruby_tick), which is not enough
 * for the same C code compiled to wasm. Emscripten's own default is 64KB.
 */
#ifndef portWASM_MIN_TASK_STACK_BYTES
    #define portWASM_MIN_TASK_STACK_BYTES    ( 64 * 1024 )
#endif

/* How long the idle task sleeps before feeding the kernel the elapsed ticks. */
#ifndef portWASM_IDLE_SLEEP_MS
    #define portWASM_IDLE_SLEEP_MS    ( 1.0 )
#endif

typedef struct THREAD
{
    pthread_t pthread;
    TaskFunction_t pxCode;
    void * pvParams;
    BaseType_t xDying;
    struct event * ev;
    size_t xStackBytes;
} Thread_t;

/*
 * The per-task record lives at the top of the task's (otherwise unused) stack
 * block, so it can be found from a TaskHandle_t: the first member of a TCB is
 * pxTopOfStack, and pxPortInitialiseStack() returned a value pointing just
 * below the record.
 */
static inline Thread_t * prvGetThreadFromTask( TaskHandle_t xTask )
{
    StackType_t * pxTopOfStack = *( StackType_t ** ) xTask;

    return ( Thread_t * ) ( pxTopOfStack + 1 );
}

/*-----------------------------------------------------------*/

static volatile BaseType_t uxCriticalNesting;
static BaseType_t xSchedulerEnd = pdFALSE;
static BaseType_t xSchedulerStarted = pdFALSE;
static struct event * pxSchedulerEndEvent;

/* Tick bookkeeping (see the "tick" note at the top of the file). */
static double dTickBaseMs;
static uint64_t ullTicksFed;
static BaseType_t xInCatchUp = pdFALSE;

/* Observability for the PoC and for later diagnosis; see wasm_port.h. */
static uint32_t ulMaxTickBurst;
static uint64_t ullCatchUpCalls;

static void prvSuspendSelf( Thread_t * pxThread );
static void prvResumeThread( Thread_t * pxThread );
static void prvSwitchThread( Thread_t * pxThreadToResume,
                             Thread_t * pxThreadToSuspend );
static void * prvWaitForStart( void * pvParams );

/*-----------------------------------------------------------*/

static void prvFatalError( const char * pcCall,
                           int iErrno )
{
    fprintf( stderr, "wasm port: %s: %s\n", pcCall, strerror( iErrno ) );
    abort();
}

/*-----------------------------------------------------------*/

/*
 * Hand the kernel every tick that has elapsed since the last time we did.
 *
 * Returns pdTRUE if the kernel asked for a context switch. Callers that are in
 * a position to switch must act on it; the idle hook does.
 */
static BaseType_t prvCatchUpTicks( void )
{
    BaseType_t xSwitchRequired = pdFALSE;
    uint64_t ullDue;
    uint32_t ulBurst = 0;

    if( ( xSchedulerStarted == pdFALSE ) || ( uxCriticalNesting != 0 ) || ( xInCatchUp != pdFALSE ) )
    {
        return pdFALSE;
    }

    xInCatchUp = pdTRUE;
    ullCatchUpCalls++;

    ullDue = ( uint64_t ) ( ( emscripten_get_now() - dTickBaseMs ) / ( double ) portTICK_PERIOD_MS );

    while( ullTicksFed < ullDue )
    {
        ullTicksFed++;
        ulBurst++;

        if( xTaskIncrementTick() != pdFALSE )
        {
            xSwitchRequired = pdTRUE;
        }
    }

    if( ulBurst > ulMaxTickBurst )
    {
        ulMaxTickBurst = ulBurst;
    }

    xInCatchUp = pdFALSE;
    return xSwitchRequired;
}

uint32_t ulPortGetMaxTickBurst( void )
{
    return ulMaxTickBurst;
}

uint64_t ullPortGetTickCatchUpCalls( void )
{
    return ullCatchUpCalls;
}

/*-----------------------------------------------------------*/

/*
 * The idle hook is this port's clock. It only runs when no task is ready, which
 * is exactly when nothing else would advance the tick.
 *
 * This is ESP-IDF's own idle hook, which its tasks.c calls unconditionally --
 * hence configUSE_IDLE_HOOK stays 0 and there is exactly one hook.
 */
void esp_vApplicationIdleHook( void )
{
    emscripten_thread_sleep( portWASM_IDLE_SLEEP_MS );

    if( prvCatchUpTicks() != pdFALSE )
    {
        taskYIELD();
    }
}

/*-----------------------------------------------------------*/

/*
 * Work out how big a stack the task asked for. pxEndOfStack is the low end of
 * the kernel's block and pxTopOfStack the high end, so their difference is the
 * requested size in bytes (StackType_t is uint8_t on this port).
 */
static size_t prvGetStackBytes( const StackType_t * pxTopOfStack,
                                const StackType_t * pxEndOfStack )
{
    size_t xBytes = ( size_t ) ( pxTopOfStack - pxEndOfStack );

    if( xBytes < portWASM_MIN_TASK_STACK_BYTES )
    {
        xBytes = portWASM_MIN_TASK_STACK_BYTES;
    }

    return xBytes;
}

StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     StackType_t * pxEndOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters )
{
    Thread_t * pxThread;
    pthread_attr_t xThreadAttributes;
    uintptr_t uxRecord;
    int iRet;

    /* Carve the per-task record out of the top of the block, keeping it
     * aligned, and report the new top back to the kernel. */
    uxRecord = ( uintptr_t ) ( pxTopOfStack + 1 ) - sizeof( Thread_t );
    uxRecord &= ~( ( uintptr_t ) ( portBYTE_ALIGNMENT - 1 ) );
    pxThread = ( Thread_t * ) uxRecord;

    pxThread->pxCode = pxCode;
    pxThread->pvParams = pvParameters;
    pxThread->xDying = pdFALSE;
    pxThread->xStackBytes = prvGetStackBytes( pxTopOfStack, pxEndOfStack );
    pxThread->ev = event_create();

    pthread_attr_init( &xThreadAttributes );

    /* The task does not run on the kernel's block: Emscripten allocates the
     * real stack, and only its size comes from the caller's request. */
    ( void ) pthread_attr_setstacksize( &xThreadAttributes, pxThread->xStackBytes );

    vPortEnterCritical();
    iRet = pthread_create( &pxThread->pthread, &xThreadAttributes,
                           prvWaitForStart, pxThread );
    vPortExitCritical();

    pthread_attr_destroy( &xThreadAttributes );

    if( iRet != 0 )
    {
        prvFatalError( "pthread_create", iRet );
    }

    return ( StackType_t * ) pxThread - 1;
}

/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler( void )
{
    Thread_t * pxFirstThread;

    pxSchedulerEndEvent = event_create();

    dTickBaseMs = emscripten_get_now();
    ullTicksFed = 0;
    xSchedulerStarted = pdTRUE;

    /* Start the first task, then park this thread. It is main()'s thread (a
     * Worker, under -sPROXY_TO_PTHREAD) and owns nothing the scheduler needs. */
    pxFirstThread = prvGetThreadFromTask( xTaskGetCurrentTaskHandle() );
    prvResumeThread( pxFirstThread );

    while( xSchedulerEnd == pdFALSE )
    {
        event_wait( pxSchedulerEndEvent );
    }

    return 0;
}

/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
    Thread_t * pxCurrentThread = prvGetThreadFromTask( xTaskGetCurrentTaskHandle() );

    xSchedulerStarted = pdFALSE;
    xSchedulerEnd = pdTRUE;
    event_signal( pxSchedulerEndEvent );

    prvSuspendSelf( pxCurrentThread );
}

/*-----------------------------------------------------------*/

void vPortEnterCritical( void )
{
    uxCriticalNesting++;
}

void vPortExitCritical( void )
{
    configASSERT( uxCriticalNesting > 0 );
    uxCriticalNesting--;
}

/*
 * There are no interrupts to mask. The counter is still kept, because it is
 * what tells prvCatchUpTicks() to stay out.
 */
void vPortDisableInterrupts( void )
{
    uxCriticalNesting++;
}

void vPortEnableInterrupts( void )
{
    configASSERT( uxCriticalNesting > 0 );
    uxCriticalNesting--;
}

BaseType_t xPortSetInterruptMask( void )
{
    uxCriticalNesting++;
    return pdTRUE;
}

void vPortClearInterruptMask( BaseType_t xMask )
{
    ( void ) xMask;
    configASSERT( uxCriticalNesting > 0 );
    uxCriticalNesting--;
}

/*-----------------------------------------------------------*/

void vPortYield( void )
{
    Thread_t * pxThreadToSuspend;
    Thread_t * pxThreadToResume;

    /* Outside the critical section, so a yield reached from inside one (the
     * kernel does that in a few places) simply does not tick. */
    ( void ) prvCatchUpTicks();

    vPortEnterCritical();

    pxThreadToSuspend = prvGetThreadFromTask( xTaskGetCurrentTaskHandle() );

    vTaskSwitchContext();

    pxThreadToResume = prvGetThreadFromTask( xTaskGetCurrentTaskHandle() );

    prvSwitchThread( pxThreadToResume, pxThreadToSuspend );

    vPortExitCritical();
}

/*-----------------------------------------------------------*/

void vPortThreadDying( void * pxTaskToDelete,
                       volatile BaseType_t * pxPendYield )
{
    Thread_t * pxThread = prvGetThreadFromTask( ( TaskHandle_t ) pxTaskToDelete );

    ( void ) pxPendYield;
    pxThread->xDying = pdTRUE;
}

/*-----------------------------------------------------------*/

#if ( configTHREAD_LOCAL_STORAGE_DELETE_CALLBACKS == 1 )

/*
 * With deletion callbacks enabled the TLS array is twice as long: slot i holds
 * the pointer and slot i + n its callback. Reached through the public accessors
 * rather than the TCB, which port.c cannot see.
 */
    static void prvRunTlsDeleteCallbacks( TaskHandle_t xTask )
    {
        const BaseType_t xSlots = configNUM_THREAD_LOCAL_STORAGE_POINTERS / 2;
        BaseType_t x;

        for( x = 0; x < xSlots; x++ )
        {
            TlsDeleteCallbackFunction_t pvDelCallback =
                ( TlsDeleteCallbackFunction_t ) pvTaskGetThreadLocalStoragePointer( xTask, x + xSlots );

            if( pvDelCallback != NULL )
            {
                pvDelCallback( ( int ) x, pvTaskGetThreadLocalStoragePointer( xTask, x ) );
            }
        }
    }

#endif /* configTHREAD_LOCAL_STORAGE_DELETE_CALLBACKS */

/*
 * Called by the kernel once the task is off every list and just before its
 * memory is released.
 *
 * A task that deleted itself has already left through pthread_exit() in
 * prvSwitchThread(); the join below just reaps it. A task deleted by someone
 * else is parked in prvSuspendSelf() -- which is where every non-running task
 * always is in this port -- so marking it dying and waking it makes it leave
 * without touching any kernel state. That is why this port can support
 * vTaskDelete(other) at all, where the POSIX port needs pthread_cancel.
 *
 * The join has to complete before the kernel frees the stack block, since the
 * Thread_t and its event live in it.
 */
void vPortCleanUpTCB( void * pxTCB )
{
    Thread_t * pxThread = prvGetThreadFromTask( ( TaskHandle_t ) pxTCB );

    #if ( configTHREAD_LOCAL_STORAGE_DELETE_CALLBACKS == 1 )
        prvRunTlsDeleteCallbacks( ( TaskHandle_t ) pxTCB );
    #endif

    if( pthread_equal( pxThread->pthread, pthread_self() ) != 0 )
    {
        /* Would be a task freeing its own TCB from its own thread; the kernel
         * defers that to the idle task, so it should not happen. */
        return;
    }

    pxThread->xDying = pdTRUE;
    event_signal( pxThread->ev );
    ( void ) pthread_join( pxThread->pthread, NULL );
    event_delete( pxThread->ev );
}

/*-----------------------------------------------------------*/

static void * prvWaitForStart( void * pvParams )
{
    Thread_t * pxThread = pvParams;

    prvSuspendSelf( pxThread );

    uxCriticalNesting = 0;

    pxThread->pxCode( pxThread->pvParams );

    /* A task function must not return; vTaskDelete( NULL ) is the way out. */
    configASSERT( pdFALSE );
    return NULL;
}

/*-----------------------------------------------------------*/

static void prvSwitchThread( Thread_t * pxThreadToResume,
                             Thread_t * pxThreadToSuspend )
{
    BaseType_t uxSavedCriticalNesting;

    if( pxThreadToSuspend != pxThreadToResume )
    {
        /* Critical nesting belongs to the task, so it rides on this thread's
         * stack across the suspend. */
        uxSavedCriticalNesting = uxCriticalNesting;

        prvResumeThread( pxThreadToResume );

        if( pxThreadToSuspend->xDying != pdFALSE )
        {
            pthread_exit( NULL );
        }

        prvSuspendSelf( pxThreadToSuspend );

        uxCriticalNesting = uxSavedCriticalNesting;
    }
}

/*-----------------------------------------------------------*/

static void prvSuspendSelf( Thread_t * pxThread )
{
    event_wait( pxThread->ev );

    /* Woken by vPortCleanUpTCB() rather than by a context switch: leave now,
     * while holding nothing. */
    if( pxThread->xDying != pdFALSE )
    {
        pthread_exit( NULL );
    }
}

/*-----------------------------------------------------------*/

static void prvResumeThread( Thread_t * pxThread )
{
    if( pthread_equal( pthread_self(), pxThread->pthread ) == 0 )
    {
        event_signal( pxThread->ev );
    }
}

/*-----------------------------------------------------------*/

unsigned long ulPortGetRunTime( void )
{
    return ( unsigned long ) emscripten_get_now();
}

void vPortSetStackWatchpoint( void * pxStackStart )
{
    ( void ) pxStackStart;
}

/*-----------------------------------------------------------*/

/*
 * configSUPPORT_STATIC_ALLOCATION is on (as in ESP-IDF), so the kernel asks the
 * application where to put the idle task.
 */
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    uint32_t * pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
