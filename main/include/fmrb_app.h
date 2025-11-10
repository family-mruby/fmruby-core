#pragma once

#include "fmrb_mem.h"
#include "fmrb_hal.h"
#include "fmrb_task_config.h"
#include <picoruby.h>
#include <stdbool.h>
#include <stdint.h>

// State machine for app lifecycle (strict transitions enforced)
typedef enum {
    PROC_STATE_FREE = 0,        // Slot available
    PROC_STATE_ALLOCATED,       // Context allocated, initializing
    PROC_STATE_INIT,            // Initialization complete, ready to start
    PROC_STATE_RUNNING,         // VM running
    PROC_STATE_SUSPENDED,       // Temporarily suspended
    PROC_STATE_STOPPING,        // Shutdown requested
    PROC_STATE_ZOMBIE,          // Terminated, awaiting cleanup
} fmrb_proc_state_t;

enum FMRB_APP_TYPE{
    APP_TYPE_KERNEL = 0,
    APP_TYPE_SYSTEM_APP,
    APP_TYPE_USER_APP,
    APP_TYPE_MAX
};

// App load mode
typedef enum {
    FMRB_LOAD_MODE_IREP = 0,     // Load from irep bytecode
    FMRB_LOAD_MODE_FILE,          // Load from text file
} fmrb_load_mode_t;

// FreeRTOS TLS slot index for app context
#define FMRB_APP_TLS_INDEX 1

// Type-safe app task context
typedef struct {
    fmrb_proc_id_t        app_id;
    fmrb_proc_state_t     state;
    enum FMRB_APP_TYPE    type;
    char                  app_name[32];      // UTF-8, null-terminated
    mrb_state*            mrb;               // Type-safe mruby VM pointer
    void*                 est;               // Estalloc Pointer
    enum FMRB_MEM_POOL_ID mempool_id;
    fmrb_semaphore_t      semaphore;         // Type-safe semaphore
    fmrb_task_handle_t    task;              // FreeRTOS task handle
    uint32_t              gen;               // Generation counter for reuse detection
    bool                  headless;          // Headless app flag (no graphics, no canvas)
    uint16_t              window_width;      // Window Width(if headless, =0)
    uint16_t              window_height;     // Window Height(if headless, =0)
    uint16_t              window_pos_x;
    uint16_t              window_pos_y;
    void*                 user_data;         // Application-specific data
} fmrb_app_task_context_t;

// Spawn attributes for creating new app task
typedef struct {
    fmrb_proc_id_t        app_id;           // Fixed slot ID
    enum FMRB_APP_TYPE    type;
    const char*           name;
    fmrb_load_mode_t      load_mode;        // Load mode (irep or file)
    union {
        const unsigned char*  irep;         // Bytecode (when load_mode == FMRB_LOAD_MODE_IREP)
        const char*           filepath;     // File path (when load_mode == FMRB_LOAD_MODE_FILE)
    };
    uint32_t              stack_words;      // Stack size in words (not bytes)
    fmrb_task_priority_t  priority;
    fmrb_base_type_t      core_affinity;    // -1 = no affinity, 0/1 = specific core
    bool                  headless;         // Headless app flag (no graphics, no canvas)
    uint16_t              window_pos_x;
    uint16_t              window_pos_y;    
} fmrb_spawn_attr_t;

// App info for ps-style listing
typedef struct {
    fmrb_proc_id_t        app_id;
    fmrb_proc_state_t     state;
    enum FMRB_APP_TYPE    type;
    char                  app_name[32];
    uint32_t              gen;
    fmrb_task_handle_t    task;
    fmrb_task_priority_t  stack_high_water; // Remaining stack (words)
} fmrb_app_info_t;

// Core APIs
bool fmrb_app_init(void);
fmrb_err_t fmrb_app_spawn(const fmrb_spawn_attr_t* attr, int32_t* out_id);
fmrb_err_t fmrb_app_spawn_simple(const fmrb_spawn_attr_t* attr, int32_t* out_id);
bool fmrb_app_kill(int32_t id);
bool fmrb_app_stop(int32_t id);
bool fmrb_app_suspend(int32_t id);
bool fmrb_app_resume(int32_t id);
int32_t fmrb_app_ps(fmrb_app_info_t* list, int32_t max_count);

// Context access (fast)
static inline fmrb_app_task_context_t* fmrb_current(void) {
    return (fmrb_app_task_context_t*)fmrb_task_get_tls(NULL, FMRB_APP_TLS_INDEX);
}

fmrb_app_task_context_t* fmrb_app_get_context_by_id(int32_t id);

fmrb_err_t fmrb_app_spawn_default_app(const char* app_name);

void* fmrb_app_get_current_est(void);
void fmrb_app_set_current_est(void* est);