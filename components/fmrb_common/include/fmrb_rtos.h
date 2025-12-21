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
#define FMRB_TRUE pdTRUE
#define FMRB_FALSE pdFALSE
#define FMRB_PASS pdPASS
#define FMRB_FAIL pdFAIL

#define FMRB_MS_TO_TICKS pdMS_TO_TICKS

// Task management
#define fmrb_task_create(fn, name, stack, param, prio, handle) \
    xTaskCreate(fn, name, stack, param, prio, handle)
#define fmrb_task_create_pinned(fn, name, stack, param, prio, handle, core) \
    xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core)
#define fmrb_task_delete(handle) vTaskDelete(handle)
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