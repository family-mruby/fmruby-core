#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <picoruby.h>
#include <mruby/internal.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include "hal.h"
#include "picoruby_fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_kernel.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif
#include "fmrb_lua.h"
#include "fmrb_basic.h"
#include "fmrb_basic_gfx.h"
#include "fmrb_transport.h"
#include "fmrb_link_protocol.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_msg.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "hw_proxy.h"
#endif

// Forward declaration for estalloc helper function
extern int mrb_get_estalloc_stats(void* est_ptr, size_t* total, size_t* used, size_t* free, int32_t* frag);

static const char *TAG = "fmrb_app";

// Max script file size (configurable)
#define MAX_SCRIPT_FILE_SIZE (64 * 1024)  // 64KB

/**
 * Drain APP_CONTROL messages and latch should_exit on stop/exit commands.
 * Used by Lua/Basic runtimes that don't otherwise process kernel messages.
 *
 * Msgpack payload matched: fixmap(1) "cmd" str => "stop" or "exit" str.
 *   {"cmd":"stop"} -> 0x81 0xA3 'cmd' 0xA4 'stop'  (10 bytes)
 *   {"cmd":"exit"} -> 0x81 0xA3 'cmd' 0xA4 'exit'  (10 bytes)
 */
bool fmrb_app_poll_exit_signal(fmrb_app_task_context_t* ctx) {
    if (!ctx) return false;
    if (ctx->should_exit) return true;

    fmrb_msg_t msg;
    while (fmrb_msg_receive(ctx->app_id, &msg, 0) == FMRB_OK) {
        if (msg.type != FMRB_MSG_TYPE_APP_CONTROL || msg.size < 10) {
            continue;
        }
        if (msg.data[0] != 0x81 || msg.data[1] != 0xA3 ||
            memcmp(&msg.data[2], "cmd", 3) != 0 || msg.data[5] != 0xA4) {
            continue;
        }
        const uint8_t* cmd = &msg.data[6];
        if (memcmp(cmd, "stop", 4) == 0 || memcmp(cmd, "exit", 4) == 0) {
            ctx->should_exit = true;
        }
    }
    return ctx->should_exit;
}

// ============================================================================
// Global state (zero-initialized at boot)
// ============================================================================

// Fixed-size context pool (PSRAM - no DMA dependency)
EXT_RAM_BSS_ATTR static fmrb_app_task_context_t g_ctx_pool[FMRB_MAX_APPS];

// Mutex for protecting context pool access
static fmrb_semaphore_t g_ctx_lock = NULL;
static bool g_large_pool_in_use = false;

