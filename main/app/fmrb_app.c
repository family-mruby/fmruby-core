#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_kernel.h"
#include "fmrb_lua.h"
#include "fmrb_link_transport.h"
#include "fmrb_link_protocol.h"

// Forward declaration for estalloc helper function
extern int mrb_get_estalloc_stats(void* est_ptr, size_t* total, size_t* used, size_t* free, int32_t* frag);

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
 * @brief Inspect mrc_irep structure for debugging
 */
static void inspect_irep(mrb_state *mrb, const char* app_name, const mrc_irep *irep,
                         const void* script_buf_start, const void* script_buf_end)
{
    if (!irep) {
        FMRB_LOGE(TAG, "[%s] IREP is NULL!", app_name);
        return;
    }

    FMRB_LOGI(TAG, "[%s] === IREP Inspection ===", app_name);
    FMRB_LOGI(TAG, "[%s] irep=%p", app_name, irep);
    FMRB_LOGI(TAG, "[%s] nlocals=%u, nregs=%u, clen=%u, flags=0x%02x",
              app_name, irep->nlocals, irep->nregs,
              irep->clen, irep->flags);
    FMRB_LOGI(TAG, "[%s] iseq=%p", app_name, irep->iseq);
    FMRB_LOGI(TAG, "[%s] pool=%p (plen=%u)", app_name, irep->pool, irep->plen);
    FMRB_LOGI(TAG, "[%s] syms=%p (slen=%u)", app_name, irep->syms, irep->slen);
    FMRB_LOGI(TAG, "[%s] reps=%p (rlen=%u)", app_name, irep->reps, irep->rlen);

    // Check symbol table validity - show first 5 symbols
    if (irep->syms && irep->slen > 0) {
        FMRB_LOGI(TAG, "[%s] Symbol table (slen=%u, first 5):", app_name, irep->slen);
        for (int i = 0; i < 5 && i < irep->slen; i++) {
            mrb_sym sym = irep->syms[i];
            const char* name = mrb_sym_name(mrb, sym);
            FMRB_LOGI(TAG, "[%s]   syms[%d] = %u ('%s')",
                      app_name, i, sym, name ? name : "NULL");
        }
    } else {
        FMRB_LOGW(TAG, "[%s] Symbol table is NULL or empty (slen=%u)", app_name, irep->slen);
    }

    // Check pool content
    if (irep->pool && irep->plen > 0) {
        FMRB_LOGI(TAG, "[%s] Pool (plen=%u, first 3):", app_name, irep->plen);
        for (int i = 0; i < 3 && i < irep->plen; i++) {
            const mrc_pool_value *pv = &irep->pool[i];
            uint32_t tt = pv->tt;
            uint32_t type = tt & 0x7;  // Lower 3 bits = type
            FMRB_LOGI(TAG, "[%s]   pool[%d] type=%u (tt=0x%08x)", app_name, i, type, tt);
            if (type == 0 || type == 2) {  // IREP_TT_STR or IREP_TT_SSTR
                const char* str = pv->u.str;
                bool in_script_buf = false;
                if (script_buf_start && script_buf_end && str) {
                    in_script_buf = (str >= (const char*)script_buf_start &&
                                    str < (const char*)script_buf_end);
                }
                FMRB_LOGI(TAG, "[%s]     -> string_ptr=%p, in_script_buf=%s, content=\"%s\"",
                          app_name, (void*)str, in_script_buf ? "YES" : "NO", str ? str : "NULL");
            }
        }
    } else {
        FMRB_LOGI(TAG, "[%s] Pool is NULL or empty (plen=%u)", app_name, irep->plen);
    }

    // Show first 10 instruction bytes
    if (irep->iseq) {
        FMRB_LOGI(TAG, "[%s] First 10 iseq bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                  app_name,
                  irep->iseq[0], irep->iseq[1], irep->iseq[2], irep->iseq[3], irep->iseq[4],
                  irep->iseq[5], irep->iseq[6], irep->iseq[7], irep->iseq[8], irep->iseq[9]);
    } else {
        FMRB_LOGW(TAG, "[%s] iseq is NULL", app_name);
    }

    FMRB_LOGI(TAG, "[%s] === End IREP Inspection ===", app_name);
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

    // Close VM based on type
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:
            if (ctx->mrb) {
                FMRB_LOGI(TAG, "[%s] Closing mruby VM", ctx->app_name);
                mrb_close(ctx->mrb);
                ctx->mrb = NULL;
            }
            break;
        case FMRB_VM_TYPE_LUA:
            if (ctx->lua) {
                FMRB_LOGI(TAG, "[%s] Closing Lua VM", ctx->app_name);
                fmrb_lua_close(ctx->lua);
                ctx->lua = NULL;
            }
            break;
        case FMRB_VM_TYPE_NATIVE:
            // No VM to close for native functions
            break;
        default:
            FMRB_LOGW(TAG, "[%s] Unknown VM type: %d", ctx->app_name, ctx->vm_type);
            break;
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
    ctx->task = 0;

    fmrb_semaphore_give(g_ctx_lock);

    FMRB_LOGI(TAG, "[%s gen=%u] Resources cleaned up", ctx->app_name, ctx->gen);
}

