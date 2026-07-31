#include <string.h>

#include "py/compile.h"
#include "py/gc.h"
#include "py/mpprint.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "port/micropython_embed.h"

#include "fmrb_mp.h"
#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"

static const char *TAG = "fmrb_mp";

/**
 * The runtime lives in MicroPython's globals, so there is exactly one of it.
 * s_owner is both the owning context and the "in use" flag; s_lock only guards
 * the handover, not the run itself (the owner is the only one running code).
 */
static fmrb_semaphore_t s_lock = NULL;
static fmrb_app_task_context_t *s_owner = NULL;
static void *s_heap = NULL;
static bool s_running = false;

/**
 * Sink that turns MicroPython's print output into log lines. Used for
 * tracebacks, which arrive as several writes and span several lines, so the
 * text is buffered and flushed per newline.
 */
typedef struct {
    char buf[160];
    size_t len;
} log_sink_t;

static void log_sink_flush(log_sink_t *sink) {
    if (sink->len == 0) {
        return;
    }
    sink->buf[sink->len] = '\0';
    FMRB_LOGE(TAG, "%s", sink->buf);
    sink->len = 0;
}

static void log_sink_print_strn(void *data, const char *str, size_t len) {
    log_sink_t *sink = (log_sink_t *)data;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            log_sink_flush(sink);
            continue;
        }
        if (sink->len + 1 >= sizeof(sink->buf)) {
            log_sink_flush(sink);
        }
        sink->buf[sink->len++] = c;
    }
}

fmrb_err_t fmrb_mp_init(void) {
    if (s_lock) {
        return FMRB_OK;
    }
    s_lock = fmrb_semaphore_create_mutex();
    if (!s_lock) {
        FMRB_LOGE(TAG, "Failed to create instance lock");
        return FMRB_ERR_NO_RESOURCE;
    }
    FMRB_LOGI(TAG, "MicroPython subsystem initialized (heap=%u bytes, single instance)",
              (unsigned)FMRB_MP_HEAP_SIZE);
    return FMRB_OK;
}

