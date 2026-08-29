/*
 * P1 proof of concept for the cooperative Emscripten FreeRTOS port.
 *
 * Exercises the FreeRTOS surface that fmruby-core actually uses (the inventory
 * in doc/wasm/plan.md), one check per acceptance item in
 * doc/wasm/instruction_p1.md. Every check prints a single machine-readable
 * line, so `rake wasm:poc` can decide pass/fail without reading prose:
 *
 *     PASS <name> - <detail>
 *     FAIL <name> - <detail>
 *     POC RESULT: <passed>/<total>
 *
 * Runs under node; nothing here needs a browser.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten/emscripten.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "message_buffer.h"
#include "freertos/idf_additions.h"

/* How long test 9 watches the tick against the wall clock. */
#ifndef POC_LONG_RUN_MS
    #define POC_LONG_RUN_MS    ( 10000 )
#endif

/* More tasks than -sPTHREAD_POOL_SIZE, so test 10 runs the pool dry. */
#ifndef POC_POOL_TASKS
    #define POC_POOL_TASKS    ( 40 )
#endif

#define POC_MAIN_PRIO      ( 10 )
#define POC_HELPER_PRIO    ( 11 )

/* Statistics the port keeps about the tick catch-up; see port.c. */
extern uint32_t ulPortGetMaxTickBurst( void );
extern uint64_t ullPortGetTickCatchUpCalls( void );

/*-----------------------------------------------------------*/

static int iChecksRun;
static int iChecksPassed;

static void prvCheck( const char * pcName,
                      int iOk,
                      const char * pcFormat,
                      ... )
{
    va_list xArgs;

    iChecksRun++;

    if( iOk )
    {
        iChecksPassed++;
    }

    printf( "%s %s - ", iOk ? "PASS" : "FAIL", pcName );
    va_start( xArgs, pcFormat );
    vprintf( pcFormat, xArgs );
    va_end( xArgs );
    printf( "\n" );
    fflush( stdout );
}

static double prvNow( void )
{
    return emscripten_get_now();
}

/*-----------------------------------------------------------
 * 1. Task creation, vTaskDelay accuracy, priority order
 *----------------------------------------------------------*/

static char cOrderSeen[ 8 ];
static int iOrderLen;

static void prvOrderTask( void * pvParams )
{
    if( iOrderLen < ( int ) sizeof( cOrderSeen ) - 1 )
    {
        cOrderSeen[ iOrderLen++ ] = ( char ) ( intptr_t ) pvParams;
    }

    vTaskDelete( NULL );
}

static void prvTestTasksAndDelay( void )
{
    double dStart;
    double dElapsed;
    int i;

    /* Priority order: three tasks made ready while a higher-priority task (this
     * one) still holds the CPU. They must run highest first once we block. */
    iOrderLen = 0;
    ( void ) xTaskCreate( prvOrderTask, "ord_lo", 4096, ( void * ) ( intptr_t ) 'L', POC_MAIN_PRIO - 3, NULL );
    ( void ) xTaskCreate( prvOrderTask, "ord_hi", 4096, ( void * ) ( intptr_t ) 'H', POC_MAIN_PRIO - 1, NULL );
    ( void ) xTaskCreate( prvOrderTask, "ord_mid", 4096, ( void * ) ( intptr_t ) 'M', POC_MAIN_PRIO - 2, NULL );
    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    cOrderSeen[ iOrderLen ] = '\0';
    prvCheck( "priority_order", strcmp( cOrderSeen, "HML" ) == 0,
              "ran in order \"%s\" (want \"HML\")", cOrderSeen );

    /* One long delay. */
    dStart = prvNow();
    vTaskDelay( pdMS_TO_TICKS( 100 ) );
    dElapsed = prvNow() - dStart;
    prvCheck( "delay_100ms", ( dElapsed >= 95.0 ) && ( dElapsed <= 115.0 ),
              "%.1f ms (want 95..115)", dElapsed );

    /* Ten short ones, to see whether the error accumulates. */
    dStart = prvNow();

    for( i = 0; i < 10; i++ )
    {
        vTaskDelay( pdMS_TO_TICKS( 20 ) );
    }

    dElapsed = prvNow() - dStart;
    prvCheck( "delay_10x20ms", ( dElapsed >= 195.0 ) && ( dElapsed <= 230.0 ),
              "%.1f ms (want 195..230)", dElapsed );
}

