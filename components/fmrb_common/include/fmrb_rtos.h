#pragma once

// FreeRTOS abstraction layer
// This header provides platform-independent wrappers for RTOS functionality

#ifdef __cplusplus
extern "C" {
#endif

// ESP32 Platform

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// Type aliases
typedef TaskHandle_t fmrb_task_handle_t;
typedef SemaphoreHandle_t fmrb_semaphore_t;
typedef QueueHandle_t fmrb_queue_t;
typedef UBaseType_t fmrb_task_priority_t;
typedef BaseType_t fmrb_base_type_t;
typedef TickType_t fmrb_tick_t;

// Re-export FreeRTOS type names (to avoid direct use)
#define TaskHandle_t TaskHandle_t
#define SemaphoreHandle_t SemaphoreHandle_t
#define QueueHandle_t QueueHandle_t
#define UBaseType_t UBaseType_t
#define BaseType_t BaseType_t
#define TickType_t TickType_t

// Re-export FreeRTOS constants
#define FMRB_TASK_PRIO_MAX configMAX_PRIORITIES
#define FMRB_TICK_MAX portMAX_DELAY
#define FMRB_MAX_DELAY portMAX_DELAY
#define FMRB_TRUE pdTRUE
#define FMRB_FALSE pdFALSE
#define FMRB_PASS pdPASS
#define FMRB_FAIL pdFAIL

#define FMRB_MS_TO_TICKS pdMS_TO_TICKS
// The other direction, for code that keeps a deadline in ticks but has to hand
// a millisecond timeout to an API (fmrb_msg_receive, for one).
#define FMRB_TICKS_TO_MS(ticks) ((uint32_t)(ticks) * portTICK_PERIOD_MS)

// Critical section types and macros
typedef portMUX_TYPE fmrb_spinlock_t;
#define FMRB_SPINLOCK_INITIALIZER portMUX_INITIALIZER_UNLOCKED
#define fmrb_enter_critical(spinlock) taskENTER_CRITICAL(spinlock)
#define fmrb_exit_critical(spinlock) taskEXIT_CRITICAL(spinlock)

// Task creation flags (bitfield)
#define FMRB_TASK_FLAG_NONE       0x00
#define FMRB_TASK_FLAG_PSRAM      0x01  // allocate stack on PSRAM
#define FMRB_TASK_FLAG_PINNED_0   0x02  // pin to core 0
#define FMRB_TASK_FLAG_PINNED_1   0x04  // pin to core 1

// Task status info returned by fmrb_task_get_status_list
typedef struct {
    const char *name;           // task name (pointer to FreeRTOS internal string)
    TaskHandle_t handle;
    uint32_t flags;             // FMRB_TASK_FLAG_* used at creation
    uint32_t stack_size;        // stack size in bytes (as requested)
    uint32_t stack_free;        // stack high water mark in bytes (minimum ever free)
    UBaseType_t priority;
    uint8_t active;             // 1 = running, 0 = deleted
} fmrb_task_info_t;

// Unified task creation with flags (implemented in fmrb_task.c)
// Automatically registers the task for monitoring.
fmrb_base_type_t fmrb_task_create_ex(
    TaskFunction_t fn, const char *name, uint32_t stack,
    void *param, UBaseType_t prio, TaskHandle_t *handle,
    uint32_t flags);

// Delete task and unregister from monitor (implemented in fmrb_task.c)
void fmrb_task_delete_ex(TaskHandle_t handle);

// Get snapshot of all tracked tasks. Returns number of entries written.
int fmrb_task_get_status_list(fmrb_task_info_t *out, int max_count);

// Print task status summary to log
void fmrb_task_dump_status(void);

// Legacy macros (kept for compatibility)
#define fmrb_task_create(fn, name, stack, param, prio, handle) \
    fmrb_task_create_ex(fn, name, stack, param, prio, handle, FMRB_TASK_FLAG_NONE)
#define fmrb_task_create_pinned(fn, name, stack, param, prio, handle, core) \
    fmrb_task_create_ex(fn, name, stack, param, prio, handle, \
        (core) == 0 ? FMRB_TASK_FLAG_PINNED_0 : FMRB_TASK_FLAG_PINNED_1)
#define fmrb_task_create_psram(fn, name, stack, param, prio, handle) \
    fmrb_task_create_ex(fn, name, stack, param, prio, handle, FMRB_TASK_FLAG_PSRAM)
#define fmrb_task_delete(handle) fmrb_task_delete_ex(handle)
#define fmrb_task_delay(ticks) vTaskDelay(ticks)
#define fmrb_task_delay_ms(ms) vTaskDelay(FMRB_MS_TO_TICKS(ms))
#define fmrb_task_get_current() xTaskGetCurrentTaskHandle()
#define fmrb_task_get_tick_count() xTaskGetTickCount()
#define fmrb_task_get_tls(handle, idx) pvTaskGetThreadLocalStoragePointer(handle, idx)
#define fmrb_task_set_tls(handle, idx, val) vTaskSetThreadLocalStoragePointer(handle, idx, val)
#define fmrb_task_set_tls_with_del(handle, idx, val, del) \
    vTaskSetThreadLocalStoragePointerAndDelCallback(handle, idx, val, del)
#define fmrb_task_suspend(handle) vTaskSuspend(handle)
#define fmrb_task_resume(handle) vTaskResume(handle)
#define fmrb_task_get_priority(handle) uxTaskPriorityGet(handle)
#define fmrb_task_get_stack_high_water_mark(handle) uxTaskGetStackHighWaterMark(handle)
#define fmrb_task_notify_give(handle) xTaskNotifyGive(handle)

// Core ID (ESP32 specific)
#define fmrb_get_core_id() xPortGetCoreID()

// Semaphore management
#define fmrb_semaphore_create_mutex() xSemaphoreCreateMutex()
#define fmrb_semaphore_create_binary() xSemaphoreCreateBinary()
#define fmrb_semaphore_create_counting(max, initial) xSemaphoreCreateCounting(max, initial)
#define fmrb_semaphore_take(sem, ticks) xSemaphoreTake(sem, ticks)
#define fmrb_semaphore_give(sem) xSemaphoreGive(sem)
#define fmrb_semaphore_delete(sem) vSemaphoreDelete(sem)

// Queue management
#define fmrb_queue_create(len, size) xQueueCreate(len, size)
#define fmrb_queue_send(queue, item, ticks) xQueueSend(queue, item, ticks)
#define fmrb_queue_receive(queue, item, ticks) xQueueReceive(queue, item, ticks)
#define fmrb_queue_delete(queue) vQueueDelete(queue)


#ifdef __cplusplus
}
#endif