// State transition strings for debugging
static const char* state_names[] = {
    "FREE", "INIT", "RUNNING", "SUSPENDED", "STOPPING"
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
    // State machine: FREE -> INIT -> RUNNING <-> SUSPENDED
    //                                RUNNING -> STOPPING -> FREE
    switch (from) {
        case PROC_STATE_FREE:
            return (to == PROC_STATE_INIT);
        case PROC_STATE_INIT:
            return (to == PROC_STATE_RUNNING || to == PROC_STATE_FREE);  // Allow rollback
        case PROC_STATE_RUNNING:
            return (to == PROC_STATE_SUSPENDED || to == PROC_STATE_STOPPING);
        case PROC_STATE_SUSPENDED:
            return (to == PROC_STATE_RUNNING || to == PROC_STATE_STOPPING);
        case PROC_STATE_STOPPING:
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

    FMRB_LOGD(TAG, "[%s] === IREP Inspection ===", app_name);
    FMRB_LOGD(TAG, "[%s] irep=%p", app_name, irep);
    FMRB_LOGD(TAG, "[%s] nlocals=%u, nregs=%u, clen=%u, flags=0x%02x",
              app_name, irep->nlocals, irep->nregs,
              irep->clen, irep->flags);
    FMRB_LOGD(TAG, "[%s] iseq=%p", app_name, irep->iseq);
    FMRB_LOGD(TAG, "[%s] pool=%p (plen=%u)", app_name, irep->pool, irep->plen);
    FMRB_LOGD(TAG, "[%s] syms=%p (slen=%u)", app_name, irep->syms, irep->slen);
    FMRB_LOGD(TAG, "[%s] reps=%p (rlen=%u)", app_name, irep->reps, irep->rlen);

    // Check symbol table validity - show first 5 symbols
    if (irep->syms && irep->slen > 0) {
        FMRB_LOGD(TAG, "[%s] Symbol table (slen=%u, first 5):", app_name, irep->slen);
        for (int i = 0; i < 5 && i < irep->slen; i++) {
            mrb_sym sym = irep->syms[i];
            const char* name = mrb_sym_name(mrb, sym);
            FMRB_LOGD(TAG, "[%s]   syms[%d] = %u ('%s')",
                      app_name, i, sym, name ? name : "NULL");
        }
    } else {
        FMRB_LOGW(TAG, "[%s] Symbol table is NULL or empty (slen=%u)", app_name, irep->slen);
    }

    // Check pool content
    if (irep->pool && irep->plen > 0) {
        FMRB_LOGD(TAG, "[%s] Pool (plen=%u, first 3):", app_name, irep->plen);
        for (int i = 0; i < 3 && i < irep->plen; i++) {
            const mrc_pool_value *pv = &irep->pool[i];
            uint32_t tt = pv->tt;
            uint32_t type = tt & 0x7;  // Lower 3 bits = type
            FMRB_LOGD(TAG, "[%s]   pool[%d] type=%u (tt=0x%08x)", app_name, i, type, tt);
            if (type == 0 || type == 2) {  // IREP_TT_STR or IREP_TT_SSTR
                const char* str = pv->u.str;
                bool in_script_buf = false;
                if (script_buf_start && script_buf_end && str) {
                    in_script_buf = (str >= (const char*)script_buf_start &&
                                    str < (const char*)script_buf_end);
                }
                FMRB_LOGD(TAG, "[%s]     -> string_ptr=%p, in_script_buf=%s, content=\"%s\"",
                          app_name, (void*)str, in_script_buf ? "YES" : "NO", str ? str : "NULL");
            }
        }
    } else {
        FMRB_LOGD(TAG, "[%s] Pool is NULL or empty (plen=%u)", app_name, irep->plen);
    }

    // Show first 10 instruction bytes
    if (irep->iseq) {
        FMRB_LOGD(TAG, "[%s] First 10 iseq bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                  app_name,
                  irep->iseq[0], irep->iseq[1], irep->iseq[2], irep->iseq[3], irep->iseq[4],
                  irep->iseq[5], irep->iseq[6], irep->iseq[7], irep->iseq[8], irep->iseq[9]);
    } else {
        FMRB_LOGW(TAG, "[%s] iseq is NULL", app_name);
    }

    FMRB_LOGD(TAG, "[%s] === End IREP Inspection ===", app_name);
}

/**
 * TLS destructor - called automatically when task is deleted
 * Note: Resources are already cleaned up in app_task_entry() before fmrb_task_delete()
 */
static void tls_destructor(int idx, void* pv) {
    (void)idx;
    (void)pv;
    // IMPORTANT: Do not call ESP_LOGI or any FreeRTOS API here.
    // ESP-IDF doc: "Deletion Callbacks should never attempt to block"
    // On SMP, calling FreeRTOS primitives from TLS destructor can corrupt
    // scheduler state and crash in prvSelectHighestPriorityTaskSMP.
    // All resource cleanup is performed in app_task_main() before fmrb_task_delete().
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

// ============================================================================
// VM lifecycle helper functions
// ============================================================================

/**
 * Create mruby VM instance
 * @return 0 on success, -1 on error
 */
static int create_vm_mruby(fmrb_app_task_context_t* ctx) {
    void* pool_ptr = fmrb_get_mempool_ptr(ctx->mempool_id);
    size_t pool_size = fmrb_get_mempool_size(ctx->mempool_id);
    FMRB_LOGI(TAG, "[%s] mempool_id=%d, ptr=%p, size=%zu",
              ctx->app_name, ctx->mempool_id, pool_ptr, pool_size);
    fmrb_mempool_check_pointer(pool_ptr);

    ctx->mrb = mrb_open_with_custom_alloc(pool_ptr, pool_size);
    FMRB_LOGD(TAG, "[%s] mrb_open_with_custom_alloc returned: %p", ctx->app_name, ctx->mrb);

    if (!ctx->mrb) {
        FMRB_LOGE(TAG, "[%s] Failed to open mruby VM", ctx->app_name);
        return -1;
    }

    // Register VM to tick manager after successful creation
    hal_register_vm(ctx->mrb);

    FMRB_LOGI(TAG, "[%s] mruby VM created successfully", ctx->app_name);
    return 0;
}

/**
 * Create Lua VM instance
 * @return 0 on success, -1 on error
 */
static int create_vm_lua(fmrb_app_task_context_t* ctx) {
    ctx->lua = fmrb_lua_newstate(ctx);
    if (!ctx->lua) {
        FMRB_LOGE(TAG, "[%s] Failed to open Lua VM", ctx->app_name);
        return -1;
    }
    fmrb_lua_openlibs(ctx->lua);

    FMRB_LOGI(TAG, "[%s] Lua VM created with mempool=%d",
              ctx->app_name, ctx->mempool_id);
    return 0;
}

// Last error buffer (PSRAM on ESP32)
#define FMRB_ERROR_BUF_SIZE 1024
static char s_last_error_name[32] = {0};
#ifndef CONFIG_IDF_TARGET_LINUX
EXT_RAM_BSS_ATTR static char s_last_error_msg[FMRB_ERROR_BUF_SIZE];
#else
static char s_last_error_msg[FMRB_ERROR_BUF_SIZE];
#endif

const char* fmrb_app_get_last_error_name(void) { return s_last_error_name; }
const char* fmrb_app_get_last_error_msg(void) { return s_last_error_msg; }

// Store error with backtrace into static buffer
static void set_last_error(fmrb_app_task_context_t *ctx, const char *plain_msg, mrb_state *mrb)
{
    strncpy(s_last_error_name, ctx->app_name, sizeof(s_last_error_name) - 1);
    s_last_error_name[sizeof(s_last_error_name) - 1] = '\0';

    size_t pos = 0;
    pos += snprintf(s_last_error_msg + pos, FMRB_ERROR_BUF_SIZE - pos, "%s", plain_msg);

    // Append backtrace if exception is available
    if (mrb && mrb->exc) {
        mrb_value bt = mrb_exc_backtrace(mrb, mrb_obj_value(mrb->exc));
        if (mrb_array_p(bt)) {
            mrb_int bt_len = RARRAY_LEN(bt);
            for (mrb_int i = 0; i < bt_len && pos < FMRB_ERROR_BUF_SIZE - 2; i++) {
                mrb_value line = mrb_ary_ref(mrb, bt, i);
                if (mrb_string_p(line)) {
                    pos += snprintf(s_last_error_msg + pos, FMRB_ERROR_BUF_SIZE - pos,
                                   "\n%s", RSTRING_PTR(line));
                }
            }
        }
    }
}

// Send lightweight error notification to kernel
// Full error details are in static buffer, kernel reads via fmrb_app_get_last_error_*
static void notify_error_to_kernel(fmrb_app_task_context_t *ctx)
{
    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_CONTROL,
        .src_pid = ctx->app_id,
    };

    uint8_t *data = msg.data;
    size_t pos = 0;

    // Map with 2 entries: {"cmd": "app_error", "name": app_name}
    data[pos++] = 0x82;

    // "cmd" => "app_error"
    data[pos++] = 0xA3;
    memcpy(&data[pos], "cmd", 3); pos += 3;
    data[pos++] = 0xA9;
    memcpy(&data[pos], "app_error", 9); pos += 9;

    // "name" => app_name
    size_t name_len = strlen(ctx->app_name);
    if (name_len > 31) name_len = 31;
    data[pos++] = 0xA4;
    memcpy(&data[pos], "name", 4); pos += 4;
    data[pos++] = 0xA0 | (uint8_t)name_len;
    memcpy(&data[pos], ctx->app_name, name_len); pos += name_len;

    msg.size = pos;

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_KERNEL, &msg, 100);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send error notification to kernel: %d", ret);
    }
}

/**
 * Execute mruby script
 * @param ctx Task context
 * @param load_mode Load mode (bytecode or file)
 * @param load_data Pointer to bytecode or filepath string
 * @param[out] script_buffer_out Allocated script buffer (caller must free)
 * @param[out] need_free_out Flag indicating if buffer needs freeing
 * @return 0 on success, -1 on error
 */