/*-----------------------------------------------------------
 * 2. Task notifications (what mruby-task's task_hal.c runs on)
 *----------------------------------------------------------*/

static TaskHandle_t xNotifyWaiter;
static volatile uint32_t ulNotifyTaken;
static volatile double dNotifyWokeAt;
static volatile uint32_t ulNotifyTimedOut;
static volatile double dNotifyTimeoutMs;
static volatile int iNotifyDone;

static void prvNotifyWaiterTask( void * pvParams )
{
    double dStart;

    ( void ) pvParams;

    /* A notification that arrives while we wait. */
    ulNotifyTaken = ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( 500 ) );
    dNotifyWokeAt = prvNow();

    /* And one that never comes: the timeout must expire on its own. */
    dStart = prvNow();
    ulNotifyTimedOut = ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( 60 ) );
    dNotifyTimeoutMs = prvNow() - dStart;

    iNotifyDone = 1;
    vTaskDelete( NULL );
}

static void prvTestNotifications( void )
{
    double dGaveAt;

    ulNotifyTaken = 0;
    ulNotifyTimedOut = 0xFFFFFFFFu;
    iNotifyDone = 0;

    ( void ) xTaskCreate( prvNotifyWaiterTask, "notify", 8192, NULL, POC_HELPER_PRIO, &xNotifyWaiter );

    /* Let it reach the first wait, then wake it. */
    vTaskDelay( pdMS_TO_TICKS( 30 ) );
    dGaveAt = prvNow();
    xTaskNotifyGive( xNotifyWaiter );

    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    prvCheck( "notify_take", ( ulNotifyTaken == 1 ) && ( ( dNotifyWokeAt - dGaveAt ) < 10.0 ),
              "value=%u, woke %.1f ms after the give", ( unsigned ) ulNotifyTaken,
              dNotifyWokeAt - dGaveAt );
    prvCheck( "notify_timeout",
              iNotifyDone && ( ulNotifyTimedOut == 0 ) &&
              ( dNotifyTimeoutMs >= 55.0 ) && ( dNotifyTimeoutMs <= 80.0 ),
              "value=%u after %.1f ms (want 0 after 55..80)",
              ( unsigned ) ulNotifyTimedOut, dNotifyTimeoutMs );
}

/*-----------------------------------------------------------
 * 3. Queues: send, receive, timeout, and release of a full queue
 *----------------------------------------------------------*/

static QueueHandle_t xTestQueue;

static void prvQueueDrainTask( void * pvParams )
{
    int iItem;

    ( void ) pvParams;
    vTaskDelay( pdMS_TO_TICKS( 40 ) );
    ( void ) xQueueReceive( xTestQueue, &iItem, 0 );
    vTaskDelete( NULL );
}

