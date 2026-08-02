/**
 * @file fmrb_mp.h
 * @brief FMRuby MicroPython integration wrapper
 *
 * Starts, runs and tears down the MicroPython runtime for one guest app,
 * with the GC heap taken from that app's memory pool.
 *
 * MicroPython keeps its entire VM state in globals (mp_state_ctx), so unlike
 * mruby and Lua it cannot be instantiated twice. Callers must therefore take
 * ownership with fmrb_mp_acquire() before fmrb_mp_start(), and give it back
 * with fmrb_mp_close(). A second guest is refused, not queued.
 */

#pragma once

#include <stddef.h>

#include "fmrb_err.h"

// Forward declaration to avoid circular dependency
typedef struct fmrb_app_task_context_s fmrb_app_task_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Size of the MicroPython GC heap, taken in one block from the app pool
 *
 * The app pool is 500KB (FMRB_MEM_POOL_SIZE_USER_APP), so this leaves room for
 * the rest of the app's allocations. Taking it as one block means the pool
 * teardown reclaims it even if the app dies badly.
 */
#ifndef FMRB_MP_HEAP_SIZE
#define FMRB_MP_HEAP_SIZE (256 * 1024)
#endif

/**
 * @brief C stack kept in reserve below the MicroPython stack limit
 *
 * MicroPython checks the limit on entry to each recursion, so only the frames
 * between two checks have to fit in here.
 */
#ifndef FMRB_MP_STACK_RESERVE
#define FMRB_MP_STACK_RESERVE (2048)
#endif

/**
 * @brief Initialize the MicroPython subsystem
 *
 * Called once during system initialization, before any guest can start.
 *
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_mp_init(void);

/**
 * @brief Take ownership of the single MicroPython instance
 *
 * @param ctx Application task context
 * @return FMRB_OK when acquired, FMRB_ERR_BUSY when another app holds it
 */
fmrb_err_t fmrb_mp_acquire(fmrb_app_task_context_t* ctx);

/**
 * @brief Bring up the runtime for the owning app
 *
 * Allocates the GC heap from ctx->mem_handle and initializes the VM. Must be
 * called from the app's own task: the C stack limit is derived from that
 * task's remaining stack.
 *
 * @param ctx Application task context, already passed to fmrb_mp_acquire()
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_mp_start(fmrb_app_task_context_t* ctx);

/**
 * @brief Compile and run a Python source string
 *
 * An uncaught exception is reported as a traceback in the log and turned into
 * an error return; it does not propagate out of this call.
 *
 * @param ctx Application task context that owns the runtime
 * @param src Python source, need not be null-terminated
 * @param len Length of src in bytes
 * @param path Name used in tracebacks (usually the script path)
 * @return FMRB_OK when the script ran to completion, error code otherwise
 */
fmrb_err_t fmrb_mp_exec(fmrb_app_task_context_t* ctx, const char* src, size_t len,
                        const char* path);

/**
 * @brief Tear down the runtime and release ownership
 *
 * Safe to call at any point after fmrb_mp_acquire(), including when
 * fmrb_mp_start() failed part way, and safe to call on a context that never
 * acquired anything.
 *
 * @param ctx Application task context
 */
void fmrb_mp_close(fmrb_app_task_context_t* ctx);

/**
 * @brief Take and release the instance lock, so a caller can be sure no task
 *        is inside an instance-lock critical section right now.
 *
 * For the forced-kill path only, and for the same reason as
 * fmrb_msg_registry_lock_barrier(): a task deleted while holding this mutex
 * would hold it forever, and every later Python app would block in
 * fmrb_mp_acquire() until the next reboot. The windows are a few
 * non-blocking instructions, so the barrier returns immediately in practice.
 */
void fmrb_mp_lock_barrier(void);

#ifdef __cplusplus
}
#endif