static int execute_mruby_script(fmrb_app_task_context_t* ctx,
                                fmrb_load_mode_t load_mode,
                                void* load_data,
                                char** script_buffer_out,
                                bool* need_free_out) {
    mrc_irep *irep_obj = NULL;
    const unsigned char* irep_ptr = NULL;
    char* script_buffer = NULL;
    bool need_free_script = false;

    mrc_ccontext *cc = mrc_ccontext_new(ctx->mrb);
    if (!cc) {
        FMRB_LOGE(TAG, "[%s] Failed to create compile context", ctx->app_name);
        return -1;
    }

    if (load_mode == FMRB_LOAD_MODE_BYTECODE) {
        // Load from bytecode (IREP)
        irep_ptr = (const unsigned char*)load_data;
        irep_obj = mrb_read_irep(ctx->mrb, irep_ptr);
        if (irep_obj == NULL) {
            FMRB_LOGE(TAG, "[%s] Failed to read IREP bytecode", ctx->app_name);
            mrc_ccontext_free(cc);
            return -1;
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
            mrc_ccontext_free(cc);
            return -1;
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
                mrb_value exc_str = mrb_exc_get_output(ctx->mrb, (struct RObject *)ctx->mrb->exc);
                set_last_error(ctx, RSTRING_PTR(exc_str), ctx->mrb);
                FMRB_LOGE(TAG, "[%s] %s", ctx->app_name, s_last_error_msg);
                notify_error_to_kernel(ctx);
                mrb_print_error(ctx->mrb);
            } else {
                set_last_error(ctx, "Compile error (unknown)", NULL);
                notify_error_to_kernel(ctx);
            }
            mrc_ccontext_free(cc);
            fmrb_sys_free(script_buffer);
            return -1;
        }

        FMRB_LOGI(TAG, "[%s] Ruby script compiled successfully", ctx->app_name);

        // Inspect IREP structure (with script buffer range for analysis)
        inspect_irep(ctx->mrb, ctx->app_name, irep_obj,
                    script_buffer, (uint8_t*)script_buffer + script_size);

        // script_buffer cannot be freed here.
        // TODO: investigate how the buffer is used.
    } else {
        FMRB_LOGE(TAG, "[%s] Invalid load mode: %d", ctx->app_name, load_mode);
        mrc_ccontext_free(cc);
        return -1;
    }

    // Execute irep
    FMRB_LOGD(TAG, "[%s] Execute irep", ctx->app_name);

    mrb_value name = mrb_str_new_cstr(ctx->mrb, ctx->app_name);
    mrb_value task = mrc_create_task(cc, irep_obj, name, mrb_nil_value(), mrb_obj_value(ctx->mrb->top_self));
    if (mrb_nil_p(task)) {
        FMRB_LOGE(TAG, "[%s] mrc_create_task failed, mrb->exc=%p", ctx->app_name, ctx->mrb->exc);
        mrc_ccontext_free(cc);
        if (need_free_script) {
            fmrb_sys_free(script_buffer);
        }
        return -1;
    }

    FMRB_LOGD(TAG, "[%s] mrb_task_run - BEFORE execution", ctx->app_name);
    mrb_task_run(ctx->mrb);
    FMRB_LOGD(TAG, "[%s] mrb_task_run - AFTER execution, mrb->exc=%p", ctx->app_name, ctx->mrb->exc);

    if (ctx->mrb->exc) {
        mrb_value exc_str = mrb_exc_get_output(ctx->mrb, (struct RObject *)ctx->mrb->exc);
        set_last_error(ctx, RSTRING_PTR(exc_str), ctx->mrb);
        FMRB_LOGE(TAG, "[%s] %s", ctx->app_name, s_last_error_msg);
        notify_error_to_kernel(ctx);
        mrb_print_error(ctx->mrb);
    } else {
        FMRB_LOGI(TAG, "[%s] No exception detected", ctx->app_name);
    }

    mrb_vm_ci_env_clear(ctx->mrb, ctx->mrb->c->cibase);
    mrc_irep_free(cc, irep_obj);
    mrc_ccontext_free(cc);

    // Return script buffer to caller for later cleanup
    *script_buffer_out = script_buffer;
    *need_free_out = need_free_script;

    return 0;
}

/**
 * Lua debug hook: count-based, periodically polls for a stop request from
 * the kernel. When the kernel asks the app to close, this raises a Lua
 * error so lua_pcall returns and the task wrapper can run its cleanup.
 */
static void lua_exit_check_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) return;
    if (fmrb_app_poll_exit_signal(ctx)) {
        FMRB_LOGI(TAG, "[%s] Lua close requested, unwinding VM", ctx->app_name);
        lua_pushliteral(L, "__fmrb_close_requested__");
        lua_error(L);  // longjmp out of pcall; does not return
    }
}

/**
 * Execute Lua script
 * @return 0 on success, -1 on error
 */
static int execute_lua_script(fmrb_app_task_context_t* ctx,
                              fmrb_load_mode_t load_mode,
                              void* load_data) {
    char* script_buffer = NULL;

    if (load_mode == FMRB_LOAD_MODE_BYTECODE) {
        // Load from Lua bytecode chunk
        FMRB_LOGW(TAG, "[%s] Lua bytecode loading not yet implemented", ctx->app_name);
        return -1;
    } else if (load_mode == FMRB_LOAD_MODE_FILE) {
        // Load from Lua source file
        const char* filepath = (const char*)load_data;
        size_t script_size = 0;

        FMRB_LOGI(TAG, "[%s] Loading Lua script from file: %s", ctx->app_name, filepath);

        script_buffer = load_script_file(filepath, &script_size);
        if (!script_buffer) {
            FMRB_LOGE(TAG, "[%s] Failed to load script file: %s", ctx->app_name, filepath);
            return -1;
        }

        // Load and compile Lua script
        int load_result = luaL_loadbuffer(ctx->lua, script_buffer, script_size, filepath);
        if (load_result != LUA_OK) {
            const char* err_msg = lua_tostring(ctx->lua, -1);
            FMRB_LOGE(TAG, "[%s] Failed to compile Lua script: %s",
                      ctx->app_name, err_msg ? err_msg : "unknown error");
            lua_pop(ctx->lua, 1);  // Pop error message
            fmrb_sys_free(script_buffer);
            return -1;
        }

        FMRB_LOGI(TAG, "[%s] Lua script compiled successfully", ctx->app_name);

        // Free script buffer immediately after compilation (no longer needed)
        fmrb_sys_free(script_buffer);
        script_buffer = NULL;

        // Install exit-signal hook: fires every N VM instructions so the
        // kernel can request graceful shutdown without killing the task.
        lua_sethook(ctx->lua, lua_exit_check_hook, LUA_MASKCOUNT, 100);

        // Execute Lua script
        int exec_result = lua_pcall(ctx->lua, 0, LUA_MULTRET, 0);
        if (exec_result != LUA_OK) {
            const char* err_msg = lua_tostring(ctx->lua, -1);
            // Close-request sentinel is not a real error; log as info so users
            // don't see a scary E-level line on normal shutdown.
            if (err_msg && strstr(err_msg, "__fmrb_close_requested__")) {
                FMRB_LOGI(TAG, "[%s] Lua script closed by user", ctx->app_name);
                lua_pop(ctx->lua, 1);
                return 0;
            }
            FMRB_LOGE(TAG, "[%s] Lua execution error: %s",
                      ctx->app_name, err_msg ? err_msg : "unknown error");
            lua_pop(ctx->lua, 1);  // Pop error message
            return -1;
        } else {
            FMRB_LOGI(TAG, "[%s] Lua script executed successfully", ctx->app_name);
        }
    }

    return 0;
}