static void prvTestQueue( void )
{
    int iSent = 0x1234;
    int iGot = 0;
    double dStart;
    double dElapsed;
    BaseType_t xResult;

    xTestQueue = xQueueCreate( 2, sizeof( int ) );

    xResult = xQueueSend( xTestQueue, &iSent, 0 );
    ( void ) xQueueReceive( xTestQueue, &iGot, 0 );
    prvCheck( "queue_roundtrip", ( xResult == pdPASS ) && ( iGot == iSent ),
              "sent 0x%x, received 0x%x", iSent, iGot );

    /* Receiving from an empty queue must wait for the whole timeout. */
    dStart = prvNow();
    xResult = xQueueReceive( xTestQueue, &iGot, pdMS_TO_TICKS( 50 ) );
    dElapsed = prvNow() - dStart;
    prvCheck( "queue_recv_timeout",
              ( xResult == pdFALSE ) && ( dElapsed >= 45.0 ) && ( dElapsed <= 70.0 ),
              "returned %ld after %.1f ms (want fail after 45..70)", ( long ) xResult, dElapsed );

    /* Fill it, then block on a send until someone makes room. */
    ( void ) xQueueSend( xTestQueue, &iSent, 0 );
    ( void ) xQueueSend( xTestQueue, &iSent, 0 );
    ( void ) xTaskCreate( prvQueueDrainTask, "qdrain", 8192, NULL, POC_HELPER_PRIO, NULL );

    dStart = prvNow();
    xResult = xQueueSend( xTestQueue, &iSent, pdMS_TO_TICKS( 500 ) );
    dElapsed = prvNow() - dStart;
    prvCheck( "queue_full_blocks",
              ( xResult == pdPASS ) && ( dElapsed >= 35.0 ) && ( dElapsed <= 70.0 ),
              "send unblocked after %.1f ms (want 35..70)", dElapsed );

    vTaskDelay( pdMS_TO_TICKS( 20 ) );
    vQueueDelete( xTestQueue );
}

/*-----------------------------------------------------------
 * 4. Counting semaphore, 96 slots
 *
 * Scaled-down stand-in for the GFX flow control in
 * main/kernel/host/host_task.c: the producer runs the slots out and blocks,
 * the consumer gives them back and the producer carries on.
 *----------------------------------------------------------*/

#define POC_GFX_SLOTS    ( 96 )

static SemaphoreHandle_t xSlotSemaphore;

static void prvSlotConsumerTask( void * pvParams )
{
    int i;

    ( void ) pvParams;
    vTaskDelay( pdMS_TO_TICKS( 40 ) );

    for( i = 0; i < 5; i++ )
    {
        ( void ) xSemaphoreGive( xSlotSemaphore );
    }

    vTaskDelete( NULL );
}

static void prvTestCountingSemaphore( void )
{
    int i;
    int iTaken = 0;
    BaseType_t xResult;
    double dStart;
    double dElapsed;

    xSlotSemaphore = xSemaphoreCreateCounting( POC_GFX_SLOTS, POC_GFX_SLOTS );

    for( i = 0; i < POC_GFX_SLOTS; i++ )
    {
        if( xSemaphoreTake( xSlotSemaphore, 0 ) == pdTRUE )
        {
            iTaken++;
        }
    }

    xResult = xSemaphoreTake( xSlotSemaphore, 0 );
    prvCheck( "counting_sem_drain", ( iTaken == POC_GFX_SLOTS ) && ( xResult == pdFALSE ),
              "took %d of %d, then the next take %s", iTaken, POC_GFX_SLOTS,
              ( xResult == pdFALSE ) ? "failed as expected" : "unexpectedly succeeded" );

    ( void ) xTaskCreate( prvSlotConsumerTask, "slots", 8192, NULL, POC_HELPER_PRIO, NULL );

    dStart = prvNow();
    xResult = xSemaphoreTake( xSlotSemaphore, pdMS_TO_TICKS( 500 ) );
    dElapsed = prvNow() - dStart;
    prvCheck( "counting_sem_refill",
              ( xResult == pdTRUE ) && ( dElapsed >= 35.0 ) && ( dElapsed <= 70.0 ),
              "blocked take succeeded after %.1f ms (want 35..70)", dElapsed );

    vTaskDelay( pdMS_TO_TICKS( 20 ) );
    vSemaphoreDelete( xSlotSemaphore );
}

/*-----------------------------------------------------------
 * 5. Mutex ownership
 *
 * Not "a non-owner's give fails": with configCHECK_MUTEX_GIVEN_BY_OWNER
 * (on, as in the device build) that path asserts and takes the process down by
 * design, so it cannot be a check here. What is checked instead is the
 * bookkeeping that constraint exists to protect -- who the kernel thinks the
 * holder is, that a second taker waits, and that the holder inherits the
 * waiter's priority and is dropped back afterwards.
 *----------------------------------------------------------*/

