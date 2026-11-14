/**
 * @file fmrb_lua.h
 * @brief FMRuby Lua integration wrapper
 *
 * This header provides FMRuby-specific wrapper for Lua 5.4.
 * It integrates Lua with FMRuby's memory management and RTOS.
 */

#pragma once

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "fmrb_err.h"

// Forward declaration to avoid circular dependency
typedef struct fmrb_app_task_context_s fmrb_app_task_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize FMRuby Lua subsystem
 *
 * This function should be called once during system initialization.
 * It sets up Lua integration with FMRuby's memory allocator.
 *
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_lua_init(void);

/**
 * @brief Create new Lua state with FMRuby memory allocator
 *
 * Creates a new Lua VM state using the memory pool from the given context.
 * This ensures Lua memory usage is tracked per-task.
 *
 * @param ctx Application task context (contains mempool_id)
 * @return Pointer to new Lua state, NULL on error
 */
lua_State* fmrb_lua_newstate(fmrb_app_task_context_t* ctx);

/**
 * @brief Close Lua state and free resources
 *
 * Closes the Lua state and releases all associated memory.
 *
 * @param L Lua state to close
 */
void fmrb_lua_close(lua_State* L);

/**
 * @brief Open standard Lua libraries
 *
 * Opens all standard Lua libraries (base, table, string, math, etc.)
 * in the given Lua state.
 *
 * @param L Lua state
 */
void fmrb_lua_openlibs(lua_State* L);

/**
 * @brief Run a loaded Lua program as an app on the app shell
 *
 * The compiled chunk is on the top of the stack (luaL_loadbuffer). The shell
 * (fmrb_app_shell.h) runs its top level, then the functions it exports.
 *
 * @param err Receives why the app ended in error (a Lua error with its
 *            traceback, or the shell's reason it could not start).
 * @return FMRB_OK when the app ended as asked, FMRB_ERR_FAILED on an error
 */
fmrb_err_t fmrb_lua_shell_run(fmrb_app_task_context_t* ctx, lua_State* L,
                              char* err, size_t err_cap);

#ifdef __cplusplus
}
#endif