/**
 * Allocate context slot (must hold g_ctx_lock)
 */
static int32_t alloc_ctx_index(fmrb_proc_id_t requested_id, enum FMRB_APP_TYPE app_type) {
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

    // For USER_APP, search only in USER_APP slot range
    int32_t start_idx = 0;
    int32_t end_idx = FMRB_MAX_APPS;

    if (app_type == APP_TYPE_USER_APP) {
        start_idx = PROC_ID_USER_APP0;
        end_idx = PROC_ID_MAX;
    }

    // Find first free slot in the appropriate range
    for (int32_t i = start_idx; i < end_idx; i++) {
        if (g_ctx_pool[i].state == PROC_STATE_FREE) {
            g_ctx_pool[i].gen++;
            return i;
        }
    }

    FMRB_LOGE(TAG, "No free context slots available for app_type=%d", app_type);
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
    fmrb_task_set_tls_with_del(0, FMRB_APP_TLS_INDEX, ctx, tls_destructor);

    FMRB_LOGI(TAG, "[%s gen=%u] Task started (core=%d, prio=%u)",
             ctx->app_name, ctx->gen, fmrb_get_core_id(), fmrb_task_get_priority(0));

    // Create VM based on vm_type
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:
            // Create mruby VM (mrbgem initialization is executed here)
            {
                void* pool_ptr = fmrb_get_mempool_ptr(ctx->mempool_id);
                size_t pool_size = fmrb_get_mempool_size(ctx->mempool_id);
                FMRB_LOGI(TAG, "[%s] mempool_id=%d, ptr=%p, size=%zu",
                          ctx->app_name, ctx->mempool_id, pool_ptr, pool_size);
                fmrb_mempool_check_pointer(pool_ptr);

                ctx->mrb = mrb_open_with_custom_alloc(pool_ptr, pool_size);
                FMRB_LOGI(TAG, "[%s] mrb_open_with_custom_alloc returned: %p", ctx->app_name, ctx->mrb);

                if (!ctx->mrb) {
                    FMRB_LOGE(TAG, "[%s] Failed to open mruby VM", ctx->app_name);
                    goto cleanup;
                }

                FMRB_LOGI(TAG, "[%s] mruby VM created successfully, checking $stdout", ctx->app_name);
            }
            break;
        case FMRB_VM_TYPE_LUA:
            // Create Lua VM with task-specific memory pool
            ctx->lua = fmrb_lua_newstate(ctx);
            if (!ctx->lua) {
                FMRB_LOGE(TAG, "[%s] Failed to open Lua VM", ctx->app_name);
                goto cleanup;
            }
            fmrb_lua_openlibs(ctx->lua);

            FMRB_LOGI(TAG, "[%s] Lua VM created with mempool=%d",
                      ctx->app_name, ctx->mempool_id);
            break;
        case FMRB_VM_TYPE_NATIVE:
            // No VM needed for native functions
            FMRB_LOGI(TAG, "[%s] Native function mode", ctx->app_name);
            break;
        default:
            FMRB_LOGE(TAG, "[%s] Unknown VM type: %d", ctx->app_name, ctx->vm_type);
            goto cleanup;
    }

    // Transition to RUNNING
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    if (!transition_state(ctx, PROC_STATE_RUNNING)) {
        fmrb_semaphore_give(g_ctx_lock);
        FMRB_LOGE(TAG, "[%s] Failed to transition to RUNNING", ctx->app_name);
        fmrb_task_delete(0);
        return;
    }
    fmrb_semaphore_give(g_ctx_lock);

    // Get load mode and data from context
    fmrb_load_mode_t load_mode = ctx->load_mode;
    void* load_data = ctx->load_data;

    // Execute based on VM type
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY: {
            mrc_irep *irep_obj = NULL;
            mrc_ccontext *cc = mrc_ccontext_new(ctx->mrb);
            if (!cc) {
                FMRB_LOGE(TAG, "[%s] Failed to create compile context", ctx->app_name);
                goto cleanup;
            }

            if (load_mode == FMRB_LOAD_MODE_BYTECODE) {
                // Load from bytecode (IREP)
                irep_ptr = (const unsigned char*)load_data;
                irep_obj = mrb_read_irep(ctx->mrb, irep_ptr);
                if (irep_obj == NULL) {
                    FMRB_LOGE(TAG, "[%s] Failed to read IREP bytecode", ctx->app_name);
                    goto cleanup;
                }

                // Inspect IREP structure (no script buffer for bytecode)
                inspect_irep(ctx->mrb, ctx->app_name, irep_obj, NULL, NULL);
            } else if (load_mode == FMRB_LOAD_MODE_FILE) {
                // Load from Ruby source file
                const char* filepath = (const char*)load_data;
                size_t script_size = 0;

                FMRB_LOGI(TAG, "[%s] Loading Ruby script from file: %s", ctx->app_name, filepath);

                script_buffer = load_script_file(filepath, &script_size);
                if (!script_buffer) {
                    FMRB_LOGE(TAG, "[%s] Failed to load script file: %s", ctx->app_name, filepath);
                    goto cleanup;
                }
                need_free_script = true;

                const uint8_t *script_ptr = (const uint8_t *)script_buffer;
                FMRB_LOGI(TAG, "[%s] Script size: %zu bytes", ctx->app_name, script_size);
                FMRB_LOGI(TAG, "[%s] script_buffer range: %p - %p",
                          ctx->app_name, (void*)script_buffer, (void*)((uint8_t*)script_buffer + script_size));

                irep_obj = mrc_load_string_cxt(cc, &script_ptr, script_size);

                FMRB_LOGI(TAG, "[%s] After mrc_load_string_cxt, irep_obj=%p, mrb->exc=%p",
                          ctx->app_name, irep_obj, ctx->mrb->exc);

                if (!irep_obj) {
                    FMRB_LOGE(TAG, "[%s] Failed to compile Ruby script", ctx->app_name);
                    if (ctx->mrb->exc) {
                        mrb_print_error(ctx->mrb);
                    }
                    mrc_ccontext_free(cc);
                    goto cleanup;
                }

                FMRB_LOGI(TAG, "[%s] Ruby script compiled successfully", ctx->app_name);

                // Inspect IREP structure (with script buffer range for analysis)
                inspect_irep(ctx->mrb, ctx->app_name, irep_obj,
                            script_buffer, (uint8_t*)script_buffer + script_size);

                // script_buffer cannot be free here.
                // TODO: investigate how the buffer is used.
            } else {
                FMRB_LOGE(TAG, "[%s] Invalid load mode: %d", ctx->app_name, load_mode);
                goto cleanup;
            }

            // Execute irep
            FMRB_LOGI(TAG, "[%s] Execute irep", ctx->app_name);

            mrb_value name = mrb_str_new_cstr(ctx->mrb, ctx->app_name);
            mrb_value task = mrc_create_task(cc, irep_obj, name, mrb_nil_value(), mrb_obj_value(ctx->mrb->top_self));
            if (mrb_nil_p(task)) {
                FMRB_LOGE(TAG, "[%s] mrc_create_task failed, mrb->exc=%p", ctx->app_name, ctx->mrb->exc);
                goto cleanup;
            }

            FMRB_LOGI(TAG, "[%s] mrb_tasks_run - BEFORE execution", ctx->app_name);
            mrb_value v = mrb_tasks_run(ctx->mrb);
            FMRB_LOGI(TAG, "[%s] mrb_tasks_run - AFTER execution, mrb->exc=%p", ctx->app_name, ctx->mrb->exc);

            //TODO: check proper free process
            if (ctx->mrb->exc) {
                FMRB_LOGI(TAG, "[%s] Exception detected, calling mrb_print_error", ctx->app_name);
                mrb_print_error(ctx->mrb);
                FMRB_LOGI(TAG, "[%s] mrb_print_error completed", ctx->app_name);
            } else {
                FMRB_LOGI(TAG, "[%s] No exception detected", ctx->app_name);
            }

            mrb_vm_ci_env_clear(ctx->mrb, ctx->mrb->c->cibase);
            // if (ctx->mrb->exc) {
            //     MRB_EXC_CHECK_EXIT(ctx->mrb, ctx->mrb->exc);
            //     mrb_undef_p(v);
            // }
            mrc_irep_free(cc, irep_obj);
            mrc_ccontext_free(cc);

            fmrb_sys_free(script_buffer);
            script_buffer = NULL;
            need_free_script = false;

            break;
        }

        case FMRB_VM_TYPE_LUA: {
            // Lua execution
            if (load_mode == FMRB_LOAD_MODE_BYTECODE) {
                // Load from Lua bytecode chunk
                FMRB_LOGW(TAG, "[%s] Lua bytecode loading not yet implemented", ctx->app_name);
            } else if (load_mode == FMRB_LOAD_MODE_FILE) {
                // Load from Lua source file
                const char* filepath = (const char*)load_data;
                size_t script_size = 0;

                FMRB_LOGI(TAG, "[%s] Loading Lua script from file: %s", ctx->app_name, filepath);

                script_buffer = load_script_file(filepath, &script_size);
                if (!script_buffer) {
                    FMRB_LOGE(TAG, "[%s] Failed to load script file: %s", ctx->app_name, filepath);
                    goto cleanup;
                }
                need_free_script = true;

                // Load and compile Lua script
                int load_result = luaL_loadbuffer(ctx->lua, script_buffer, script_size, filepath);
                if (load_result != LUA_OK) {
                    const char* err_msg = lua_tostring(ctx->lua, -1);
                    FMRB_LOGE(TAG, "[%s] Failed to compile Lua script: %s",
                              ctx->app_name, err_msg ? err_msg : "unknown error");
                    lua_pop(ctx->lua, 1);  // Pop error message
                    goto cleanup;
                }

                FMRB_LOGI(TAG, "[%s] Lua script compiled successfully", ctx->app_name);

                // Free script buffer immediately after compilation (no longer needed)
                fmrb_sys_free(script_buffer);
                script_buffer = NULL;
                need_free_script = false;

                // Execute Lua script
                int exec_result = lua_pcall(ctx->lua, 0, LUA_MULTRET, 0);
                if (exec_result != LUA_OK) {
                    const char* err_msg = lua_tostring(ctx->lua, -1);
                    FMRB_LOGE(TAG, "[%s] Lua execution error: %s",
                              ctx->app_name, err_msg ? err_msg : "unknown error");
                    lua_pop(ctx->lua, 1);  // Pop error message
                } else {
                    FMRB_LOGI(TAG, "[%s] Lua script executed successfully", ctx->app_name);
                }
            }
            break;
        }

        case FMRB_VM_TYPE_NATIVE: {
            // Native C function execution
            void (*native_func)(void*) = (void (*)(void*))load_data;
            if (native_func) {
                FMRB_LOGI(TAG, "[%s] Executing native function", ctx->app_name);
                native_func(ctx);
            } else {
                FMRB_LOGE(TAG, "[%s] Native function pointer is NULL", ctx->app_name);
            }
            break;
        }

        default:
            FMRB_LOGE(TAG, "[%s] Unknown VM type: %d", ctx->app_name, ctx->vm_type);
            break;
    }

