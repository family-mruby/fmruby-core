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

#ifdef __cplusplus
}
#endif
