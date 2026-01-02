#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fmrb_err.h"
#include "fmrb_mem_config.h"
#include "fmrb_task_config.h"
#include "fmrb_rtos.h"

// Forward declarations to avoid circular dependencies with other components
typedef struct lua_State lua_State;
typedef struct mrb_state mrb_state;

#define FMRB_MAX_APP_NAME (32)
#define FMRB_MAX_PATH_LEN (256)

// Load mode for script loading
typedef enum {
    FMRB_LOAD_MODE_BYTECODE = 0,  // Load from precompiled bytecode
    FMRB_LOAD_MODE_FILE = 1,       // Load from source file
} fmrb_load_mode_t;

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

// VM Type for multi-VM support
typedef enum {
    FMRB_VM_TYPE_MRUBY = 0,      // PicoRuby/mruby
    FMRB_VM_TYPE_LUA,            // Lua
    FMRB_VM_TYPE_NATIVE,         // Native C function
    FMRB_VM_TYPE_MAX
} fmrb_vm_type_t;

// FreeRTOS TLS slot index for app context
#define FMRB_APP_TLS_INDEX 1

// Type-safe app task context
typedef struct fmrb_app_task_context_s {
    fmrb_proc_id_t        app_id;
    fmrb_proc_state_t     state;
    enum FMRB_APP_TYPE    type;
    char                  app_name[FMRB_MAX_APP_NAME];      // UTF-8, null-terminated
    char                  filepath[FMRB_MAX_PATH_LEN];      // Script file path (for FILE load mode)

    // Multi-VM support
    fmrb_vm_type_t        vm_type;           // VM type (mruby, lua, native)
    union {
        mrb_state*        mrb;               // mruby VM pointer
        lua_State*        lua;               // Lua VM pointer
        void*             vm_generic;        // Generic VM pointer
    };

    void*                 est;               // Estalloc Pointer
    enum FMRB_MEM_POOL_ID mempool_id;        // Memory Pool ID
    fmrb_mem_handle_t     mem_handle;        // Memory alloc handle
    fmrb_semaphore_t      semaphore;         // Type-safe semaphore
    fmrb_task_handle_t    task;              // FreeRTOS task handle
    uint32_t              gen;               // Generation counter for reuse detection
    bool                  headless;          // Headless app flag (no graphics, no canvas)
    uint16_t              window_width;      // Window Width(if headless, =0)
    uint16_t              window_height;     // Window Height(if headless, =0)
    uint16_t              window_pos_x;
    uint16_t              window_pos_y;

    // Load mode and data (replaces encoded user_data pointer tagging)
    fmrb_load_mode_t      load_mode;         // How to load the script
    void*                 load_data;         // Bytecode ptr or filepath ptr
} fmrb_app_task_context_t;

// Spawn attributes for creating new app task
typedef struct {
    fmrb_proc_id_t        app_id;           // Fixed slot ID
    enum FMRB_APP_TYPE    type;
    const char*           name;

    // Multi-VM support
    fmrb_vm_type_t        vm_type;          // VM type (mruby, lua, native)
    fmrb_load_mode_t      load_mode;        // Load mode (bytecode, file)
    union {
        const unsigned char*  bytecode;     // Bytecode (mruby irep, Lua chunk, etc.)
        const char*           filepath;     // Script file path
        void (*native_func)(void*);         // Native C function pointer
    };

    uint32_t              stack_words;      // Stack size in words (not bytes)
    fmrb_task_priority_t  priority;
    fmrb_base_type_t      core_affinity;    // -1 = no affinity, 0/1 = specific core
    bool                  headless;         // Headless app flag (no graphics, no canvas)
    uint16_t              window_width;     // Window Width (if headless, =0)
    uint16_t              window_height;    // Window Height (if headless, =0)
    uint16_t              window_pos_x;
    uint16_t              window_pos_y;
} fmrb_spawn_attr_t;

// App info for ps-style listing
typedef struct {
    fmrb_proc_id_t        app_id;
    fmrb_proc_state_t     state;
    enum FMRB_APP_TYPE    type;
    char                  app_name[FMRB_MAX_APP_NAME];
    uint32_t              gen;
    fmrb_task_handle_t    task;
    fmrb_task_priority_t  stack_high_water; // Remaining stack (words)

    // Memory statistics
    fmrb_vm_type_t        vm_type;          // VM type (mruby, lua, native)
    size_t                mem_total;        // Total memory pool size
    size_t                mem_used;         // Used memory
    size_t                mem_free;         // Free memory
    int32_t               mem_frag;         // Fragmentation count or block count
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

// Context access
static inline fmrb_app_task_context_t* fmrb_current(void) {
    return (fmrb_app_task_context_t*)fmrb_task_get_tls(fmrb_task_get_current(), FMRB_APP_TLS_INDEX);
}

fmrb_app_task_context_t* fmrb_app_get_context_by_id(int32_t id);

fmrb_err_t fmrb_app_spawn_app(const char* app_name);

void* fmrb_app_get_current_est(void);
void fmrb_app_set_current_est(void* est);