/**
 * Execute BASIC script
 * @return 0 on success, -1 on error
 */
static int execute_basic_script(fmrb_app_task_context_t* ctx,
                                fmrb_load_mode_t load_mode,
                                void* load_data) {
    char* script_buffer = NULL;

    if (load_mode == FMRB_LOAD_MODE_BYTECODE) {
        // BASIC doesn't support bytecode yet
        FMRB_LOGW(TAG, "[%s] BASIC bytecode loading not supported", ctx->app_name);
        return -1;
    } else if (load_mode == FMRB_LOAD_MODE_FILE) {
        // Load from BASIC source file
        const char* filepath = (const char*)load_data;
        size_t script_size = 0;

        FMRB_LOGI(TAG, "[%s] Loading BASIC script from file: %s", ctx->app_name, filepath);

        script_buffer = load_script_file(filepath, &script_size);
        if (!script_buffer) {
            FMRB_LOGE(TAG, "[%s] Failed to load script file: %s", ctx->app_name, filepath);
            return -1;
        }

        // Load BASIC program
        fmrb_err_t load_result = fmrb_basic_load(ctx->basic, script_buffer);
        if (load_result != FMRB_OK) {
            FMRB_LOGE(TAG, "[%s] Failed to load BASIC program", ctx->app_name);
            fmrb_sys_free(script_buffer);
            return -1;
        }

        FMRB_LOGI(TAG, "[%s] BASIC program loaded successfully", ctx->app_name);

        // Free script buffer immediately after loading (no longer needed)
        fmrb_sys_free(script_buffer);
        script_buffer = NULL;

        // Initialize console window for PRINT output (if not headless)
        basic_console_ctx_t* console = NULL;
        if (!ctx->headless) {
            console = (basic_console_ctx_t*)fmrb_malloc(ctx->mem_handle,
                                                         sizeof(basic_console_ctx_t));
            if (console) {
                memset(console, 0, sizeof(basic_console_ctx_t));
                if (basic_console_init(console, ctx) == FMRB_OK) {
                    fmrb_basic_set_output_cb(ctx->basic,
                                             basic_console_output_cb, console);
                    basic_console_register_gfx_ops(ctx->basic, console);
                } else {
                    fmrb_free(ctx->mem_handle, console);
                    console = NULL;
                }
            } else {
                FMRB_LOGW(TAG, "[%s] Failed to allocate console context", ctx->app_name);
            }
        }

        // Execute BASIC program
        fmrb_err_t exec_result = fmrb_basic_run(ctx->basic);
        if (exec_result != FMRB_OK) {
            FMRB_LOGE(TAG, "[%s] BASIC execution error", ctx->app_name);
        } else {
            FMRB_LOGI(TAG, "[%s] BASIC program executed successfully", ctx->app_name);
        }

        // Cleanup console
        if (console) {
            basic_console_destroy(console);
            fmrb_free(ctx->mem_handle, console);
        }

        if (exec_result != FMRB_OK) {
            return -1;
        }
    }

    return 0;
}

/**
 * Execute native C function
 * @return 0 on success, -1 on error
 */
static int execute_native_function(fmrb_app_task_context_t* ctx, void* load_data) {
    void (*native_func)(void*) = (void (*)(void*))load_data;
    if (native_func) {
        FMRB_LOGI(TAG, "[%s] Executing native function", ctx->app_name);
        native_func(ctx);
        return 0;
    } else {
        FMRB_LOGE(TAG, "[%s] Native function pointer is NULL", ctx->app_name);
        return -1;
    }
}

/**
 * Destroy VM and cleanup resources
 */
static void destroy_vm(fmrb_app_task_context_t* ctx) {
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:
            if (ctx->mrb) {
                FMRB_LOGI(TAG, "[%s] Closing mruby VM", ctx->app_name);
                // Cleanup VM resources (unregister from HAL tick manager)
                fmrb_app_vm_cleanup(ctx->mrb);
                // NOTE: mrb_close() causes segfault when called after mrc_irep_free()
                // This is a known issue in PicoRuby (see picoruby-bin-microruby/tools/microruby/microruby.c:335)
                // We call mrc_irep_free() + mrc_ccontext_free() in execution path,
                // so we skip mrb_close() here to avoid double-free.
                // Memory will be cleaned up when mem_handle is destroyed.
                // mrb_close(ctx->mrb);
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
        case FMRB_VM_TYPE_BASIC:
            if (ctx->basic) {
                FMRB_LOGI(TAG, "[%s] Closing BASIC VM", ctx->app_name);
                fmrb_basic_close(ctx->basic);
                ctx->basic = NULL;
            }
            break;
        case FMRB_VM_TYPE_NATIVE:
            // No VM to close for native functions
            break;
        default:
            FMRB_LOGW(TAG, "[%s] Unknown VM type: %d", ctx->app_name, ctx->vm_type);
            break;
    }
}

/**
 * Application task entry point
 */
