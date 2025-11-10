#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_log.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_kernel.h"

static const char *TAG = "fmrb_app";

// Max script file size (configurable)
#define MAX_SCRIPT_FILE_SIZE (64 * 1024)  // 64KB

// ============================================================================
// Global state (zero-initialized at boot)
// ============================================================================

// Fixed-size context pool
static fmrb_app_task_context_t g_ctx_pool[FMRB_MAX_APPS];

// Mutex for protecting context pool access
static fmrb_semaphore_t g_ctx_lock = NULL;

// State transition strings for debugging
static const char* state_names[] = {
    "FREE", "ALLOCATED", "INIT", "RUNNING", "SUSPENDED", "STOPPING", "ZOMBIE"
};

// ============================================================================
// Internal helpers
// ============================================================================

/**
 * Get human-readable state name
 */
static inline const char* state_str(fmrb_proc_state_t state) {
    return (state >= 0 && state < sizeof(state_names)/sizeof(state_names[0]))
           ? state_names[state] : "UNKNOWN";
}

/**
 * Validate state transition
 */
static bool is_valid_transition(fmrb_proc_state_t from, fmrb_proc_state_t to) {
    // State machine: FREE -> ALLOCATED -> INIT -> RUNNING <-> SUSPENDED
    //                                              RUNNING -> STOPPING -> ZOMBIE -> FREE
    switch (from) {
        case PROC_STATE_FREE:
            return (to == PROC_STATE_ALLOCATED);
        case PROC_STATE_ALLOCATED:
            return (to == PROC_STATE_INIT || to == PROC_STATE_FREE);  // Allow rollback
        case PROC_STATE_INIT:
            return (to == PROC_STATE_RUNNING || to == PROC_STATE_FREE);
        case PROC_STATE_RUNNING:
            return (to == PROC_STATE_SUSPENDED || to == PROC_STATE_STOPPING);
        case PROC_STATE_SUSPENDED:
            return (to == PROC_STATE_RUNNING || to == PROC_STATE_STOPPING);
        case PROC_STATE_STOPPING:
            return (to == PROC_STATE_ZOMBIE);
        case PROC_STATE_ZOMBIE:
            return (to == PROC_STATE_FREE);
        default:
            return false;
    }
}

/**
 * Atomic state transition (must hold g_ctx_lock)
 */
static bool transition_state(fmrb_app_task_context_t* ctx, fmrb_proc_state_t new_state) {
    if (!is_valid_transition(ctx->state, new_state)) {
        FMRB_LOGW(TAG, "[%s gen=%u] Invalid transition %s -> %s",
                 ctx->app_name, ctx->gen, state_str(ctx->state), state_str(new_state));
        return false;
    }

    FMRB_LOGI(TAG, "[%s gen=%u] State: %s -> %s",
             ctx->app_name, ctx->gen, state_str(ctx->state), state_str(new_state));
    ctx->state = new_state;
    return true;
}

/**
 * TLS destructor - called automatically when task is deleted
 */
static void tls_destructor(int idx, void* pv) {
    int32_t idx_i32 = (int32_t)idx;  // Convert FreeRTOS int to stdint
    (void)idx_i32;  // Currently unused
    fmrb_app_task_context_t* ctx = (fmrb_app_task_context_t*)pv;
    if (!ctx) return;

    FMRB_LOGI(TAG, "[%s gen=%u] TLS destructor called", ctx->app_name, ctx->gen);

    // Lock for state transition
    if (fmrb_semaphore_take(g_ctx_lock, FMRB_MS_TO_TICKS(1000)) != FMRB_TRUE) {
        FMRB_LOGE(TAG, "[%s] Failed to acquire lock in destructor", ctx->app_name);
        return;
    }

    // Close mruby VM if still open
    if (ctx->mrb) {
        FMRB_LOGI(TAG, "[%s] Closing mruby VM", ctx->app_name);
        mrb_close(ctx->mrb);
        ctx->mrb = NULL;
    }

    // Delete semaphore
    if (ctx->semaphore) {
        fmrb_semaphore_delete(ctx->semaphore);
        ctx->semaphore = NULL;
    }

    // Transition to ZOMBIE then FREE
    transition_state(ctx, PROC_STATE_ZOMBIE);
    transition_state(ctx, PROC_STATE_FREE);

    // Clear task handle
    ctx->task = NULL;

    fmrb_semaphore_give(g_ctx_lock);

    FMRB_LOGI(TAG, "[%s gen=%u] Resources cleaned up", ctx->app_name, ctx->gen);
}

