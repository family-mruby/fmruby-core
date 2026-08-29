/*
 * FreeRTOS configuration for the Emscripten (wasm) port.
 *
 * The values follow ESP-IDF's generated FreeRTOSConfig.h as resolved by the
 * Linux simulation build (config/sdkconfig.defaults.linux), so that code
 * written against the device behaves the same here. The differences are listed
 * where they occur; the two that matter are:
 *
 *   - configCHECK_FOR_STACK_OVERFLOW is 0. Tasks run on pthread stacks that
 *     Emscripten allocates, not on the block the kernel allocates, so the
 *     kernel's canary would be watching memory nothing writes to.
 *   - configUSE_IDLE_HOOK is 1. The idle hook is where real time is turned into
 *     ticks when every task is blocked (see port.c, "tick supply").
 */
#pragma once

#ifndef __ASSEMBLER__
    #include <assert.h>
    #include <stdint.h>
#endif

#include "sdkconfig.h"

/* ------------------ Scheduler Related -------------------- */

/* Kept at 1: the API-boundary switch (a task blocks, a higher-priority task is
 * readied) still works exactly as on the device. What a cooperative port loses
 * is only the asynchronous tick prying a running task loose. */
#define configUSE_PREEMPTION                     1
#define configUSE_TICKLESS_IDLE                  0
#define configCPU_CLOCK_HZ                       ( 1000000 )
#define configTICK_RATE_HZ                       CONFIG_FREERTOS_HZ
#define configMAX_PRIORITIES                     ( 25 )
#define configUSE_TIME_SLICING                   1
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  0
#define configKERNEL_INTERRUPT_PRIORITY          1
#define configNUMBER_OF_CORES                    CONFIG_FREERTOS_NUMBER_OF_CORES
#define configNUM_CORES                          configNUMBER_OF_CORES

/* ------------- Synchronization Primitives ---------------- */

#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              1
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_QUEUE_SETS                     1
#define configQUEUE_REGISTRY_SIZE                CONFIG_FREERTOS_QUEUE_REGISTRY_SIZE
#define configUSE_TASK_NOTIFICATIONS             1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES

/* ----------------------- System -------------------------- */

#define configMAX_TASK_NAME_LEN                  CONFIG_FREERTOS_MAX_TASK_NAME_LEN
#define configMINIMAL_STACK_SIZE                 CONFIG_FREERTOS_IDLE_TASK_STACKSIZE

/* With deletion callbacks on, the array is twice the usable slot count: the
 * second half holds the callbacks (ESP-IDF's layout, kept so that
 * vTaskSetThreadLocalStoragePointerAndDelCallback means the same thing). */
#if CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS
    #define configNUM_THREAD_LOCAL_STORAGE_POINTERS    ( CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS * 2 )
#else
    #define configNUM_THREAD_LOCAL_STORAGE_POINTERS    CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS
#endif

#define configSTACK_DEPTH_TYPE                   uint32_t
#define configENABLE_BACKWARD_COMPATIBILITY      0
#define configASSERT( a )                        assert( a )
#define configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H 1

/* ----------------------- Memory  ------------------------- */

#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configAPPLICATION_ALLOCATED_HEAP         1
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0

/* ------------------------ Hooks -------------------------- */

/*
 * 0, but the idle task still calls into the port: ESP-IDF's tasks.c calls
 * esp_vApplicationIdleHook() unconditionally, and that is where this port turns
 * elapsed real time into ticks (port.c). configUSE_IDLE_HOOK would add a second,
 * upstream-named hook on top of it, which we do not need.
 */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0

/* A task's pthread stack is not the kernel's stack block, so the kernel cannot
 * see an overflow of it. Emscripten traps the real overflow instead
 * (-sSTACK_OVERFLOW_CHECK). uxTaskGetStackHighWaterMark is meaningless here for
 * the same reason and always reports the block as untouched. */
#define configCHECK_FOR_STACK_OVERFLOW           0
#define configRECORD_STACK_HIGH_ADDRESS          1

/* ------------------- Run-time Stats ---------------------- */

#define configGENERATE_RUN_TIME_STATS            0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configUSE_TRACE_FACILITY                 1
#define configRUN_TIME_COUNTER_TYPE              uint32_t

/* -------------------- Co-routines  ----------------------- */

#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          2

/* ------------------- Software Timer ---------------------- */

/* No user of software timers in the codebase (doc/wasm/plan.md inventory), and
 * timers.c is not vendored. */
#define configUSE_TIMERS                         0

/* ------------------------ List --------------------------- */

#define configLIST_VOLATILE                      volatile
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 0

/* -------------------- API Includes ----------------------- */

#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetIdleTaskHandle           1
#define INCLUDE_xTaskAbortDelay                  1
#define INCLUDE_xSemaphoreGetMutexHolder         1
#define INCLUDE_xTaskGetHandle                   1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_eTaskGetState                    1
#define INCLUDE_xTaskResumeFromISR               1
#define INCLUDE_xTimerPendFunctionCall           0
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1

/* -------------------------------------------------- IDF FreeRTOS ----------- */

#if CONFIG_FREERTOS_TLSP_DELETION_CALLBACKS
    #define configTHREAD_LOCAL_STORAGE_DELETE_CALLBACKS    1
#endif
#if CONFIG_FREERTOS_CHECK_MUTEX_GIVEN_BY_OWNER
    #define configCHECK_MUTEX_GIVEN_BY_OWNER               1
#endif

#define portNUM_PROCESSORS                       configNUMBER_OF_CORES

/* -------------------- Trace Macros ----------------------- */

/*
 * ESP-IDF added these three call sites to queue.c but only supplies defaults for
 * them through its SystemView integration, which this build does not include.
 */
#define traceQUEUE_GIVE_FROM_ISR( pxQueue )           do { ( void ) ( pxQueue ); } while( 0 )
#define traceQUEUE_GIVE_FROM_ISR_FAILED( pxQueue )    do { ( void ) ( pxQueue ); } while( 0 )
#define traceQUEUE_SEMAPHORE_RECEIVE( pxQueue )       do { ( void ) ( pxQueue ); } while( 0 )