static void app_task_main(void* arg) {
    fmrb_app_task_context_t* ctx = (fmrb_app_task_context_t*)arg;
    char* script_buffer = NULL;
    bool need_free_script = false;

    // Reset per-run termination flag (context slots are reused on respawn)
    ctx->should_exit = false;

    // Register in TLS with destructor
    fmrb_task_set_tls_with_del(0, FMRB_APP_TLS_INDEX, ctx, tls_destructor);

    FMRB_LOGI(TAG, "[%s gen=%u] Task started (core=%d, prio=%u)",
             ctx->app_name, ctx->gen, fmrb_get_core_id(), fmrb_task_get_priority(0));

    // Create VM based on vm_type
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:
            if (create_vm_mruby(ctx) != 0) {
                goto cleanup;
            }
            break;
        case FMRB_VM_TYPE_LUA:
            if (create_vm_lua(ctx) != 0) {
                goto cleanup;
            }
            break;
        case FMRB_VM_TYPE_BASIC:
            FMRB_LOGI(TAG, "[%s] Creating BASIC VM", ctx->app_name);
            ctx->basic = fmrb_basic_newstate(ctx);
            if (!ctx->basic) {
                FMRB_LOGE(TAG, "[%s] Failed to create BASIC state", ctx->app_name);
                goto cleanup;
            }
            break;
        case FMRB_VM_TYPE_NATIVE:
            FMRB_LOGI(TAG, "[%s] Native function mode", ctx->app_name);
            break;
        default:
            FMRB_LOGE(TAG, "[%s] Unknown VM type: %d", ctx->app_name, ctx->vm_type);
            goto cleanup;
    }

    // Create the message queue before the state transitions to RUNNING so
    // any sender that observes RUNNING is guaranteed to find a registered
    // queue. Otherwise messages sent during the window between RUNNING and
    // the mruby task's _init binding are silently dropped (FMRB_ERR_NOT_FOUND).
    {
        uint32_t queue_len = (ctx->type == APP_TYPE_KERNEL)
                             ? FMRB_KERNEL_MSG_QUEUE_LEN
                             : FMRB_USER_APP_MSG_QUEUE_LEN;
        fmrb_msg_queue_config_t queue_config = {
            .queue_length = queue_len,
            .message_size = sizeof(fmrb_msg_t)
        };
        fmrb_err_t qret = fmrb_msg_create_queue(ctx->app_id, &queue_config);
        if (qret != FMRB_OK) {
            FMRB_LOGE(TAG, "[%s] Failed to create message queue: %d",
                      ctx->app_name, qret);
            goto cleanup;
        }
    }

    // Transition to RUNNING
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    if (!transition_state(ctx, PROC_STATE_RUNNING)) {
        fmrb_semaphore_give(g_ctx_lock);
        FMRB_LOGE(TAG, "[%s] Failed to transition to RUNNING", ctx->app_name);
        goto cleanup;
    }
    fmrb_semaphore_give(g_ctx_lock);

    // Get load mode and data from context
    fmrb_load_mode_t load_mode = ctx->load_mode;
    void* load_data = ctx->load_data;

    // Execute based on VM type
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY:
            if (execute_mruby_script(ctx, load_mode, load_data, &script_buffer, &need_free_script) != 0) {
                // Error already logged in execute_mruby_script
            }
            break;

        case FMRB_VM_TYPE_LUA:
            if (execute_lua_script(ctx, load_mode, load_data) != 0) {
                // Error already logged in execute_lua_script
            }
            break;

        case FMRB_VM_TYPE_BASIC:
            if (execute_basic_script(ctx, load_mode, load_data) != 0) {
                // Error already logged in execute_basic_script
            }
            break;

        case FMRB_VM_TYPE_NATIVE:
            if (execute_native_function(ctx, load_data) != 0) {
                // Error already logged in execute_native_function
            }
            break;

        default:
            FMRB_LOGE(TAG, "[%s] Unknown VM type: %d", ctx->app_name, ctx->vm_type);
            break;
    }

cleanup:
    FMRB_LOGI(TAG, "[%s gen=%u] Task exiting normally", ctx->app_name, ctx->gen);

    // Perform cleanup immediately before task deletion
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);

    // Move to STOPPING so the kernel reaper sees a consistent state.
    // INIT (setup failed before RUNNING) is also reapable.
    if (ctx->state == PROC_STATE_RUNNING || ctx->state == PROC_STATE_SUSPENDED) {
        transition_state(ctx, PROC_STATE_STOPPING);
    }
    // INIT remains INIT here — fmrb_app_reap() accepts INIT or STOPPING.

    // Close VM based on type (BEFORE destroying memory handle!)
    destroy_vm(ctx);

    // Delete canvas if not already cleaned up by Ruby _cleanup()
    // (e.g. uncaught exception skips destroy() -> _cleanup())
    if (ctx->canvas_id != 0) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (gfx_ctx) {
            FMRB_LOGI(TAG, "[%s] C cleanup: deleting canvas %u", ctx->app_name, ctx->canvas_id);
            fmrb_gfx_delete_canvas(gfx_ctx, ctx->canvas_id);
        }
        ctx->canvas_id = 0;
    }
    if (ctx->has_background_canvas && ctx->bg_canvas_id != 0) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (gfx_ctx) {
            fmrb_gfx_delete_canvas(gfx_ctx, ctx->bg_canvas_id);
        }
        ctx->bg_canvas_id = 0;
    }

    // Delete message queue
    fmrb_msg_delete_queue(ctx->app_id);

#ifndef CONFIG_IDF_TARGET_LINUX
    // Release HW resources (I2C, RMT, GPIO pins)
    hw_proxy_release_resources((hw_proxy_task_handle_t)fmrb_task_get_current());