static SemaphoreHandle_t xTestMutex;
static volatile BaseType_t xWaiterTimedOut;
static volatile BaseType_t xWaiterGotIt;
static volatile int iWaiterDone;

static void prvMutexWaiterTask( void * pvParams )
{
    ( void ) pvParams;

    /* The main task holds it: this must time out. */
    xWaiterTimedOut = xSemaphoreTake( xTestMutex, pdMS_TO_TICKS( 40 ) );

    /* Now wait properly; the main task gives it up shortly. */
    xWaiterGotIt = xSemaphoreTake( xTestMutex, pdMS_TO_TICKS( 500 ) );

    if( xWaiterGotIt == pdTRUE )
    {
        ( void ) xSemaphoreGive( xTestMutex );
    }

    iWaiterDone = 1;
    vTaskDelete( NULL );
}

static void prvTestMutex( void )
{
    UBaseType_t uxPriorityWhileWaited;
    UBaseType_t uxPriorityAfter;
    TaskHandle_t xSelf = xTaskGetCurrentTaskHandle();

    xTestMutex = xSemaphoreCreateMutex();
    iWaiterDone = 0;
    xWaiterTimedOut = pdTRUE;
    xWaiterGotIt = pdFALSE;

    ( void ) xSemaphoreTake( xTestMutex, portMAX_DELAY );
    prvCheck( "mutex_holder", xSemaphoreGetMutexHolder( xTestMutex ) == xSelf,
              "holder is %s", ( xSemaphoreGetMutexHolder( xTestMutex ) == xSelf ) ? "this task" : "someone else" );

    ( void ) xTaskCreate( prvMutexWaiterTask, "mutexw", 8192, NULL, POC_HELPER_PRIO, NULL );

    /* Let the waiter time out once, then block for real. */
    vTaskDelay( pdMS_TO_TICKS( 80 ) );
    uxPriorityWhileWaited = uxTaskPriorityGet( xSelf );

    ( void ) xSemaphoreGive( xTestMutex );
    uxPriorityAfter = uxTaskPriorityGet( xSelf );
    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    prvCheck( "mutex_contended", ( xWaiterTimedOut == pdFALSE ) && ( xWaiterGotIt == pdTRUE ) && iWaiterDone,
              "waiter timed out while held (%s), then took it (%s)",
              ( xWaiterTimedOut == pdFALSE ) ? "yes" : "no",
              ( xWaiterGotIt == pdTRUE ) ? "yes" : "no" );
    prvCheck( "mutex_priority_inherit",
              ( uxPriorityWhileWaited == POC_HELPER_PRIO ) && ( uxPriorityAfter == POC_MAIN_PRIO ),
              "holder ran at %u while contended, %u after giving (want %u then %u)",
              ( unsigned ) uxPriorityWhileWaited, ( unsigned ) uxPriorityAfter,
              ( unsigned ) POC_HELPER_PRIO, ( unsigned ) POC_MAIN_PRIO );

    vSemaphoreDelete( xTestMutex );
}

/*-----------------------------------------------------------
 * 6. Thread-local storage and its deletion callbacks
 *
 * fmrb_current() reads a TLS slot on every call, and the app teardown path
 * hangs off the deletion callback, so this has to work for a task that ends by
 * deleting itself.
 *----------------------------------------------------------*/

#define POC_TLS_VALUE_0    ( ( void * ) 0xA1A1A1A1u )
#define POC_TLS_VALUE_1    ( ( void * ) 0xB2B2B2B2u )
#define POC_TLS_VALUE_2    ( ( void * ) 0xC3C3C3C3u )

static volatile int iTlsCallbackCount;
static volatile int iTlsCallbackIndex[ 4 ];
static void * volatile pvTlsCallbackValue[ 4 ];
static volatile int iTlsReadBack;

static void prvTlsDeleteCallback( int iIndex,
                                  void * pvValue )
{
    if( iTlsCallbackCount < 4 )
    {
        iTlsCallbackIndex[ iTlsCallbackCount ] = iIndex;
        pvTlsCallbackValue[ iTlsCallbackCount ] = pvValue;
    }

    iTlsCallbackCount++;
}

