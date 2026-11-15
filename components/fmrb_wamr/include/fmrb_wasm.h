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
 * Built on the P4 and the Linux builds (FMRB_HAVE_WASM); not on the S3.
 */
#pragma once

#include <stddef.h>
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

#ifdef __cplusplus
}
#endif
