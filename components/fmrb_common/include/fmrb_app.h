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
typedef struct basic_state basic_state_t;

#define FMRB_MAX_APP_NAME (32)
#define FMRB_MAX_PATH_LEN (128)

// Load mode for script loading
typedef enum {
    FMRB_LOAD_MODE_BYTECODE = 0,  // Load from precompiled bytecode
    FMRB_LOAD_MODE_FILE = 1,       // Load from source file
} fmrb_load_mode_t;

// State machine for app lifecycle (strict transitions enforced)
typedef enum {
    PROC_STATE_FREE = 0,        // Slot available
    PROC_STATE_INIT,            // Context allocated, VM initialized, ready to start
    PROC_STATE_RUNNING,         // VM running
    PROC_STATE_SUSPENDED,       // Temporarily suspended
    PROC_STATE_STOPPING,        // Shutdown requested
} fmrb_proc_state_t;

enum FMRB_APP_TYPE{
    APP_TYPE_KERNEL = 0,
    APP_TYPE_SYSTEM_APP,
    APP_TYPE_USER_APP,
    APP_TYPE_MAX
};

// VM Type for multi-VM support.
// Append only: the numbers are mirrored by the kernel's app-info snapshot
// (fmrb_spx_kernel.c / picoruby-fmrb-kernel kernel.c) and by the taskbar's
// colour table, so renumbering silently mislabels running apps.
typedef enum {
    FMRB_VM_TYPE_MRUBY = 0,      // PicoRuby/mruby
    FMRB_VM_TYPE_LUA,            // Lua
    FMRB_VM_TYPE_BASIC,          // BASIC
    FMRB_VM_TYPE_NATIVE,         // Native C function
    FMRB_VM_TYPE_MICROPYTHON,    // MicroPython
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
    fmrb_vm_type_t        vm_type;           // VM type (mruby, lua, basic, native)
    union {
        mrb_state*        mrb;               // mruby VM pointer
        lua_State*        lua;               // Lua VM pointer
        basic_state_t*    basic;             // BASIC VM pointer
        // MicroPython has no per-app VM handle: its state is global and only
        // one app may hold it (see fmrb_mp.h). This flag stands in for the
        // pointer so the create/destroy paths keep the same shape.
        bool              mp_active;
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
    uint8_t               z_order;           // Z-order (0=back, higher=front)
    uint16_t              canvas_id;         // Canvas ID (0 for headless apps)
    bool                  has_background_canvas; // Desktop only: has additional bg canvas (z=0)
    uint16_t              bg_canvas_id;      // Background canvas ID (0 if none)
    // Extra canvases created by the app at runtime (FmrbApp#create_canvas_gfx,
    // e.g. a hardware-scrolled map layer). Registered here so the kernel's
    // suspend/resume visibility control and the C-level cleanup on kill/crash
    // cover them like the main canvas (0 = empty slot).
#define FMRB_APP_MAX_EXTRA_CANVAS 2
    uint16_t              extra_canvas_ids[FMRB_APP_MAX_EXTRA_CANVAS];
    bool                  fullscreen;        // Fullscreen app flag
    bool                  resizable;         // Allow window resize (default: false)
    uint16_t              min_window_width;  // Per-app minimum width (0 = use global default)
    uint16_t              min_window_height; // Per-app minimum height (0 = use global default)
    bool                  rounded_corners;   // Window has rounded corners; if false, canvas is opaque (skips transparent compositing)

    // Load mode and data (replaces encoded user_data pointer tagging)
    fmrb_load_mode_t      load_mode;         // How to load the script
    void*                 load_data;         // Bytecode ptr or filepath ptr

    // Cooperative termination flag. Set by runtimes that poll APP_CONTROL
    // messages (Lua hook, BASIC run loop) when the kernel requests close
    // via {"cmd": "stop"}. The VM unwinds and the task exits normally so
    // canvas/queue cleanup in the task wrapper runs.
    volatile bool         should_exit;

    // Non-zero while the task is blocked in a synchronous round trip whose
    // reply context lives on its own stack (graphics sync commands, file
    // transfer). Deleting the task there would leave the host task or the
    // transport about to write into a stack that is being reused, so the
    // forced kill waits for this to clear instead. Incremented and
    // decremented by the waiting task itself, so no lock is needed; it is a
    // counter rather than a flag only to stay correct if a wait ever nests.
    volatile uint8_t      sync_io_depth;
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
    uint32_t              flags;            // FMRB_TASK_FLAG_* bitfield
    fmrb_base_type_t      core_affinity;    // -1 = no affinity, 0/1 = specific core (legacy)
    bool                  headless;         // Headless app flag (no graphics, no canvas)
    bool                  has_background_canvas; // Desktop only: create bg canvas (z=0)
    bool                  fullscreen;       // Fullscreen app (suspend others, no menu bar)
    bool                  resizable;        // Allow window resize (default: false)
    bool                  large_memory;     // Use LARGE memory pool (1MB)
    uint16_t              window_width;     // Window Width (if headless, =0)
    uint16_t              window_height;    // Window Height (if headless, =0)
    uint16_t              window_pos_x;
    uint16_t              window_pos_y;
    uint16_t              min_window_width;  // Per-app minimum width (0 = use global default)
    uint16_t              min_window_height; // Per-app minimum height (0 = use global default)
    bool                  rounded_corners;   // Window has rounded corners (default: true). false = opaque canvas, no transparent compositing
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

    // Spinel (NATIVE) only: begin/catch stack depth high-waters, the
    // observations SP_EXC_STACK_MAX / SP_CATCH_STACK_MAX are sized from.
    // 0 for other VM types.
    int32_t               exc_hw;
    int32_t               catch_hw;
} fmrb_app_info_t;

// Window info for hit testing
typedef struct {
    uint8_t               pid;              // Process ID
    char                  app_name[FMRB_MAX_APP_NAME];  // Application name
    uint16_t              x;                // Window X position
    uint16_t              y;                // Window Y position
    uint16_t              width;            // Window width
    uint16_t              height;           // Window height
    uint8_t               z_order;          // Z-order (0=back, higher=front)
    bool                  fullscreen;       // Fullscreen app flag
    bool                  resizable;        // Allow window resize
    uint16_t              min_width;        // Per-app minimum width (0 = use global default)
    uint16_t              min_height;       // Per-app minimum height (0 = use global default)
} fmrb_window_info_t;

// Core APIs
bool fmrb_app_init(void);
fmrb_err_t fmrb_app_spawn(const fmrb_spawn_attr_t* attr, int32_t* out_id);
fmrb_err_t fmrb_app_spawn_simple(const fmrb_spawn_attr_t* attr, int32_t* out_id);
bool fmrb_app_kill(int32_t id);
bool fmrb_app_stop(int32_t id);
bool fmrb_app_reap(int32_t id);  // External delete after self-cleanup (called from kernel)
bool fmrb_app_suspend(int32_t id);
bool fmrb_app_resume(int32_t id);
int32_t fmrb_app_ps(fmrb_app_info_t* list, int32_t max_count);

/**
 * @brief Process-set generation counter, bumped on every state transition.
 *
 * Cheap change detector for UI polls: call fmrb_app_ps() (which allocates)
 * only when this value moved since the last poll.
 */
uint32_t fmrb_app_proc_generation(void);

// Context access
static inline fmrb_app_task_context_t* fmrb_current(void) {
    return (fmrb_app_task_context_t*)fmrb_task_get_tls(fmrb_task_get_current(), FMRB_APP_TLS_INDEX);
}

fmrb_app_task_context_t* fmrb_app_get_context_by_id(int32_t id);

fmrb_err_t fmrb_app_spawn_app(const char* app_name, int32_t* out_pid);

/**
 * @brief Mark the calling task as being inside a synchronous round trip.
 *
 * Pairs with fmrb_app_sync_io_end(). Safe to call with no app context (the
 * kernel and host tasks use the same helpers), in which case it does nothing.
 */
static inline void fmrb_app_sync_io_begin(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx) ctx->sync_io_depth++;
}

