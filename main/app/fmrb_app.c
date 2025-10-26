#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"

static const char *TAG = "fmrb_app";

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
static inline const char* state_str(enum FMRB_PROC_STATE state) {
    return (state >= 0 && state < sizeof(state_names)/sizeof(state_names[0]))
           ? state_names[state] : "UNKNOWN";
}

/**
 * Validate state transition
 */
static bool is_valid_transition(enum FMRB_PROC_STATE from, enum FMRB_PROC_STATE to) {
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
static bool transition_state(fmrb_app_task_context_t* ctx, enum FMRB_PROC_STATE new_state) {
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

    // Delete event queue
    if (ctx->event_queue) {
        fmrb_queue_delete(ctx->event_queue);
        ctx->event_queue = NULL;
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
static int32_t alloc_ctx_index(enum FMRB_PROC_ID requested_id) {
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
 * Application task entry point
 */
static void app_task_main(void* arg) {
    fmrb_app_task_context_t* ctx = (fmrb_app_task_context_t*)arg;
    const unsigned char* irep = (const unsigned char*)ctx->user_data;

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

    // Load and execute bytecode
    mrc_irep *irep_obj = mrb_read_irep(ctx->mrb, irep);
    if (irep_obj == NULL) {
        FMRB_LOGE(TAG, "[%s] Failed to read irep", ctx->app_name);
    } else {
        mrc_ccontext *cc = mrc_ccontext_new(ctx->mrb);
        mrb_value name = mrb_str_new_cstr(ctx->mrb, ctx->app_name);
        mrb_value task = mrc_create_task(cc, irep_obj, name, mrb_nil_value(),
                                         mrb_obj_value(ctx->mrb->top_self));

        if (mrb_nil_p(task)) {
            FMRB_LOGE(TAG, "[%s] mrc_create_task failed", ctx->app_name);
        } else {
            // Main event loop: run mruby tasks
            #if 0
            mrb_tasks_run(ctx->mrb);
            #else
            FMRB_LOGI(TAG, "[%s] Skip mruby VM run sleep", ctx->app_name);
            while(1){
                FMRB_LOGI(TAG, "[%s] app thread running", ctx->app_name);
                fmrb_task_delay(FMRB_MS_TO_TICKS(1000));
            }
            FMRB_LOGI(TAG, "[%s] Skip mruby VM run sleep done", ctx->app_name);
            #endif
        }

        if (ctx->mrb->exc) {
            mrb_print_error(ctx->mrb);
        }

        mrc_ccontext_free(cc);
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
void fmrb_app_init(void) {
    if (g_ctx_lock != NULL) {
        FMRB_LOGW(TAG, "App context already initialized");
        return;
    }

    // Create mutex
    g_ctx_lock = fmrb_semaphore_create_mutex();
    if (!g_ctx_lock) {
        FMRB_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Initialize context pool
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        memset(&g_ctx_pool[i], 0, sizeof(fmrb_app_task_context_t));
        g_ctx_pool[i].app_id = i;
        g_ctx_pool[i].state = PROC_STATE_FREE;
        g_ctx_pool[i].gen = 0;
    }

    FMRB_LOGI(TAG, "App context management initialized (max_apps=%d)", FMRB_MAX_APPS);
}

/**
 * Spawn simple debug task (no context management, no mruby VM)
 */
static fmrb_task_handle_t g_task_debug = NULL;
bool fmrb_app_spawn_simple(const fmrb_spawn_attr_t* attr, int32_t* out_id) {
    if (!attr || !attr->name) {
        FMRB_LOGE(TAG, "Invalid spawn attributes");
        return false;
    }

    fmrb_base_type_t result = fmrb_task_create(
        app_task_test, attr->name, attr->stack_words,
        NULL, attr->priority, &g_task_debug);


    if (result == FMRB_PASS) {
        if (out_id) *out_id = -1;  // No context ID for simple spawn
        FMRB_LOGI(TAG, "[%s] Debug task spawned (prio=%u)", attr->name, attr->priority);
        return true;
    } else {
        FMRB_LOGE(TAG, "[%s] Failed to create debug task", attr->name);
        return false;
    }
}

/**
 * Spawn new app task
 */
bool fmrb_app_spawn(const fmrb_spawn_attr_t* attr, int32_t* out_id) {
    if (!attr || !attr->name || !attr->irep) {
        FMRB_LOGE(TAG, "Invalid spawn attributes");
        return false;
    }

    int32_t idx = -1;
    fmrb_app_task_context_t* ctx = NULL;

    // Allocate context slot
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    idx = alloc_ctx_index(attr->app_id);
    if (idx < 0) {
        fmrb_semaphore_give(g_ctx_lock);
        return false;
    }

    ctx = &g_ctx_pool[idx];
    transition_state(ctx, PROC_STATE_ALLOCATED);
    fmrb_semaphore_give(g_ctx_lock);

    // Initialize context fields
    ctx->app_id = idx;
    ctx->type = attr->type;
    ctx->mempool_id = idx;  // Map PROC_ID to POOL_ID
    strncpy(ctx->app_name, attr->name, sizeof(ctx->app_name) - 1);
    ctx->app_name[sizeof(ctx->app_name) - 1] = '\0';
    ctx->user_data = (void*)attr->irep;  // Store irep for task_main

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

    // Create event queue if requested
    if (attr->event_queue_len > 0) {
        ctx->event_queue = fmrb_queue_create(attr->event_queue_len, sizeof(void*));
        if (!ctx->event_queue) {
            FMRB_LOGW(TAG, "[%s] Failed to create event queue", ctx->app_name);
            // Non-fatal, continue
        }
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
    if (out_id) *out_id = idx;
    FMRB_LOGI(TAG, "[%s gen=%u] Task spawned (id=%d, prio=%u)",
             ctx->app_name, ctx->gen, idx, attr->priority);
    return true;

unwind:
    // Cleanup on failure
    FMRB_LOGW(TAG, "[%s gen=%u] Spawn failed, unwinding", ctx->app_name, ctx->gen);

    if (ctx->event_queue) {
        fmrb_queue_delete(ctx->event_queue);
        ctx->event_queue = NULL;
    }
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

    return false;
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

/**
 * Post event to specific app
 */
bool fmrb_post_event(int32_t id, const void* event, size_t size, fmrb_tick_t timeout) {
    fmrb_app_task_context_t* ctx = fmrb_app_get_context_by_id(id);
    if (!ctx || !ctx->event_queue) return false;

    return (fmrb_queue_send(ctx->event_queue, event, timeout) == FMRB_TRUE);
}

/**
 * Broadcast event to all apps
 */
bool fmrb_broadcast(const void* event, size_t size, fmrb_tick_t timeout) {
    bool any_sent = false;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        fmrb_app_task_context_t* ctx = &g_ctx_pool[i];
        if (ctx->state != PROC_STATE_FREE && ctx->event_queue) {
            if (fmrb_queue_send(ctx->event_queue, event, timeout) == FMRB_TRUE) {
                any_sent = true;
            }
        }
    }
    fmrb_semaphore_give(g_ctx_lock);

    return any_sent;
}