fmrb_err_t fmrb_mp_acquire(fmrb_app_task_context_t *ctx) {
    if (!ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (!s_lock) {
        FMRB_LOGE(TAG, "[%s] Acquire before fmrb_mp_init", ctx->app_name);
        return FMRB_ERR_INVALID_STATE;
    }
    if (fmrb_semaphore_take(s_lock, FMRB_MAX_DELAY) != FMRB_TRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    fmrb_err_t ret = FMRB_OK;
    if (s_owner) {
        FMRB_LOGE(TAG, "[%s] Python app '%s' is already running; only one at a time",
                  ctx->app_name, s_owner->app_name);
        ret = FMRB_ERR_BUSY;
    } else {
        s_owner = ctx;
    }

    fmrb_semaphore_give(s_lock);
    return ret;
}

/**
 * C stack budget for the VM, measured from what the calling task has left.
 * Returns 0 when there is not enough to bother starting.
 */
static size_t stack_limit_bytes(void) {
    size_t free_bytes =
        (size_t)fmrb_task_get_stack_high_water_mark(NULL) * sizeof(StackType_t);
    if (free_bytes <= FMRB_MP_STACK_RESERVE) {
        return 0;
    }
    return free_bytes - FMRB_MP_STACK_RESERVE;
}

fmrb_err_t fmrb_mp_start(fmrb_app_task_context_t *ctx) {
    if (!ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (s_owner != ctx) {
        // Starting without owning would run a second guest over the shared
        // globals and corrupt the first one.
        FMRB_LOGE(TAG, "[%s] Start without acquire", ctx->app_name);
        return FMRB_ERR_INVALID_STATE;
    }
    if (s_running) {
        FMRB_LOGE(TAG, "[%s] Runtime already started", ctx->app_name);
        return FMRB_ERR_INVALID_STATE;
    }

    size_t stack_limit = stack_limit_bytes();
    if (stack_limit == 0) {
        FMRB_LOGE(TAG, "[%s] Not enough task stack left to start MicroPython",
                  ctx->app_name);
        return FMRB_ERR_NO_RESOURCE;
    }

    s_heap = fmrb_malloc(ctx->mem_handle, FMRB_MP_HEAP_SIZE);
    if (!s_heap) {
        FMRB_LOGE(TAG, "[%s] Failed to allocate %u byte GC heap from pool %d",
                  ctx->app_name, (unsigned)FMRB_MP_HEAP_SIZE, ctx->mempool_id);
        return FMRB_ERR_NO_MEMORY;
    }

    int stack_top;
    mp_embed_init(s_heap, FMRB_MP_HEAP_SIZE, &stack_top);
    // Must follow mp_embed_init immediately: it only sets the stack top, and
    // MICROPY_STACK_CHECK compares against a limit that starts at 0, so until
    // this line every stack check fails. The resulting raise happens before
    // any nlr handler is pushed and lands in nlr_jump_fail's endless loop --
    // the app hangs with no message at all.
    mp_stack_set_limit((mp_uint_t)stack_limit);
    s_running = true;

    gc_info_t info;
    gc_info(&info);
    FMRB_LOGI(TAG, "[%s] MicroPython started: gc total=%u used=%u free=%u, stack limit=%u",
              ctx->app_name, (unsigned)info.total, (unsigned)info.used,
              (unsigned)info.free, (unsigned)stack_limit);
    return FMRB_OK;
}

fmrb_err_t fmrb_mp_exec(fmrb_app_task_context_t *ctx, const char *src, size_t len,
                        const char *path) {
    if (!ctx || !src) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (s_owner != ctx || !s_running) {
        FMRB_LOGE(TAG, "[%s] Exec without a running runtime", ctx->app_name);
        return FMRB_ERR_INVALID_STATE;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        qstr source_name = qstr_from_str(path ? path : "<script>");
        mp_lexer_t *lex = mp_lexer_new_from_str_len(source_name, src, len, 0);
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
        return FMRB_OK;
    } else {
        // Uncaught exception. Report it where the rest of the system reports
        // errors rather than on stdout, then return normally so the task
        // wrapper can clean up.
        log_sink_t sink = { .len = 0 };
        const mp_print_t print = { &sink, log_sink_print_strn };
        FMRB_LOGE(TAG, "[%s] Uncaught exception:", ctx->app_name);
        mp_obj_print_exception(&print, MP_OBJ_FROM_PTR(nlr.ret_val));
        log_sink_flush(&sink);
        return FMRB_ERR_FAILED;
    }
}

void fmrb_mp_close(fmrb_app_task_context_t *ctx) {
    if (!ctx || s_owner != ctx) {
        // Never acquired, or already closed. Both are normal on the error
        // paths that call this unconditionally.
        return;
    }

    if (s_running) {
        mp_embed_deinit();
        s_running = false;
    }
    if (s_heap) {
        fmrb_free(ctx->mem_handle, s_heap);
        s_heap = NULL;
    }

    if (fmrb_semaphore_take(s_lock, FMRB_MAX_DELAY) == FMRB_TRUE) {
        s_owner = NULL;
        fmrb_semaphore_give(s_lock);
    } else {
        // Releasing matters more than the lock: leaving s_owner set would
        // lock out every future Python app.
        s_owner = NULL;
    }

    FMRB_LOGI(TAG, "[%s] MicroPython runtime closed", ctx->app_name);
}

#ifdef FMRB_MP_SELFTEST

// Context stands in for an app task's. Kept at file scope rather than on the
// caller's stack because the boot task's budget is not ours to spend.
static fmrb_app_task_context_t s_selftest_ctx;

void fmrb_mp_selftest(void) {
    static const char script[] = "print('mp:', 1 + 1)\n";

    // Borrow the first user app pool: nothing has spawned yet at this point in
    // boot, and the handle is destroyed again below so the spawner sees the
    // state it expects.
    void *pool = fmrb_get_mempool_ptr(POOL_ID_USER_APP0);
    size_t pool_size = fmrb_get_mempool_size(POOL_ID_USER_APP0);
    if (!pool || pool_size == 0 || fmrb_mem_handle_exist(POOL_ID_USER_APP0)) {
        FMRB_LOGE(TAG, "selftest: user app pool 0 is not available");
        return;
    }

    fmrb_mem_handle_t handle = fmrb_mem_create_handle(pool, pool_size, POOL_ID_USER_APP0);
    if (handle < 0) {
        FMRB_LOGE(TAG, "selftest: failed to create a pool handle");
        return;
    }

    memset(&s_selftest_ctx, 0, sizeof(s_selftest_ctx));
    strncpy(s_selftest_ctx.app_name, "mp_selftest", sizeof(s_selftest_ctx.app_name) - 1);
    s_selftest_ctx.mempool_id = POOL_ID_USER_APP0;
    s_selftest_ctx.mem_handle = handle;

    FMRB_LOGI(TAG, "selftest: starting");
    if (fmrb_mp_acquire(&s_selftest_ctx) == FMRB_OK) {
        if (fmrb_mp_start(&s_selftest_ctx) == FMRB_OK) {
            fmrb_err_t ret = fmrb_mp_exec(&s_selftest_ctx, script, sizeof(script) - 1,
                                          "<selftest>");
            FMRB_LOGI(TAG, "selftest: exec returned %d", (int)ret);
        }
        fmrb_mp_close(&s_selftest_ctx);
    }

    fmrb_mem_destroy_handle(handle);
    FMRB_LOGI(TAG, "selftest: done");
}

#endif // FMRB_MP_SELFTEST