/**
 * Allocate context slot (must hold g_ctx_lock)
 */
static int32_t alloc_ctx_index(fmrb_proc_id_t requested_id) {
    // For fixed IDs, use that slot directly
    if (requested_id >= 0 && requested_id < FMRB_MAX_APPS) {
        if (g_ctx_pool[requested_id].state == PROC_STATE_FREE) {
            g_ctx_pool[requested_id].gen++;  // Increment generation
            return requested_id;
        }
        FMRB_LOGW(TAG, "Requested slot %d already in use (state=%s)",
                 requested_id, state_str(g_ctx_pool[requested_id].state));
        return -1;
    }

    // Find first free slot
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        if (g_ctx_pool[i].state == PROC_STATE_FREE) {
            g_ctx_pool[i].gen++;
            return i;
        }
    }

    FMRB_LOGE(TAG, "No free context slots available");
    return -1;
}

/**
 * Free context slot (must hold g_ctx_lock)
 */
static void free_ctx_index(int32_t idx) {
    if (idx < 0 || idx >= FMRB_MAX_APPS) return;

    fmrb_app_task_context_t* ctx = &g_ctx_pool[idx];

    // Zero out context (keep gen counter)
    uint32_t gen = ctx->gen;
    memset(ctx, 0, sizeof(*ctx));
    ctx->gen = gen;
    ctx->app_id = idx;
    ctx->state = PROC_STATE_FREE;
}

// ============================================================================
// App task main loop
// ============================================================================

/**
 * Load Ruby script from file
 * Returns: allocated buffer containing script text (must be freed by caller)
 *          NULL on error
 */
static char* load_script_file(const char* filepath, size_t* out_size) {
    fmrb_file_t file = NULL;
    char* buffer = NULL;
    uint32_t file_size = 0;
    size_t bytes_read = 0;
    fmrb_err_t ret;

    // Open file
    ret = fmrb_hal_file_open(filepath, FMRB_O_RDONLY, &file);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to open script file: %s", filepath);
        return NULL;
    }

    // Get file size
    ret = fmrb_hal_file_size(file, &file_size);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to get file size: %s", filepath);
        fmrb_hal_file_close(file);
        return NULL;
    }

    // Check size limit
    if (file_size > MAX_SCRIPT_FILE_SIZE) {
        FMRB_LOGE(TAG, "Script file too large: %u bytes (max: %d)",
                 file_size, MAX_SCRIPT_FILE_SIZE);
        fmrb_hal_file_close(file);
        return NULL;
    }

    // Allocate buffer (+1 for null terminator)
    buffer = (char*)fmrb_sys_malloc(file_size + 1);
    if (!buffer) {
        FMRB_LOGE(TAG, "Failed to allocate buffer for script file");
        fmrb_hal_file_close(file);
        return NULL;
    }

    // Read file
    ret = fmrb_hal_file_read(file, buffer, file_size, &bytes_read);
    if (ret != FMRB_OK || bytes_read != file_size) {
        FMRB_LOGE(TAG, "Failed to read script file (expected %u, got %zu)",
                 file_size, bytes_read);
        fmrb_sys_free(buffer);
        fmrb_hal_file_close(file);
        return NULL;
    }

    // Null terminate
    buffer[file_size] = '\0';

    fmrb_hal_file_close(file);

    if (out_size) {
        *out_size = file_size;
    }

    return buffer;
}

/**
 * Application task entry point
 */
