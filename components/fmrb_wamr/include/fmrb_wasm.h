/**
 * @file fmrb_wasm.h
 * @brief wasm apps (.app.wasm) on WAMR's classic interpreter (doc/wasm_apps W1b)
 *
 * A wasm app exports functions and the app shell (fmrb_app_shell.h) calls
 * them, the same life a Lua app has. The runtime is started by the first
 * wasm app and ended by the last one (decision 4); its small shared pool
 * (module data, instances, operand stacks) is taken from PSRAM by the first
 * wasm app and kept. The app's binary copy and its linear memory come from
 * the app's own memory pool, the same frame a Ruby app takes (decision 5).
 *
 * Built on the P4, the S3 and the Linux builds (FMRB_HAVE_WASM).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"

typedef struct fmrb_app_task_context_s fmrb_app_task_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/** The task stack a wasm app gets when its .app.toml does not say (bytes). */
#define FMRB_WASM_APP_TASK_STACK_SIZE (24 * 1024)

/** @brief Set up the runtime's lock at boot (fmrb_app_init); takes no pool. */
void fmrb_wasm_init(void);

/**
 * @brief Load the .wasm file at path and run it on the app shell, in the
 *        calling app task, to its end.
 *
 * @param err On FMRB_ERR_FAILED, why: the load, a missing export, a trap
 *            (with the function it was in) or the shell's refusal.
 * @return FMRB_OK when the app ended as asked, FMRB_ERR_FAILED otherwise.
 */
fmrb_err_t fmrb_wasm_app_run(fmrb_app_task_context_t *ctx, const char *path,
                             char *err, size_t err_cap);

/**
 * @brief Give back a wasm app's use of the runtime after its task was
 *        deleted by a forced kill (fmrb_wasm_app_run did not get to its
 *        end). Frees nothing of the app; the runtime ends (and its shared
 *        pool is laid out afresh by the next start) when it was the last
 *        wasm app. Does nothing for an app that was not a user.
 */
void fmrb_wasm_app_forget(fmrb_app_task_context_t *ctx);

/** The runtime as a whole (fmrb_wasm_stats). */
typedef struct {
    bool running;           /**< started (a wasm app holds it) */
    uint32_t apps;          /**< wasm apps using it */
    uint32_t modules;       /**< modules loaded and instantiated now */
    uint32_t natives;       /**< the boundary's functions it registers */
    uint32_t pool_total;    /**< the shared pool (0: not taken yet) */
    uint32_t pool_used;     /**< in use now (0 while not running) */
    uint32_t pool_high;     /**< the most ever in use, since boot */
    uint32_t traps;         /**< calls that trapped (or ran too long), since boot */
} fmrb_wasm_stats_t;

/** One wasm app (fmrb_wasm_app_stats). */
typedef struct {
    uint32_t pages;         /**< its linear memory now, in 64 KB pages */
    uint32_t max_pages;     /**< the most it may grow to */
    uint32_t binary_size;   /**< its .wasm, bytes */
    uint32_t hook_us;       /**< the last hook call took (us) */
    uint32_t hook_max_us;   /**< the longest hook call (us) */
    uint32_t calls;         /**< hook calls so far */
} fmrb_wasm_app_stats_t;

/** @brief The runtime's state; always fills out (all zero before the first wasm app). */
void fmrb_wasm_stats(fmrb_wasm_stats_t *out);

/**
 * @brief The state of the wasm app pid.
 * @return false (out zeroed) when pid is not a wasm app with its module loaded.
 */
bool fmrb_wasm_app_stats(int pid, fmrb_wasm_app_stats_t *out);

/** @brief The fmrb_wasm: lines of the periodic dump (fmrb_app_dump_vm_pools). */
void fmrb_wasm_dump_stats(void);

#ifdef __cplusplus
}
#endif
