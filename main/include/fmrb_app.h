#pragma once

#include "fmrb_mem.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <picoruby.h>
#include <stdbool.h>
#include <stdint.h>

// FreeRTOS TLS slot index for app context
#define FMRB_APP_TLS_INDEX 0

// Maximum number of concurrent apps
#define FMRB_MAX_APPS PROC_ID_MAX

enum FMRB_PROC_ID{
    PROC_ID_KERNEL = 0,
    PROC_ID_SYSTEM_APP,
    PROC_ID_USER_APP0,
    PROC_ID_USER_APP1,
    PROC_ID_USER_APP2,
    PROC_ID_MAX
};

// State machine for app lifecycle (strict transitions enforced)
enum FMRB_PROC_STATE{
    PROC_STATE_FREE = 0,        // Slot available
    PROC_STATE_ALLOCATED,       // Context allocated, initializing
    PROC_STATE_INIT,            // Initialization complete, ready to start
    PROC_STATE_RUNNING,         // VM running
    PROC_STATE_SUSPENDED,       // Temporarily suspended
    PROC_STATE_STOPPING,        // Shutdown requested
    PROC_STATE_ZOMBIE,          // Terminated, awaiting cleanup
};

enum FMRB_APP_TYPE{
    APP_TYPE_KERNEL = 0,
    APP_TYPE_SYSTEM_APP,
    APP_TYPE_USER_APP,
    APP_TYPE_MAX
};

// Type-safe app task context
typedef struct {
    enum FMRB_PROC_STATE  state;
    enum FMRB_PROC_ID     app_id;
    enum FMRB_APP_TYPE    type;
    char                  app_name[32];      // UTF-8, null-terminated
    mrb_state*            mrb;               // Type-safe mruby VM pointer
    enum FMRB_MEM_POOL_ID mempool_id;
    SemaphoreHandle_t     semaphore;         // Type-safe semaphore
    TaskHandle_t          task;              // FreeRTOS task handle
    uint32_t              gen;               // Generation counter for reuse detection
    QueueHandle_t         event_queue;       // Event queue for inter-task communication
    void*                 user_data;         // Application-specific data
} fmrb_app_task_context_t;

// Spawn attributes for creating new app task
typedef struct {
    enum FMRB_PROC_ID     app_id;           // Fixed slot ID
    enum FMRB_APP_TYPE    type;
    const char*           name;
    const unsigned char*  irep;             // Bytecode
    uint32_t              stack_words;      // Stack size in words (not bytes)
    UBaseType_t           priority;
    BaseType_t            core_affinity;    // -1 = no affinity, 0/1 = specific core
    size_t                event_queue_len;  // 0 = no queue
} fmrb_spawn_attr_t;

// App info for ps-style listing
typedef struct {
    enum FMRB_PROC_ID     app_id;
    enum FMRB_PROC_STATE  state;
    enum FMRB_APP_TYPE    type;
    char                  app_name[32];
    uint32_t              gen;
    TaskHandle_t          task;
    UBaseType_t           stack_high_water; // Remaining stack (words)
} fmrb_app_info_t;

// Core APIs
void fmrb_app_init(void);
bool fmrb_app_spawn(const fmrb_spawn_attr_t* attr, int32_t* out_id);
bool fmrb_app_spawn_simple(const fmrb_spawn_attr_t* attr, int32_t* out_id);
bool fmrb_app_kill(int32_t id);
bool fmrb_app_stop(int32_t id);
bool fmrb_app_suspend(int32_t id);
bool fmrb_app_resume(int32_t id);
int32_t fmrb_app_ps(fmrb_app_info_t* list, int32_t max_count);

// Context access (fast)
static inline fmrb_app_task_context_t* fmrb_current(void) {
    return (fmrb_app_task_context_t*)pvTaskGetThreadLocalStoragePointer(NULL, FMRB_APP_TLS_INDEX);
}

fmrb_app_task_context_t* fmrb_app_get_context_by_id(int32_t id);

// Event posting
bool fmrb_post_event(int32_t id, const void* event, size_t size, TickType_t timeout);
bool fmrb_broadcast(const void* event, size_t size, TickType_t timeout);