static void app_task_main(void* arg) {
    fmrb_app_task_context_t* ctx = (fmrb_app_task_context_t*)arg;
    const unsigned char* irep_ptr = NULL;
    char* script_buffer = NULL;
    bool need_free_script = false;

    // Register in TLS with destructor
    fmrb_task_set_tls_with_del(NULL, FMRB_APP_TLS_INDEX, ctx, tls_destructor);

    FMRB_LOGI(TAG, "[%s gen=%u] Task started (core=%d, prio=%u)",
             ctx->app_name, ctx->gen, fmrb_get_core_id(), fmrb_task_get_priority(NULL));

    // Transition to RUNNING
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    if (!transition_state(ctx, PROC_STATE_RUNNING)) {
        fmrb_semaphore_give(g_ctx_lock);
        FMRB_LOGE(TAG, "[%s] Failed to transition to RUNNING", ctx->app_name);
        fmrb_task_delete(NULL);
        return;
    }
    fmrb_semaphore_give(g_ctx_lock);

    // Determine load mode from user_data
    fmrb_load_mode_t load_mode = (fmrb_load_mode_t)((uintptr_t)ctx->user_data & 0xFF);
    void* load_data = (void*)((uintptr_t)ctx->user_data >> 8);

    mrc_irep *irep_obj = NULL;

    if (load_mode == FMRB_LOAD_MODE_IREP) {
        // Load from irep bytecode (existing behavior)
        irep_ptr = (const unsigned char*)load_data;
        irep_obj = mrb_read_irep(ctx->mrb, irep_ptr);
        if (irep_obj == NULL) {
            FMRB_LOGE(TAG, "[%s] Failed to read irep", ctx->app_name);
        }
    } else if (load_mode == FMRB_LOAD_MODE_FILE) {
        // Load from text file (new behavior)
        const char* filepath = (const char*)load_data;
        size_t script_size = 0;

        FMRB_LOGI(TAG, "[%s] Loading script from file: %s", ctx->app_name, filepath);

        script_buffer = load_script_file(filepath, &script_size);
        if (!script_buffer) {
            FMRB_LOGE(TAG, "[%s] Failed to load script file: %s", ctx->app_name, filepath);
            goto cleanup;
        }
        need_free_script = true;

        // Compile script to irep
        mrc_ccontext *cc = mrc_ccontext_new(ctx->mrb);
        if (!cc) {
            FMRB_LOGE(TAG, "[%s] Failed to create compile context", ctx->app_name);
            goto cleanup;
        }

        const uint8_t *script_ptr = (const uint8_t *)script_buffer;
        irep_obj = mrc_load_string_cxt(cc, &script_ptr, script_size);

        if (!irep_obj) {
            FMRB_LOGE(TAG, "[%s] Failed to compile script", ctx->app_name);
            if (ctx->mrb->exc) {
                mrb_print_error(ctx->mrb);
            }
            mrc_ccontext_free(cc);
            goto cleanup;
        }

        mrc_ccontext_free(cc);
        FMRB_LOGI(TAG, "[%s] Script compiled successfully", ctx->app_name);
    } else {
        FMRB_LOGE(TAG, "[%s] Invalid load mode: %d", ctx->app_name, load_mode);
        goto cleanup;
    }

    // Execute irep (common path for both modes)
    if (irep_obj) {
        mrc_ccontext *cc = mrc_ccontext_new(ctx->mrb);
        mrb_value name = mrb_str_new_cstr(ctx->mrb, ctx->app_name);
        mrb_value task = mrc_create_task(cc, irep_obj, name, mrb_nil_value(),
                                         mrb_obj_value(ctx->mrb->top_self));

        if (mrb_nil_p(task)) {
            FMRB_LOGE(TAG, "[%s] mrc_create_task failed", ctx->app_name);
        } else {
            // Main event loop: run mruby tasks
            mrb_tasks_run(ctx->mrb);
        }

        if (ctx->mrb->exc) {
            mrb_print_error(ctx->mrb);
        }

        mrc_ccontext_free(cc);
    }

cleanup:
    // Free script buffer if allocated
    if (need_free_script && script_buffer) {
        fmrb_sys_free(script_buffer);
    }

    FMRB_LOGI(TAG, "[%s gen=%u] Task exiting normally", ctx->app_name, ctx->gen);

    // Transition to STOPPING
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    transition_state(ctx, PROC_STATE_STOPPING);
    fmrb_semaphore_give(g_ctx_lock);

    // TLS destructor will handle cleanup
    fmrb_task_delete(NULL);
}

#ifdef CONFIG_IDF_TARGET_LINUX
extern void dump_signal_mask(const char*);
extern void log_itimer_real(const char*);
#endif

static void app_task_test(void* arg) {
    FMRB_LOGI("SIG", "[app_task_test] enter");
#ifdef CONFIG_IDF_TARGET_LINUX
    dump_signal_mask("app_task_test");
    log_itimer_real("app_task_test");
#endif
    while (1) {
        FMRB_LOGI("SIG", "testapp  tick=%u", (unsigned)fmrb_task_get_tick_count());
        fmrb_task_delay(FMRB_MS_TO_TICKS(1000));
    }
}