#endif

    // Free script buffer AFTER VM is closed (IRep may reference this buffer)
    if (need_free_script && script_buffer) {
        fmrb_sys_free(script_buffer);
    }

    // Destroy memory handle AFTER VM is closed
    if (ctx->mem_handle >= 0) {
        fmrb_mem_destroy_handle(ctx->mem_handle);
        ctx->mem_handle = -1;
    }

    // Delete semaphore
    if (ctx->semaphore) {
        fmrb_semaphore_delete(ctx->semaphore);
        ctx->semaphore = NULL;
    }

    // Release LARGE pool if this app was using it
    if (ctx->mempool_id == POOL_ID_USER_APP_LARGE) {
        g_large_pool_in_use = false;
        FMRB_LOGI(TAG, "[%s] Released LARGE memory pool", ctx->app_name);
    }

    // Slot stays in STOPPING; the kernel reaper transitions to FREE after
    // deleting this task externally (see fmrb_app_reap).
    fmrb_semaphore_give(g_ctx_lock);

    FMRB_LOGI(TAG, "[%s gen=%u] Resources cleaned up", ctx->app_name, ctx->gen);

    // Notify kernel of app termination AFTER all resources are released.
    // Kernel will call fmrb_app_reap() which deletes this task externally —
    // self-delete on SMP races with the TLS destructor and IDLE-task TCB
    // reclaim, leaving stale entries in the task monitor table.
    {
        fmrb_msg_t exit_msg = {
            .type = FMRB_MSG_TYPE_APP_CONTROL,
            .src_pid = ctx->app_id,
        };
        uint8_t *d = exit_msg.data;
        size_t p = 0;
        d[p++] = 0x81;  // fixmap 1
        d[p++] = 0xA3; memcpy(&d[p], "cmd", 3); p += 3;
        d[p++] = 0xA4; memcpy(&d[p], "exit", 4); p += 4;
        exit_msg.size = p;
        fmrb_msg_send(PROC_ID_KERNEL, &exit_msg, 100);
    }

    // Park until the kernel reaps this task. fmrb_task_delete() called from
    // the kernel context wakes us up by deleting the TCB.
    while (1) {
        fmrb_task_delay(FMRB_MS_TO_TICKS(10000));
    }
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
    transition_state(ctx, PROC_STATE_INIT);
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
            if (idx == PROC_ID_SYSTEM_OVERLAY) {
                ctx->mempool_id = POOL_ID_SYSTEM_OVERLAY;
            } else {
                ctx->mempool_id = POOL_ID_SYSTEM_APP;
            }
            break;
        case APP_TYPE_USER_APP:
            if (idx >= PROC_ID_USER_APP0 && idx < PROC_ID_MAX) {
                if (attr->large_memory) {
                    if (g_large_pool_in_use) {
                        FMRB_LOGE(TAG, "[%s] LARGE memory pool already in use", attr->name);
                        goto unwind;
                    }
                    ctx->mempool_id = POOL_ID_USER_APP_LARGE;
                    g_large_pool_in_use = true;
                    FMRB_LOGI(TAG, "USER_APP using LARGE pool (1MB): idx=%d", idx);
                } else {
                    ctx->mempool_id = POOL_ID_USER_APP0 + (idx - PROC_ID_USER_APP0);
                }
                FMRB_LOGI(TAG, "USER_APP mempool_id: idx=%d, PROC_ID_USER_APP0=%d, POOL_ID_USER_APP0=%d, calculated mempool_id=%d",
                          idx, PROC_ID_USER_APP0, POOL_ID_USER_APP0, ctx->mempool_id);
            } else {
                FMRB_LOGE(TAG, "Invalid USER_APP proc_id: %d", idx);
                goto unwind;
            }
            // Note: MRUBY uses estalloc which manages pool memory directly, so no TLSF handle needed
            if(ctx->vm_type == FMRB_VM_TYPE_LUA || ctx->vm_type == FMRB_VM_TYPE_BASIC){
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
        ctx->window_width = sys_config->display_width - sys_config->display_margin_x;
        ctx->window_height = sys_config->display_height - sys_config->display_margin_y;
    } else {
        ctx->window_width = 0;   // Headless
        ctx->window_height = 0;
    }

    // Initialize Z-order
    // Desktop with background canvas: main canvas at z=254 (foreground menu bar),
    // background canvas at z=0 (created later in _init)
    ctx->has_background_canvas = attr->has_background_canvas;
    ctx->bg_canvas_id = 0;
    ctx->fullscreen = attr->fullscreen;
    ctx->resizable = attr->resizable;
    ctx->min_window_width = attr->min_window_width;
    ctx->min_window_height = attr->min_window_height;
    if (ctx->has_background_canvas) {
        ctx->z_order = 254;  // Main canvas is foreground (menu bar)
    } else if (strcmp(ctx->app_name, "system_overlay") == 0) {
        ctx->z_order = 254;
    } else {
        // Find max z_order among user apps (exclude z=0 and z=254)
        uint8_t max_z = 0;
        for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
            if (g_ctx_pool[i].state != PROC_STATE_FREE &&
                !g_ctx_pool[i].headless &&
                g_ctx_pool[i].z_order > max_z &&
                g_ctx_pool[i].z_order < 254) {
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

    // Already in INIT state (set at line 735)

    // Create FreeRTOS task with flags from task config
    fmrb_base_type_t result;
    FMRB_LOGI(TAG, "fmrb_task_create_ex [%s] (flags=0x%02x)", ctx->app_name, (unsigned)attr->flags);
    result = fmrb_task_create_ex(
        app_task_main, ctx->app_name, attr->stack_words,
        ctx, attr->priority, &ctx->task, attr->flags);

    if (result != FMRB_PASS) {
#ifndef CONFIG_IDF_TARGET_LINUX
        FMRB_LOGE(TAG, "[%s] Failed to create task (stack=%u, free_internal=%u, free_psram=%u)",
                 ctx->app_name, (unsigned)attr->stack_words,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#else
        FMRB_LOGE(TAG, "[%s] Failed to create task (stack=%u)", ctx->app_name, (unsigned)attr->stack_words);
#endif
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
                // Cleanup VM resources (unregister from HAL tick manager)
                fmrb_app_vm_cleanup(ctx->mrb);
                // NOTE: Same as cleanup path - skip mrb_close() to avoid segfault
                // mrb_close(ctx->mrb);
                ctx->mrb = NULL;
            }
            break;
        case FMRB_VM_TYPE_LUA:
            if (ctx->lua) {
                fmrb_lua_close(ctx->lua);
                ctx->lua = NULL;
            }
            break;
        case FMRB_VM_TYPE_BASIC:
            if (ctx->basic) {
                fmrb_basic_close(ctx->basic);
                ctx->basic = NULL;
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
    ctx->task = 0;  // prevent double-delete from a subsequent reap
    fmrb_semaphore_give(g_ctx_lock);

    // Notify task to terminate
    if (task) {
        fmrb_task_notify_give(task);  // Wake up task if waiting
        fmrb_task_delete(task);       // Force delete
    }

    // Move slot to FREE so it can be reused. Note: this leaves resources
    // (mem handle, semaphore, etc.) leaked since kill bypasses app_task_main
    // cleanup. Kill is an emergency path; prefer graceful exit when possible.
    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    if (ctx->state == PROC_STATE_STOPPING) {
        transition_state(ctx, PROC_STATE_FREE);
    }
    fmrb_semaphore_give(g_ctx_lock);

    FMRB_LOGI(TAG, "[%s gen=%u] Killed", ctx->app_name, ctx->gen);
    return true;
}

/**
 * Reap a parked app task: delete its FreeRTOS task and free the slot.
 *
 * Called by the kernel after receiving an exit notification from an app that
 * has finished its own resource cleanup and parked itself in app_task_main().
 * Deleting the task from another context avoids the SMP self-delete races
 * (TLS destructor + IDLE-task TCB reclaim) that previously left stale
 * entries in the task monitor table.
 *
 * Idempotent: returns true if the slot was already freed.
 */
bool fmrb_app_reap(int32_t id) {
    if (id < 0 || id >= FMRB_MAX_APPS) return false;

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);
    fmrb_app_task_context_t* ctx = &g_ctx_pool[id];

    // Already reaped or never spawned
    if (ctx->state == PROC_STATE_FREE) {
        fmrb_semaphore_give(g_ctx_lock);
        return true;
    }

    // Reap from STOPPING (normal exit) or INIT (setup failed early).
    // RUNNING/SUSPENDED here is the expected case for the early "exit"
    // notification sent from Ruby's FmrbApp#destroy before the C-side
    // app_task_main cleanup runs — silently skip; the second exit
    // notification (from app_task_main, after STOPPING) will reap.
    if (ctx->state != PROC_STATE_STOPPING && ctx->state != PROC_STATE_INIT) {
        fmrb_semaphore_give(g_ctx_lock);
        return false;
    }

    fmrb_task_handle_t task = ctx->task;
    ctx->task = 0;
    transition_state(ctx, PROC_STATE_FREE);
    uint32_t gen = ctx->gen;
    const char* name = ctx->app_name;
    fmrb_semaphore_give(g_ctx_lock);

    // Delete task outside the lock (unregister_task + vTaskDelete)
    if (task) {
        fmrb_task_delete(task);
    }

    FMRB_LOGI(TAG, "[%s gen=%u] Reaped", name, gen);
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
    uint16_t canvas_id = ctx->canvas_id;
    uint16_t bg_canvas_id = ctx->bg_canvas_id;
    bool has_bg = ctx->has_background_canvas;
    bool headless = ctx->headless;
    fmrb_semaphore_give(g_ctx_lock);

    if (task) {
        fmrb_task_suspend(task);
        FMRB_LOGI(TAG, "[%s gen=%u] Suspended", ctx->app_name, ctx->gen);

        // Hide canvas on suspend
        if (!headless && canvas_id > 0) {
            fmrb_link_graphics_set_canvas_visible_t cmd = {
                .canvas_id = canvas_id,
                .visible = 0
            };
            fmrb_transport_send(
                FMRB_LINK_TYPE_GRAPHICS,
                FMRB_LINK_GFX_SET_CANVAS_VISIBLE,
                (const uint8_t*)&cmd,
                sizeof(cmd),
                FMRB_TRANSPORT_TIMEOUT_DEFAULT
            );
            if (has_bg && bg_canvas_id > 0) {
                cmd.canvas_id = bg_canvas_id;
                fmrb_transport_send(
                    FMRB_LINK_TYPE_GRAPHICS,
                    FMRB_LINK_GFX_SET_CANVAS_VISIBLE,
                    (const uint8_t*)&cmd,
                    sizeof(cmd),
                    FMRB_TRANSPORT_TIMEOUT_DEFAULT
                );
            }
        }

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
    uint16_t canvas_id = ctx->canvas_id;
    uint16_t bg_canvas_id = ctx->bg_canvas_id;
    bool has_bg = ctx->has_background_canvas;
    bool headless = ctx->headless;
    fmrb_semaphore_give(g_ctx_lock);

    if (task) {
        fmrb_task_resume(task);
        FMRB_LOGI(TAG, "[%s gen=%u] Resumed", ctx->app_name, ctx->gen);

        // Show canvas on resume
        if (!headless && canvas_id > 0) {
            fmrb_link_graphics_set_canvas_visible_t cmd = {
                .canvas_id = canvas_id,
                .visible = 1
            };
            fmrb_transport_send(
                FMRB_LINK_TYPE_GRAPHICS,
                FMRB_LINK_GFX_SET_CANVAS_VISIBLE,
                (const uint8_t*)&cmd,
                sizeof(cmd),
                FMRB_TRANSPORT_TIMEOUT_DEFAULT
            );
            if (has_bg && bg_canvas_id > 0) {
                cmd.canvas_id = bg_canvas_id;
                fmrb_transport_send(
                    FMRB_LINK_TYPE_GRAPHICS,
                    FMRB_LINK_GFX_SET_CANVAS_VISIBLE,
                    (const uint8_t*)&cmd,
                    sizeof(cmd),
                    FMRB_TRANSPORT_TIMEOUT_DEFAULT
                );
            }
        }

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
            case FMRB_VM_TYPE_BASIC: {
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
    FMRB_LOGD(TAG, "init estalloc: app = %s est = %p", ctx->app_name, est);
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
            list[count].fullscreen = ctx->fullscreen;
            list[count].resizable = ctx->resizable;
            list[count].min_width = ctx->min_window_width;
            list[count].min_height = ctx->min_window_height;

            count++;
        }
    }

    fmrb_semaphore_give(g_ctx_lock);
    return count;
}
// Z-order threshold for reordering (leave some headroom before uint8_t max)
#define FMRB_Z_ORDER_REORDER_THRESHOLD 20

/**
 * Reorder all z_order values to compact them
 * system_desktop stays at Z=0, system_overlay stays at Z=254, others are reassigned sequentially
 */
static void reorder_z_orders(void) {
    // Collect user app windows (excluding desktop, overlay, and headless)
    fmrb_app_task_context_t* windows[FMRB_MAX_APPS];
    int32_t count = 0;

    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        fmrb_app_task_context_t* ctx = &g_ctx_pool[i];
        if ((ctx->state == PROC_STATE_RUNNING || ctx->state == PROC_STATE_SUSPENDED) &&
            !ctx->headless &&
            strcmp(ctx->app_name, "system_desktop") != 0 &&
            strcmp(ctx->app_name, "system_overlay") != 0) {
            windows[count++] = ctx;
        }
    }

    // Sort by current z_order (bubble sort - simple for small arrays)
    for (int32_t i = 0; i < count - 1; i++) {
        for (int32_t j = 0; j < count - i - 1; j++) {
            if (windows[j]->z_order > windows[j + 1]->z_order) {
                fmrb_app_task_context_t* temp = windows[j];
                windows[j] = windows[j + 1];
                windows[j + 1] = temp;
            }
        }
    }

    // Reassign z_order sequentially starting from 1
    // (system_gui stays at 0)
    for (int32_t i = 0; i < count; i++) {
        uint8_t old_z = windows[i]->z_order;
        windows[i]->z_order = i + 1;

        if (old_z != windows[i]->z_order) {
            FMRB_LOGI(TAG, "Reordered '%s' (PID %d): Z %d -> %d",
                      windows[i]->app_name, windows[i]->app_id, old_z, windows[i]->z_order);

            // Send updated z_order to Host
            fmrb_link_graphics_set_window_order_t cmd = {
                .canvas_id = windows[i]->canvas_id,
                .z_order = (int16_t)windows[i]->z_order
            };

            fmrb_transport_send(
                FMRB_LINK_TYPE_GRAPHICS,
                FMRB_LINK_GFX_SET_WINDOW_ORDER,
                (const uint8_t*)&cmd,
                sizeof(cmd),
                FMRB_TRANSPORT_TIMEOUT_DEFAULT
            );
        }
    }

    FMRB_LOGI(TAG, "Z-order reordering complete (%d windows)", count);
}

/**
 * Bring window to front by updating z_order
 * System/gui_app (z=0) cannot be brought to front
 * Automatically reorders when z_order exceeds threshold
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

    // system_desktop has fixed z-order (z=0 bg + z=254 fg)
    if (strcmp(target_ctx->app_name, "system_desktop") == 0) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_OK;
    }

    // Find current max z_order (excluding desktop and overlay)
    uint8_t max_z = 0;
    for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
        fmrb_app_task_context_t* ctx = &g_ctx_pool[i];
        if ((ctx->state == PROC_STATE_RUNNING || ctx->state == PROC_STATE_SUSPENDED) &&
            !ctx->headless &&
            strcmp(ctx->app_name, "system_desktop") != 0 &&
            ctx->z_order > max_z) {
            max_z = ctx->z_order;
        }
    }

    // If already at front, do nothing
    if (target_ctx->z_order == max_z) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_OK;
    }

    // Check if we need to reorder due to threshold
    if (max_z >= FMRB_Z_ORDER_REORDER_THRESHOLD) {
        FMRB_LOGI(TAG, "Z-order reached threshold (%d >= %d), reordering...",
                  max_z, FMRB_Z_ORDER_REORDER_THRESHOLD);
        reorder_z_orders();

        // After reordering, find new max_z
        max_z = 0;
        for (int32_t i = 0; i < FMRB_MAX_APPS; i++) {
            fmrb_app_task_context_t* ctx = &g_ctx_pool[i];
            if ((ctx->state == PROC_STATE_RUNNING || ctx->state == PROC_STATE_SUSPENDED) &&
                !ctx->headless &&
                strcmp(ctx->app_name, "system_desktop") != 0 &&
                strcmp(ctx->app_name, "system_overlay") != 0 &&
                ctx->z_order > max_z) {
                max_z = ctx->z_order;
            }
        }
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

    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_SET_WINDOW_ORDER,
        (const uint8_t*)&cmd,
        sizeof(cmd),
        FMRB_TRANSPORT_TIMEOUT_DEFAULT
    );

    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send SET_WINDOW_ORDER to Host: %d", ret);
    }

    fmrb_semaphore_give(g_ctx_lock);
    return FMRB_OK;
}

/**
 * Update window position for drag and drop
 * System/gui_app cannot be moved
 */
fmrb_err_t fmrb_app_update_window_position(uint8_t pid, uint16_t x, uint16_t y) {
    if (pid >= FMRB_MAX_APPS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);

    fmrb_app_task_context_t* ctx = &g_ctx_pool[pid];

    // Check if target app exists and has a window
    if (ctx->state != PROC_STATE_RUNNING && ctx->state != PROC_STATE_SUSPENDED) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_STATE;
    }

    if (ctx->headless) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_PARAM;
    }

    // system_gui cannot be moved
    if (strcmp(ctx->app_name, "system_desktop") == 0) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_PARAM;
    }

    // Update position
    ctx->window_pos_x = x;
    ctx->window_pos_y = y;

    // Send PRESENT command to Host to reflect new position immediately
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PRESENT,
        .canvas_id = ctx->canvas_id,
        .params.present = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .transparent_color = 0xFF  // No transparency
        }
    };

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_GFX,
        .src_pid = PROC_ID_KERNEL,
        .size = sizeof(gfx_cmd_t)
    };
    memcpy(msg.data, &cmd, sizeof(gfx_cmd_t));

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 100);

    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send PRESENT command to Host: %d", ret);
    }

    fmrb_semaphore_give(g_ctx_lock);
    return FMRB_OK;
}