static void prvTlsTask( void * pvParams )
{
    ( void ) pvParams;

    vTaskSetThreadLocalStoragePointerAndDelCallback( NULL, 0, POC_TLS_VALUE_0, prvTlsDeleteCallback );
    vTaskSetThreadLocalStoragePointerAndDelCallback( NULL, 2, POC_TLS_VALUE_2, prvTlsDeleteCallback );

    iTlsReadBack = ( pvTaskGetThreadLocalStoragePointer( NULL, 0 ) == POC_TLS_VALUE_0 ) &&
                   ( pvTaskGetThreadLocalStoragePointer( NULL, 2 ) == POC_TLS_VALUE_2 );

    vTaskDelete( NULL );
}

static void prvTestTls( void )
{
    int iHitsForSlot0 = 0;
    int iHitsForSlot2 = 0;
    int i;

    iTlsCallbackCount = 0;
    iTlsReadBack = 0;

    ( void ) xTaskCreate( prvTlsTask, "tls", 8192, NULL, POC_HELPER_PRIO, NULL );

    /* Blocking here lets the idle task run, which is where the kernel reaps a
     * self-deleted task and where the callbacks are invoked. */
    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    prvCheck( "tls_set_get", iTlsReadBack != 0, "read back both slots" );

    for( i = 0; i < iTlsCallbackCount && i < 4; i++ )
    {
        if( ( iTlsCallbackIndex[ i ] == 0 ) && ( pvTlsCallbackValue[ i ] == POC_TLS_VALUE_0 ) )
        {
            iHitsForSlot0++;
        }

        if( ( iTlsCallbackIndex[ i ] == 2 ) && ( pvTlsCallbackValue[ i ] == POC_TLS_VALUE_2 ) )
        {
            iHitsForSlot2++;
        }
    }

    prvCheck( "tls_delete_callback",
              ( iTlsCallbackCount == 2 ) && ( iHitsForSlot0 == 1 ) && ( iHitsForSlot2 == 1 ),
              "%d callback(s) after the task deleted itself, slot0=%d slot2=%d",
              iTlsCallbackCount, iHitsForSlot0, iHitsForSlot2 );
}

/*-----------------------------------------------------------
 * 6b. Deleting somebody else's task
 *
 * fmrb_app_kill()'s last resort. The POSIX port cannot do this (it reaches for
 * pthread_cancel), so this port's way of doing it -- waking the victim where it
 * is parked, with a flag that makes it leave -- is new code and gets its own
 * check: the victim must be gone, its deletion callback must have run, and the
 * deleter (this task) must come back rather than hang in the join.
 *----------------------------------------------------------*/

static volatile int iVictimStarted;