cleanup:
    // Free script buffer if allocated
    if (need_free_script && script_buffer) {
        fmrb_sys_free(script_buffer);
    }
    if (ctx->mem_handle >=0 ) {
        fmrb_mem_destroy_handle(ctx->mem_handle);
    }

    FMRB_LOGI(TAG, "[%s gen=%u] Task exiting normally", ctx->app_name, ctx->gen);

    // Transition to STOPPING
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    transition_state(ctx, PROC_STATE_STOPPING);
    fmrb_semaphore_give(g_ctx_lock);

    // TLS destructor will handle cleanup
    fmrb_task_delete(0);
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
static fmrb_task_handle_t g_task_debug = 0;
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

    FMRB_LOGI(TAG, "fmrb_app_spawn: name=%s, vm_type=%d, mode=%d, type=%d",
              attr->name, attr->vm_type, attr->load_mode, attr->type);

    // Validate load mode and source based on VM type
    if (attr->vm_type == FMRB_VM_TYPE_NATIVE) {
        if (!attr->native_func) {
            FMRB_LOGE(TAG, "native_func is NULL for NATIVE mode");
            return FMRB_ERR_INVALID_PARAM;
        }
    } else {
        // mruby or Lua - validate bytecode/file
        if (attr->load_mode == FMRB_LOAD_MODE_BYTECODE) {
            if (!attr->bytecode) {
                FMRB_LOGE(TAG, "bytecode is NULL for BYTECODE mode");
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
    }

    if (!out_id) {
        FMRB_LOGE(TAG, "out_id is NULL");
        return FMRB_ERR_INVALID_PARAM;
    }

    int32_t idx = -1;
    fmrb_app_task_context_t* ctx = NULL;

    // Allocate context slot
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    idx = alloc_ctx_index(attr->app_id, attr->type);
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
    ctx->vm_type = attr->vm_type;

    // Assign memory pool based on task type to avoid conflicts
    ctx->mem_handle = -1; //invalid
    switch (attr->type) {
        case APP_TYPE_KERNEL:
            ctx->mempool_id = POOL_ID_KERNEL;
            break;
        case APP_TYPE_SYSTEM_APP:
            ctx->mempool_id = POOL_ID_SYSTEM_APP;
            break;
        case APP_TYPE_USER_APP:
            if (idx >= PROC_ID_USER_APP0 && idx < PROC_ID_MAX) {
                ctx->mempool_id = POOL_ID_USER_APP0 + (idx - PROC_ID_USER_APP0);
                FMRB_LOGI(TAG, "USER_APP mempool_id: idx=%d, PROC_ID_USER_APP0=%d, POOL_ID_USER_APP0=%d, calculated mempool_id=%d",
                          idx, PROC_ID_USER_APP0, POOL_ID_USER_APP0, ctx->mempool_id);
            } else {
                FMRB_LOGE(TAG, "Invalid USER_APP proc_id: %d", idx);
                goto unwind;
            }
            if(ctx->vm_type == FMRB_VM_TYPE_LUA){
                if (fmrb_mem_handle_exist(ctx->mempool_id) == 0) {
                    void* pool_ptr = fmrb_get_mempool_ptr(ctx->mempool_id);
                    size_t pool_size = fmrb_get_mempool_size(ctx->mempool_id);
                    if (pool_ptr && pool_size > 0) {
                        fmrb_mem_handle_t handle = fmrb_mem_create_handle(pool_ptr, pool_size, ctx->mempool_id);
                        ctx->mem_handle = handle;
                        if (ctx->mem_handle < 0) {
                            FMRB_LOGE(TAG, "[%s] Failed to create memory pool handle for pool_id=%d", attr->name, ctx->mempool_id);
                            goto unwind;
                        }
                        FMRB_LOGI(TAG, "[%s] Memory pool handle created: handle=%d, pool_id=%d, size=%zu", attr->name, handle, ctx->mempool_id, pool_size);
                    } else {
                        FMRB_LOGE(TAG, "[%s] Invalid memory pool: id=%d", attr->name, ctx->mempool_id);
                        goto unwind;
                    }
                } else {
                    FMRB_LOGI(TAG, "[%s] Memory pool handle already exists: id=%d", attr->name, ctx->mempool_id);
                }
            }
            break;
        default:
            FMRB_LOGE(TAG, "Unknown app type: %d", attr->type);
            goto unwind;
    }

    strncpy(ctx->app_name, attr->name, sizeof(ctx->app_name) - 1);
    ctx->app_name[sizeof(ctx->app_name) - 1] = '\0';

    // Copy filepath if provided (for FILE load mode)
    if (attr->load_mode == FMRB_LOAD_MODE_FILE && attr->filepath) {
        strncpy(ctx->filepath, attr->filepath, sizeof(ctx->filepath) - 1);
        ctx->filepath[sizeof(ctx->filepath) - 1] = '\0';
    } else {
        ctx->filepath[0] = '\0';
    }

    // Set load mode and data pointer directly (no pointer tagging)
    if (attr->vm_type == FMRB_VM_TYPE_NATIVE) {
        // For native functions, store function pointer
        ctx->load_mode = FMRB_LOAD_MODE_BYTECODE;
        ctx->load_data = (void*)attr->native_func;
    } else if (attr->load_mode == FMRB_LOAD_MODE_BYTECODE) {
        ctx->load_mode = FMRB_LOAD_MODE_BYTECODE;
        ctx->load_data = (void*)attr->bytecode;
    } else {
        // For FILE mode, use ctx->filepath (copied above)
        ctx->load_mode = FMRB_LOAD_MODE_FILE;
        ctx->load_data = (void*)ctx->filepath;
    }
    ctx->headless = attr->headless;
    ctx->window_pos_x = attr->window_pos_x;
    ctx->window_pos_y = attr->window_pos_y;

    // Initialize window size based on app type
    const fmrb_system_config_t* sys_config = fmrb_kernel_get_config();
    if (attr->type == APP_TYPE_USER_APP && !ctx->headless) {
        // Use attr values if specified, otherwise use system defaults
        ctx->window_width = (attr->window_width > 0) ? attr->window_width : sys_config->default_user_app_width;
        ctx->window_height = (attr->window_height > 0) ? attr->window_height : sys_config->default_user_app_height;
    } else if (attr->type == APP_TYPE_SYSTEM_APP) {
        ctx->window_width = sys_config->display_width;
        ctx->window_height = sys_config->display_height;
    } else {
        ctx->window_width = 0;   // Headless
        ctx->window_height = 0;
    }

    // Initialize Z-order: system/gui_app is always at back (0), others on top
    if (strcmp(ctx->app_name, "system/gui_app") == 0) {
        ctx->z_order = 0;  // system/gui_app always at bottom
    } else {
        // Find max z_order and assign next value
        uint8_t max_z = 0;
        for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
            if (g_ctx_pool[i].state != PROC_STATE_FREE &&
                !g_ctx_pool[i].headless &&
                g_ctx_pool[i].z_order > max_z) {
                max_z = g_ctx_pool[i].z_order;
            }
        }
        ctx->z_order = max_z + 1;
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

    // Close VM based on type
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:
            if (ctx->mrb) {
                mrb_close(ctx->mrb);
                ctx->mrb = NULL;
            }
            break;
        case FMRB_VM_TYPE_LUA:
            if (ctx->lua) {
                fmrb_lua_close(ctx->lua);
                ctx->lua = NULL;
            }
            break;
        case FMRB_VM_TYPE_NATIVE:
            // No VM to close
            break;
        case FMRB_VM_TYPE_MAX:
            // Not a valid VM type
            break;
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
        list[count].stack_high_water = (ctx->task && ctx->state != PROC_STATE_STOPPING)
                                        ? fmrb_task_get_stack_high_water_mark(ctx->task)
                                        : 0;

        // Get memory statistics based on VM type
        list[count].vm_type = ctx->vm_type;

        switch (ctx->vm_type) {
            case FMRB_VM_TYPE_MRUBY: {
                if (ctx->est) {
                    size_t total, used, free;
                    int32_t frag;
                    if (mrb_get_estalloc_stats(ctx->est, &total, &used, &free, &frag) == 0) {
                        list[count].mem_total = total;
                        list[count].mem_used = used;
                        list[count].mem_free = free;
                        list[count].mem_frag = frag;
                    } else {
                        list[count].mem_total = 0;
                        list[count].mem_used = 0;
                        list[count].mem_free = 0;
                        list[count].mem_frag = 0;
                    }
                } else {
                    list[count].mem_total = 0;
                    list[count].mem_used = 0;
                    list[count].mem_free = 0;
                    list[count].mem_frag = 0;
                }
                break;
            }
            case FMRB_VM_TYPE_LUA: {
                if (ctx->mem_handle >= 0) {
                    fmrb_pool_stats_t stats;
                    if (fmrb_mem_get_stats(ctx->mem_handle, &stats) == 0) {
                        list[count].mem_total = stats.total_size;
                        list[count].mem_used = stats.used_size;
                        list[count].mem_free = stats.free_size;
                        list[count].mem_frag = stats.used_blocks + stats.free_blocks;
                    } else {
                        list[count].mem_total = 0;
                        list[count].mem_used = 0;
                        list[count].mem_free = 0;
                        list[count].mem_frag = 0;
                    }
                } else {
                    list[count].mem_total = 0;
                    list[count].mem_used = 0;
                    list[count].mem_free = 0;
                    list[count].mem_frag = 0;
                }
                break;
            }
            case FMRB_VM_TYPE_NATIVE:
            default:
                // Native or unknown - no memory stats available
                list[count].mem_total = 0;
                list[count].mem_used = 0;
                list[count].mem_free = 0;
                list[count].mem_frag = 0;
                break;
        }

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

//called from mruby alloc.c
void* fmrb_get_current_est(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    //ESP_LOGI(TAG, "get  estalloc: app = %s est = %p", ctx->app_name ,ctx->est);
    return ctx->est;
}

//called from mruby alloc.c
void fmrb_set_current_est(void* est)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    ESP_LOGI(TAG, "init estalloc: app = %s est = %p", ctx->app_name ,est);
    ctx->est = est;
}

/**
 * Get window information list for all active apps
 * Returns array of window info (pid, x, y, width, height) for RUNNING/SUSPENDED apps
 */
int32_t fmrb_app_get_window_list(fmrb_window_info_t* list, int32_t max_count) {
    if (!list || max_count <= 0) return 0;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);

    int32_t count = 0;
    for (int32_t i = 0; i < FMRB_MAX_APPS && count < max_count; i++) {
        fmrb_app_task_context_t* ctx = &g_ctx_pool[i];

        // Only include RUNNING or SUSPENDED apps with visible windows
        if ((ctx->state == PROC_STATE_RUNNING || ctx->state == PROC_STATE_SUSPENDED) &&
            !ctx->headless && ctx->window_width > 0 && ctx->window_height > 0) {

            list[count].pid = (uint8_t)ctx->app_id;
            strncpy(list[count].app_name, ctx->app_name, FMRB_MAX_APP_NAME - 1);
            list[count].app_name[FMRB_MAX_APP_NAME - 1] = '\0';
            list[count].x = ctx->window_pos_x;
            list[count].y = ctx->window_pos_y;
            list[count].width = ctx->window_width;
            list[count].height = ctx->window_height;
            list[count].z_order = ctx->z_order;

            count++;
        }
    }

    fmrb_semaphore_give(g_ctx_lock);
    return count;
}
/**
 * Bring window to front by updating z_order
 * System/gui_app (z=0) cannot be brought to front
 */