/**
 * Update window size for resize operation
 * System/gui_app cannot be resized
 * Minimum size constraints: 64x64 pixels
 */
fmrb_err_t fmrb_app_update_window_size(uint8_t pid, uint16_t width, uint16_t height) {
    if (pid >= FMRB_MAX_APPS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_semaphore_take(g_ctx_lock, FMRB_TICK_MAX);

    fmrb_app_task_context_t* ctx = &g_ctx_pool[pid];

    // Check if target app exists and has a window
    if (ctx->state != PROC_STATE_RUNNING && ctx->state != PROC_STATE_SUSPENDED) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_STATE;
    }

    if (ctx->headless) {
        fmrb_semaphore_give(g_ctx_lock);
        return FMRB_ERR_INVALID_PARAM;
    }

    // Only resizable windows can be resized
    if (!ctx->resizable) {
        fmrb_semaphore_give(g_ctx_lock);
        FMRB_LOGW(TAG, "Window '%s' is not resizable", ctx->app_name);
        return FMRB_ERR_NOT_SUPPORTED;
    }

    // Apply per-app minimum size (fallback: global 64x64)
    uint16_t min_w = ctx->min_window_width  > 0 ? ctx->min_window_width  : 64;
    uint16_t min_h = ctx->min_window_height > 0 ? ctx->min_window_height : 64;
    if (width  < min_w) width  = min_w;
    if (height < min_h) height = min_h;

    // Update size
    ctx->window_width = width;
    ctx->window_height = height;

    FMRB_LOGD(TAG, "Window '%s' (PID %d) resized to %dx%d",
              ctx->app_name, pid, width, height);

    // Send UPDATE_WINDOW command to Host to update canvas active size
    fmrb_link_graphics_update_window_t update_cmd = {
        .canvas_id = ctx->canvas_id,
        .x = (int32_t)ctx->window_pos_x,
        .y = (int32_t)ctx->window_pos_y,
        .width = (int32_t)width,
        .height = (int32_t)height
    };

    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_UPDATE_WINDOW,
        (const uint8_t*)&update_cmd,
        sizeof(update_cmd),
        FMRB_TRANSPORT_TIMEOUT_DEFAULT
    );

    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send UPDATE_WINDOW command to Host: %d", ret);
    }

    // Send resize event message to app (APP_CONTROL message)
    // Format: msgpack {"cmd": "resize", "width": xxx, "height": yyy}
    fmrb_msg_t resize_msg = {
        .type = FMRB_MSG_TYPE_APP_CONTROL,
        .src_pid = PROC_ID_KERNEL,
        .size = 0
    };

    // Create msgpack data manually
    uint8_t* data = resize_msg.data;
    size_t pos = 0;

    // Map with 3 elements (fixmap | 3 = 0x83)
    data[pos++] = 0x83;

    // Key: "cmd" (fixstr | 3 = 0xA3)
    data[pos++] = 0xA3;
    data[pos++] = 'c'; data[pos++] = 'm'; data[pos++] = 'd';

    // Value: "resize" (fixstr | 6 = 0xA6)
    data[pos++] = 0xA6;
    data[pos++] = 'r'; data[pos++] = 'e'; data[pos++] = 's';
    data[pos++] = 'i'; data[pos++] = 'z'; data[pos++] = 'e';

    // Key: "width" (fixstr | 5 = 0xA5)
    data[pos++] = 0xA5;
    data[pos++] = 'w'; data[pos++] = 'i'; data[pos++] = 'd';
    data[pos++] = 't'; data[pos++] = 'h';

    // Value: width (uint16)
    data[pos++] = 0xCD;  // uint16
    data[pos++] = (width >> 8) & 0xFF;
    data[pos++] = width & 0xFF;

    // Key: "height" (fixstr | 6 = 0xA6)
    data[pos++] = 0xA6;
    data[pos++] = 'h'; data[pos++] = 'e'; data[pos++] = 'i';
    data[pos++] = 'g'; data[pos++] = 'h'; data[pos++] = 't';

    // Value: height (uint16)
    data[pos++] = 0xCD;  // uint16
    data[pos++] = (height >> 8) & 0xFF;
    data[pos++] = height & 0xFF;

    resize_msg.size = pos;

    ret = fmrb_msg_send(pid, &resize_msg, 100);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "Failed to send resize event to app PID %d: %d", pid, ret);
    } else {
        FMRB_LOGD(TAG, "Resize event sent to app PID %d", pid);
    }

    fmrb_semaphore_give(g_ctx_lock);
    return FMRB_OK;
}