static void prvVictimTask( void * pvParams )
{
    ( void ) pvParams;

    vTaskSetThreadLocalStoragePointerAndDelCallback( NULL, 1, POC_TLS_VALUE_1, prvTlsDeleteCallback );
    iVictimStarted = 1;

    /* Blocked forever: only vTaskDelete() from outside gets it out of here. */
    for( ; ; )
    {
        ( void ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
    }
}

static void prvTestDeleteOtherTask( void )
{
    TaskHandle_t xVictim = NULL;
    UBaseType_t uxBefore;
    UBaseType_t uxAfter;

    iTlsCallbackCount = 0;
    iVictimStarted = 0;

    ( void ) xTaskCreate( prvVictimTask, "victim", 8192, NULL, POC_HELPER_PRIO, &xVictim );
    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    uxBefore = uxTaskGetNumberOfTasks();
    vTaskDelete( xVictim );
    uxAfter = uxTaskGetNumberOfTasks();
    vTaskDelay( pdMS_TO_TICKS( 50 ) );

    prvCheck( "delete_other_task",
              iVictimStarted && ( uxAfter == uxBefore - 1 ) && ( iTlsCallbackCount == 1 ) &&
              ( iTlsCallbackIndex[ 0 ] == 1 ) && ( pvTlsCallbackValue[ 0 ] == POC_TLS_VALUE_1 ),
              "task count %u -> %u, %d deletion callback(s), the deleter returned",
              ( unsigned ) uxBefore, ( unsigned ) uxAfter, iTlsCallbackCount );
}

/*-----------------------------------------------------------
 * 7. Message buffers (what fmrb_hal_link_local.c carries gfx commands on)
 *----------------------------------------------------------*/

static MessageBufferHandle_t xTestMessageBuffer;

static void prvMessageWriterTask( void * pvParams )
{
    ( void ) pvParams;
    vTaskDelay( pdMS_TO_TICKS( 40 ) );
    ( void ) xMessageBufferSend( xTestMessageBuffer, "late", 4, 0 );
    vTaskDelete( NULL );
}

static void prvTestMessageBuffer( void )
{
    static uint8_t ucBig[ 100 ];
    static uint8_t ucGot[ 256 ];
    size_t xSent;
    size_t xReceived;
    int iBigOk;
    double dStart;
    double dElapsed;
    size_t i;

    for( i = 0; i < sizeof( ucBig ); i++ )
    {
        ucBig[ i ] = ( uint8_t ) i;
    }

    xTestMessageBuffer = xMessageBufferCreate( 512 );

    xSent = xMessageBufferSend( xTestMessageBuffer, "hello", 5, 0 );
    xSent += xMessageBufferSend( xTestMessageBuffer, ucBig, sizeof( ucBig ), 0 );

    xReceived = xMessageBufferReceive( xTestMessageBuffer, ucGot, sizeof( ucGot ), 0 );
    prvCheck( "msgbuf_short",
              ( xSent == 5 + sizeof( ucBig ) ) && ( xReceived == 5 ) && ( memcmp( ucGot, "hello", 5 ) == 0 ),
              "sent %u bytes in two messages, first came back as %u bytes",
              ( unsigned ) xSent, ( unsigned ) xReceived );

    xReceived = xMessageBufferReceive( xTestMessageBuffer, ucGot, sizeof( ucGot ), 0 );
    iBigOk = ( xReceived == sizeof( ucBig ) ) && ( memcmp( ucGot, ucBig, sizeof( ucBig ) ) == 0 );
    prvCheck( "msgbuf_long", iBigOk, "second message came back as %u bytes, content %s",
              ( unsigned ) xReceived, iBigOk ? "intact" : "wrong" );

    /* A receive that has to wait for a writer. */
    ( void ) xTaskCreate( prvMessageWriterTask, "mbwrite", 8192, NULL, POC_HELPER_PRIO, NULL );
    dStart = prvNow();
    xReceived = xMessageBufferReceive( xTestMessageBuffer, ucGot, sizeof( ucGot ), pdMS_TO_TICKS( 500 ) );
    dElapsed = prvNow() - dStart;
    prvCheck( "msgbuf_blocking_receive",
              ( xReceived == 4 ) && ( memcmp( ucGot, "late", 4 ) == 0 ) &&
              ( dElapsed >= 35.0 ) && ( dElapsed <= 70.0 ),
              "got %u bytes after %.1f ms (want 4 after 35..70)", ( unsigned ) xReceived, dElapsed );

    vTaskDelay( pdMS_TO_TICKS( 20 ) );
    vMessageBufferDelete( xTestMessageBuffer );
}

/*-----------------------------------------------------------
 * 8. The cooperative limit, stated as a test
 *
 * A task that spins holds the CPU: a higher-priority task whose delay expires
 * meanwhile does not run until the spinner yields. This is the port's defining
 * behaviour, so it is asserted rather than merely tolerated -- if it ever
 * changes, this check is where it shows up.
 *----------------------------------------------------------*/

#define POC_SPIN_MS      ( 150.0 )
#define POC_WATCH_DELAY_MS ( 20 )

static volatile double dSpinYieldedAt;
static volatile double dWatcherRanAt;
static volatile double dWatcherBlockedAt;

static void prvSpinnerTask( void * pvParams )
{
    double dUntil = prvNow() + POC_SPIN_MS;
    volatile unsigned long ulBurn = 0;

    ( void ) pvParams;

    while( prvNow() < dUntil )
    {
        ulBurn++;
    }

    dSpinYieldedAt = prvNow();
    taskYIELD();
    vTaskDelete( NULL );
}

static void prvWatcherTask( void * pvParams )
{
    ( void ) pvParams;
    dWatcherBlockedAt = prvNow();
    vTaskDelay( pdMS_TO_TICKS( POC_WATCH_DELAY_MS ) );
    dWatcherRanAt = prvNow();
    vTaskDelete( NULL );
}

static void prvTestCooperativeLimit( void )
{
    double dHeldOff;
    double dAfterYield;

    dSpinYieldedAt = 0.0;
    dWatcherRanAt = 0.0;
    dWatcherBlockedAt = 0.0;

    /* Both are below this task, so neither starts until we block. The watcher
     * is above the spinner. */
    ( void ) xTaskCreate( prvWatcherTask, "watcher", 8192, NULL, POC_MAIN_PRIO - 1, NULL );
    ( void ) xTaskCreate( prvSpinnerTask, "spinner", 8192, NULL, POC_MAIN_PRIO - 2, NULL );

    vTaskDelay( pdMS_TO_TICKS( 400 ) );

    dHeldOff = dWatcherRanAt - dWatcherBlockedAt;
    dAfterYield = dWatcherRanAt - dSpinYieldedAt;

    prvCheck( "cooperative_holdoff", dHeldOff >= ( POC_SPIN_MS * 0.8 ),
              "a %d ms delay took %.1f ms because a lower-priority task was spinning",
              POC_WATCH_DELAY_MS, dHeldOff );
    prvCheck( "cooperative_release", ( dAfterYield >= 0.0 ) && ( dAfterYield < 15.0 ),
              "the watcher ran %.1f ms after taskYIELD() (want under 15)", dAfterYield );
}

/*-----------------------------------------------------------
 * 9. Tick against the wall clock over a long run
 *----------------------------------------------------------*/

static void prvTickLoadTask( void * pvParams )
{
    TickType_t xDelay = ( TickType_t ) ( intptr_t ) pvParams;
    double dUntil = prvNow() + POC_LONG_RUN_MS + 200.0;

    while( prvNow() < dUntil )
    {
        vTaskDelay( xDelay );
    }

    vTaskDelete( NULL );
}

static void prvTestTickVersusRealTime( void )
{
    TickType_t xTickStart;
    TickType_t xTickEnd;
    double dStart;
    double dWall;
    double dTicks;
    double dDrift;

    ( void ) xTaskCreate( prvTickLoadTask, "load3", 8192, ( void * ) ( intptr_t ) pdMS_TO_TICKS( 3 ),
                          POC_MAIN_PRIO - 1, NULL );
    ( void ) xTaskCreate( prvTickLoadTask, "load7", 8192, ( void * ) ( intptr_t ) pdMS_TO_TICKS( 7 ),
                          POC_MAIN_PRIO - 2, NULL );

    xTickStart = xTaskGetTickCount();
    dStart = prvNow();
    vTaskDelay( pdMS_TO_TICKS( POC_LONG_RUN_MS ) );
    xTickEnd = xTaskGetTickCount();
    dWall = prvNow() - dStart;

    dTicks = ( double ) ( xTickEnd - xTickStart ) * ( double ) portTICK_PERIOD_MS;
    dDrift = dTicks - dWall;

    prvCheck( "tick_tracks_real_time", ( dDrift > -50.0 ) && ( dDrift < 50.0 ),
              "%.0f ms of ticks over %.0f ms of wall clock (drift %.1f ms, want within 50)",
              dTicks, dWall, dDrift );

    vTaskDelay( pdMS_TO_TICKS( 300 ) );
}

/*-----------------------------------------------------------
 * 10. What happens when the pthread pool runs out
 *
 * Emscripten hands out Workers from a pool fixed at link time. Creating more
 * tasks than that has to go back to the main thread, so this measures it rather
 * than assuming: the numbers feed the pool sizing for P4a.
 *----------------------------------------------------------*/

static SemaphoreHandle_t xPoolExitSemaphore;
static volatile int iPoolTasksRunning;

static void prvPoolTask( void * pvParams )
{
    ( void ) pvParams;
    iPoolTasksRunning++;
    ( void ) xSemaphoreTake( xPoolExitSemaphore, portMAX_DELAY );
    vTaskDelete( NULL );
}

static void prvTestThreadPool( void )
{
    static TaskHandle_t xHandles[ POC_POOL_TASKS ];
    int iCreated = 0;
    int i;
    double dSlowest = 0.0;
    int iSlowestIndex = -1;
    char cName[ 16 ];

    xPoolExitSemaphore = xSemaphoreCreateCounting( POC_POOL_TASKS, 0 );
    iPoolTasksRunning = 0;

    for( i = 0; i < POC_POOL_TASKS; i++ )
    {
        double dStart = prvNow();
        double dTook;
        BaseType_t xResult;

        snprintf( cName, sizeof( cName ), "pool%d", i );
        xResult = xTaskCreate( prvPoolTask, cName, 65536, NULL, POC_MAIN_PRIO - 1, &xHandles[ i ] );
        dTook = prvNow() - dStart;

        if( xResult != pdPASS )
        {
            break;
        }

        iCreated++;

        if( dTook > dSlowest )
        {
            dSlowest = dTook;
            iSlowestIndex = i;
        }
    }

    /* Let them all get going, then let them all out. */
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    for( i = 0; i < iCreated; i++ )
    {
        ( void ) xSemaphoreGive( xPoolExitSemaphore );
    }

    vTaskDelay( pdMS_TO_TICKS( 400 ) );

    prvCheck( "thread_pool_exhaustion",
              ( iCreated == POC_POOL_TASKS ) && ( iPoolTasksRunning == POC_POOL_TASKS ),
              "created %d/%d tasks, %d reached their entry point; slowest creation %.1f ms (task #%d)",
              iCreated, POC_POOL_TASKS, iPoolTasksRunning, dSlowest, iSlowestIndex );

    vSemaphoreDelete( xPoolExitSemaphore );
}

/*-----------------------------------------------------------*/

static void prvPocTask( void * pvParams )
{
    ( void ) pvParams;

    printf( "POC START tick_rate=%u portTICK_PERIOD_MS=%u\n",
            ( unsigned ) configTICK_RATE_HZ, ( unsigned ) portTICK_PERIOD_MS );
    fflush( stdout );

    prvTestTasksAndDelay();
    prvTestNotifications();
    prvTestQueue();
    prvTestCountingSemaphore();
    prvTestMutex();
    prvTestTls();
    prvTestDeleteOtherTask();
    prvTestMessageBuffer();
    prvTestCooperativeLimit();
    prvTestThreadPool();
    prvTestTickVersusRealTime();

    printf( "POC STATS max_tick_burst=%u catchup_calls=%llu free_heap=%u\n",
            ( unsigned ) ulPortGetMaxTickBurst(),
            ( unsigned long long ) ullPortGetTickCatchUpCalls(),
            ( unsigned ) xPortGetFreeHeapSize() );
    printf( "POC RESULT: %d/%d\n", iChecksPassed, iChecksRun );
    fflush( stdout );

    emscripten_force_exit( ( iChecksPassed == iChecksRun ) ? 0 : 1 );
}

int main( void )
{
    if( xTaskCreate( prvPocTask, "poc", 131072, NULL, POC_MAIN_PRIO, NULL ) != pdPASS )
    {
        printf( "FAIL bootstrap - could not create the test task\n" );
        printf( "POC RESULT: 0/1\n" );
        return 1;
    }

    vTaskStartScheduler();

    printf( "FAIL bootstrap - the scheduler returned\n" );
    printf( "POC RESULT: 0/1\n" );
    return 1;
}