// ============================================================================
// Public APIs
// ============================================================================

/**
 * Initialize app context management (call once at boot)
 */
bool fmrb_app_init(void) {
    if (g_ctx_lock != NULL) {
        FMRB_LOGW(TAG, "App context already initialized");
        return false;
    }

    // Create mutex
    g_ctx_lock = fmrb_semaphore_create_mutex();
    if (!g_ctx_lock) {
        FMRB_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    // Initialize context pool
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        memset(&g_ctx_pool[i], 0, sizeof(fmrb_app_task_context_t));
        g_ctx_pool[i].app_id = i;
        g_ctx_pool[i].state = PROC_STATE_FREE;
        g_ctx_pool[i].gen = 0;
    }

    FMRB_LOGI(TAG, "App context management initialized (max_apps=%d)", FMRB_MAX_APPS);
    return true;
}


/**
 * Spawn simple debug task (no context management, no mruby VM)
 */
static fmrb_task_handle_t g_task_debug = NULL;
fmrb_err_t fmrb_app_spawn_simple(const fmrb_spawn_attr_t* attr, int32_t* out_id) {
    if (!attr || !attr->name) {
        FMRB_LOGE(TAG, "Invalid spawn attributes");
        return FMRB_ERR_INVALID_PARAM;
    }

    if (!out_id) {
        FMRB_LOGE(TAG, "out_id is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_base_type_t result = fmrb_task_create(
        app_task_test, attr->name, attr->stack_words,
        NULL, attr->priority, &g_task_debug);


    if (result == FMRB_PASS) {
        *out_id = -1;  // No context ID for simple spawn
        FMRB_LOGI(TAG, "[%s] Debug task spawned (prio=%u)", attr->name, attr->priority);
        return FMRB_OK;
    } else {
        FMRB_LOGE(TAG, "[%s] Failed to create debug task", attr->name);
        return FMRB_ERR_FAILED;
    }
}

/**
 * Spawn new app task
 */
fmrb_err_t fmrb_app_spawn(const fmrb_spawn_attr_t* attr, int32_t* out_id) {
    if (!attr || !attr->name) {
        FMRB_LOGE(TAG, "Invalid spawn attributes");
        return FMRB_ERR_INVALID_PARAM;
    }

    FMRB_LOGI(TAG, "fmrb_app_spawn: name=%s, mode=%d, type=%d", attr->name, attr->load_mode, attr->type);

    // Validate load mode and source
    if (attr->load_mode == FMRB_LOAD_MODE_IREP) {
        if (!attr->irep) {
            FMRB_LOGE(TAG, "irep is NULL for IREP mode");
            return FMRB_ERR_INVALID_PARAM;
        }
    } else if (attr->load_mode == FMRB_LOAD_MODE_FILE) {
        if (!attr->filepath) {
            FMRB_LOGE(TAG, "filepath is NULL for FILE mode");
            return FMRB_ERR_INVALID_PARAM;
        }
    } else {
        FMRB_LOGE(TAG, "Invalid load_mode: %d", attr->load_mode);
        return FMRB_ERR_INVALID_PARAM;
    }

    if (!out_id) {
        FMRB_LOGE(TAG, "out_id is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    int32_t idx = -1;
    fmrb_app_task_context_t* ctx = NULL;

    // Allocate context slot
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    idx = alloc_ctx_index(attr->app_id);
    if (idx < 0) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_NO_RESOURCE;
    }

    ctx = &g_ctx_pool[idx];
    transition_state(ctx, PROC_STATE_ALLOCATED);
    fmrb_semaphore_give(g_ctx_lock);

    // Initialize context fields
    ctx->app_id = idx;
    ctx->type = attr->type;

    // Assign memory pool based on task type to avoid conflicts
    switch (attr->type) {
        case APP_TYPE_KERNEL:
            ctx->mempool_id = POOL_ID_KERNEL;
            break;
        case APP_TYPE_SYSTEM_APP:
            ctx->mempool_id = POOL_ID_SYSTEM_APP;
            break;
        case APP_TYPE_USER_APP:
            // User apps: map PROC_ID_USER_APP0..2 to POOL_ID_USER_APP0..2
            if (idx >= PROC_ID_USER_APP0 && idx < PROC_ID_MAX) {
                ctx->mempool_id = POOL_ID_USER_APP0 + (idx - PROC_ID_USER_APP0);
            } else {
                FMRB_LOGE(TAG, "Invalid USER_APP proc_id: %d", idx);
                goto unwind;
            }
            break;
        default:
            FMRB_LOGE(TAG, "Unknown app type: %d", attr->type);
            goto unwind;
    }

    strncpy(ctx->app_name, attr->name, sizeof(ctx->app_name) - 1);
    ctx->app_name[sizeof(ctx->app_name) - 1] = '\0';

    // Encode load mode and data pointer in user_data
    // Lower 8 bits: load_mode, upper bits: data pointer
    uintptr_t encoded_data;
    if (attr->load_mode == FMRB_LOAD_MODE_IREP) {
        encoded_data = ((uintptr_t)attr->irep << 8) | FMRB_LOAD_MODE_IREP;
    } else {
        encoded_data = ((uintptr_t)attr->filepath << 8) | FMRB_LOAD_MODE_FILE;
    }
    ctx->user_data = (void*)encoded_data;
    ctx->headless = attr->headless;
    ctx->window_pos_x = attr->window_pos_x;
    ctx->window_pos_y = attr->window_pos_y;

    // Initialize window size based on app ztype
    const fmrb_system_config_t* sys_config = fmrb_kernel_get_config();
    if (attr->type == APP_TYPE_USER_APP && !ctx->headless) {
        ctx->window_width = sys_config->default_user_app_width;
        ctx->window_height = sys_config->default_user_app_height;
    } else if (attr->type == APP_TYPE_SYSTEM_APP) {
        ctx->window_width = sys_config->display_width;
        ctx->window_height = sys_config->display_height;
    } else {
        ctx->window_width = 0;   // Headless
        ctx->window_height = 0;
    }

    // Create mruby VM
    // mrbgem initialization is executed here.
    ctx->mrb = mrb_open_with_custom_alloc(
        fmrb_get_mempool_ptr(ctx->mempool_id),
        fmrb_get_mempool_size(ctx->mempool_id));
    if (!ctx->mrb) {
        FMRB_LOGE(TAG, "[%s] Failed to open mruby VM", ctx->app_name);
        goto unwind;
    }

    // Create semaphore
    ctx->semaphore = fmrb_semaphore_create_binary();
    if (!ctx->semaphore) {
        FMRB_LOGE(TAG, "[%s] Failed to create semaphore", ctx->app_name);
        goto unwind;
    }

    // Transition to INIT
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    if (!transition_state(ctx, PROC_STATE_INIT)) {
        fmrb_semaphore_give(g_ctx_lock);
        goto unwind;
    }
    fmrb_semaphore_give(g_ctx_lock);

    // Create FreeRTOS task
    fmrb_base_type_t result;
    if (attr->core_affinity >= 0) {
        FMRB_LOGI(TAG, "fmrb_task_create_pinned [%s]",ctx->app_name);
        result = fmrb_task_create_pinned(
            app_task_main, ctx->app_name, attr->stack_words,
            ctx, attr->priority, &ctx->task, attr->core_affinity);
    } else {
        FMRB_LOGI(TAG, "fmrb_task_create [%s]",ctx->app_name);
        result = fmrb_task_create(
            app_task_main, ctx->app_name, attr->stack_words,
            ctx, attr->priority, &ctx->task);
    }

    if (result != FMRB_PASS) {
        FMRB_LOGE(TAG, "[%s] Failed to create task", ctx->app_name);
        goto unwind;
    }

    // Success - Note: Spawned task may have already started running
    // if its priority is higher than the current task
    *out_id = idx;
    FMRB_LOGI(TAG, "[%s gen=%u] Task spawned (id=%d, prio=%u)",
             ctx->app_name, ctx->gen, idx, attr->priority);
    return FMRB_OK;

unwind:
    // Cleanup on failure
    FMRB_LOGW(TAG, "[%s gen=%u] Spawn failed, unwinding", ctx->app_name, ctx->gen);

    if (ctx->semaphore) {
        fmrb_semaphore_delete(ctx->semaphore);
        ctx->semaphore = NULL;
    }
    if (ctx->mrb) {
        mrb_close(ctx->mrb);
        ctx->mrb = NULL;
    }

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    free_ctx_index(idx);
    fmrb_semaphore_give(g_ctx_lock);

    return FMRB_ERR_FAILED;
}

/**
 * Kill app (forceful termination)
 */
bool fmrb_app_kill(int32_t id) {
    if (id < 0 || id >= FMRB_MAX_APPS) return false;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    fmrb_app_task_context_t* ctx = &g_ctx_pool[id];

    if (ctx->state != PROC_STATE_RUNNING && ctx->state != PROC_STATE_SUSPENDED) {
        FMRB_LOGW(TAG, "[%s] Cannot kill app in state %s", ctx->app_name, state_str(ctx->state));
        fmrb_semaphore_give(g_ctx_lock);
        return false;
    }

    transition_state(ctx, PROC_STATE_STOPPING);
    fmrb_task_handle_t task = ctx->task;
    fmrb_semaphore_give(g_ctx_lock);

    // Notify task to terminate
    if (task) {
        fmrb_task_notify_give(task);  // Wake up task if waiting
        fmrb_task_delete(task);       // Force delete
    }

    FMRB_LOGI(TAG, "[%s gen=%u] Killed", ctx->app_name, ctx->gen);
    return true;
}

/**
 * Stop app (graceful shutdown)
 */
bool fmrb_app_stop(int32_t id) {
    // For now, same as kill (TODO: implement graceful shutdown signal)
    return fmrb_app_kill(id);
}

/**
 * Suspend app
 */
bool fmrb_app_suspend(int32_t id) {
    if (id < 0 || id >= FMRB_MAX_APPS) return false;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    fmrb_app_task_context_t* ctx = &g_ctx_pool[id];

    if (ctx->state != PROC_STATE_RUNNING) {
        fmrb_semaphore_give(g_ctx_lock);
        return false;
    }

    transition_state(ctx, PROC_STATE_SUSPENDED);
    fmrb_task_handle_t task = ctx->task;
    fmrb_semaphore_give(g_ctx_lock);

    if (task) {
        fmrb_task_suspend(task);
        FMRB_LOGI(TAG, "[%s gen=%u] Suspended", ctx->app_name, ctx->gen);
        return true;
    }

    return false;
}

/**
 * Resume app
 */
bool fmrb_app_resume(int32_t id) {
    if (id < 0 || id >= FMRB_MAX_APPS) return false;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    fmrb_app_task_context_t* ctx = &g_ctx_pool[id];

    if (ctx->state != PROC_STATE_SUSPENDED) {
        fmrb_semaphore_give(g_ctx_lock);
        return false;
    }

    transition_state(ctx, PROC_STATE_RUNNING);
    fmrb_task_handle_t task = ctx->task;
    fmrb_semaphore_give(g_ctx_lock);

    if (task) {
        fmrb_task_resume(task);
        FMRB_LOGI(TAG, "[%s gen=%u] Resumed", ctx->app_name, ctx->gen);
        return true;
    }

    return false;
}

/**
 * Get app list (ps-style)
 */
int32_t fmrb_app_ps(fmrb_app_info_t* list, int32_t max_count) {
    if (!list || max_count <= 0) return 0;

    int32_t count = 0;
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);

    for (int32_t i = 0; i < FMRB_MAX_APPS && count < max_count; i++) {
        fmrb_app_task_context_t* ctx = &g_ctx_pool[i];
        if (ctx->state == PROC_STATE_FREE) continue;

        list[count].app_id = ctx->app_id;
        list[count].state = ctx->state;
        list[count].type = ctx->type;
        strncpy(list[count].app_name, ctx->app_name, sizeof(list[count].app_name) - 1);
        list[count].gen = ctx->gen;
        list[count].task = ctx->task;
        list[count].stack_high_water = ctx->task ? fmrb_task_get_stack_high_water_mark(ctx->task) : 0;
        count++;
    }

    fmrb_semaphore_give(g_ctx_lock);
    return count;
}

/**
 * Get context by ID
 */
fmrb_app_task_context_t* fmrb_app_get_context_by_id(int32_t id) {
    if (id < 0 || id >= FMRB_MAX_APPS) return NULL;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    fmrb_app_task_context_t* ctx = &g_ctx_pool[id];
    if (ctx->state == PROC_STATE_FREE) ctx = NULL;
    fmrb_semaphore_give(g_ctx_lock);

    return ctx;
}