fmrb_err_t fmrb_app_bring_to_front(uint8_t pid) {
    if (pid >= FMRB_MAX_APPS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);

    fmrb_app_task_context_t* target_ctx = &g_ctx_pool[pid];

    // Check if target app exists and has a window
    if (target_ctx->state != PROC_STATE_RUNNING && target_ctx->state != PROC_STATE_SUSPENDED) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_STATE;
    }

    if (target_ctx->headless) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_PARAM;
    }

    // system/gui_app (z=0) stays at bottom
    if (strcmp(target_ctx->app_name, "system/gui_app") == 0) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_OK;  // No error, just do nothing
    }

    // Find current max z_order (excluding system/gui_app)
    uint8_t max_z = 0;
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        fmrb_app_task_context_t* ctx = &g_ctx_pool[i];
        if ((ctx->state == PROC_STATE_RUNNING || ctx->state == PROC_STATE_SUSPENDED) &&
            !ctx->headless &&
            strcmp(ctx->app_name, "system/gui_app") != 0 &&
            ctx->z_order > max_z) {
            max_z = ctx->z_order;
        }
    }

    // If already at front, do nothing
    if (target_ctx->z_order == max_z) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_OK;
    }

    // Set target to front
    uint8_t old_z = target_ctx->z_order;
    target_ctx->z_order = max_z + 1;

    FMRB_LOGI(TAG, "Brought '%s' (PID %d) to front: Z %d -> %d",
              target_ctx->app_name, pid, old_z, target_ctx->z_order);

    // Send SET_WINDOW_ORDER command to Host
    fmrb_link_graphics_set_window_order_t cmd = {
        .canvas_id = target_ctx->canvas_id,
        .z_order = (int16_t)target_ctx->z_order
    };

    fmrb_err_t ret = fmrb_link_transport_send(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_SET_WINDOW_ORDER,
        (const uint8_t*)&cmd,
        sizeof(cmd)
    );

    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send SET_WINDOW_ORDER to Host: %d", ret);
    }

    fmrb_semaphore_give(g_ctx_lock);
    return FMRB_OK;
}