/**
 * @brief End the region opened by fmrb_app_sync_io_begin().
 */
static inline void fmrb_app_sync_io_end(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx && ctx->sync_io_depth) ctx->sync_io_depth--;
}

/**
 * @brief Drain APP_CONTROL messages and set should_exit when a stop/exit is seen
 *
 * Non-blocking poll intended for Lua/Basic runtimes that would otherwise
 * ignore kernel messages. Any APP_CONTROL msg with cmd="stop" or cmd="exit"
 * flips ctx->should_exit so the runtime can unwind gracefully.
 *
 * @param ctx Task context (usually fmrb_current())
 * @return true if an exit has been requested (latched)
 */
bool fmrb_app_poll_exit_signal(fmrb_app_task_context_t* ctx);

/**
 * @brief Latch should_exit if this message is a stop/exit APP_CONTROL request
 *
 * For runtimes that read the app queue themselves: a loop that drains the queue
 * looking for HID events must hand every other message here, or a kernel stop
 * request is dropped and only a forced kill can end the app.
 *
 * Takes the message fields rather than fmrb_msg_t so this header keeps its
 * current (message-free) dependencies.
 *
 * @param ctx Task context (usually fmrb_current())
 * @param msg_type Message type as received (FMRB_MSG_TYPE_*)
 * @param payload Message payload
 * @param size Payload size in bytes
 * @return true if this message was an exit request
 */
bool fmrb_app_note_control_payload(fmrb_app_task_context_t* ctx, uint8_t msg_type,
                                   const uint8_t* payload, uint32_t size);

void* fmrb_app_get_current_est(void);
void fmrb_app_set_current_est(void* est);

int32_t fmrb_app_get_window_list(fmrb_window_info_t* list, int32_t max_count);
fmrb_err_t fmrb_app_bring_to_front(uint8_t pid);
fmrb_err_t fmrb_app_update_window_position(uint8_t pid, uint16_t x, uint16_t y);
fmrb_err_t fmrb_app_update_window_size(uint8_t pid, uint16_t width, uint16_t height);

// Last error info (stored in PSRAM static buffer)
const char* fmrb_app_get_last_error_name(void);
const char* fmrb_app_get_last_error_msg(void);

// Log every live VM's pool occupancy (estalloc used/free/fragmentation), one
// line per app. Each mruby VM and each Spinel instance owns a fixed mempool and
// dies with sp_oom_die / an allocation failure when it fills, so the headroom is
// only visible if it is printed: pair this with fmrb_task_dump_status(), whose
// IRAM/PSRAM totals say nothing about the per-pool budgets.
void fmrb_app_dump_vm_pools(void);