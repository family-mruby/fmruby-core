/**
 * @file fmrb_basic.h
 * @brief FMRuby BASIC interpreter integration wrapper
 *
 * This header provides FMRuby-specific wrapper for the BASIC interpreter.
 * It integrates BASIC with FMRuby's memory management and RTOS.
 */

#pragma once

#include "fmrb_err.h"
#include <stdint.h>
#include <stddef.h>

// Forward declaration to avoid circular dependency
typedef struct fmrb_app_task_context_s fmrb_app_task_context_t;

// BASIC interpreter state (opaque)
typedef struct basic_state basic_state_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize FMRuby BASIC subsystem
 *
 * This function should be called once during system initialization.
 * It sets up BASIC integration with FMRuby's memory allocator.
 *
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_basic_init(void);

/**
 * @brief Create new BASIC interpreter state with FMRuby memory allocator
 *
 * Creates a new BASIC interpreter state using the memory pool from the given context.
 * This ensures BASIC memory usage is tracked per-task.
 *
 * @param ctx Application task context (contains mempool_id)
 * @return Pointer to new BASIC state, NULL on error
 */
basic_state_t* fmrb_basic_newstate(fmrb_app_task_context_t* ctx);

/**
 * @brief Close BASIC interpreter state and free resources
 *
 * Closes the BASIC state and releases all associated memory.
 *
 * @param state BASIC state to close
 */
void fmrb_basic_close(basic_state_t* state);

/**
 * @brief Load BASIC program from string
 *
 * Parses and loads a BASIC program from the given string.
 * The program can contain multiple lines with line numbers.
 *
 * @param state BASIC state
 * @param program Program text
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_basic_load(basic_state_t* state, const char* program);

/**
 * @brief Run loaded BASIC program
 *
 * Executes the currently loaded BASIC program from the first line.
 *
 * @param state BASIC state
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_basic_run(basic_state_t* state);

/**
 * @brief Execute a single BASIC statement in direct mode
 *
 * Executes a single BASIC statement without a line number.
 * Useful for interactive mode.
 *
 * @param state BASIC state
 * @param statement Statement to execute
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_basic_exec(basic_state_t* state, const char* statement);

/**
 * @brief Clear loaded program
 *
 * Removes all program lines from memory.
 *
 * @param state BASIC state
 */
void fmrb_basic_clear(basic_state_t* state);

/**
 * @brief List program lines
 *
 * Prints all program lines to the output callback.
 *
 * @param state BASIC state
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_basic_list(basic_state_t* state);

/**
 * @brief Output callback function type
 *
 * User-provided callback for PRINT statements and system output.
 *
 * @param user_data User data pointer
 * @param text Text to output
 */
typedef void (*basic_output_cb_t)(void* user_data, const char* text);

/**
 * @brief Input callback function type
 *
 * User-provided callback for INPUT statements.
 *
 * @param user_data User data pointer
 * @param buffer Buffer to store input
 * @param max_len Maximum buffer length
 * @return Number of characters read, or -1 on error
 */
typedef int (*basic_input_cb_t)(void* user_data, char* buffer, size_t max_len);

/**
 * @brief Set output callback
 *
 * Sets the callback function for PRINT statements.
 *
 * @param state BASIC state
 * @param callback Output callback function
 * @param user_data User data passed to callback
 */
void fmrb_basic_set_output_cb(basic_state_t* state, basic_output_cb_t callback, void* user_data);

/**
 * @brief Set input callback
 *
 * Sets the callback function for INPUT statements.
 *
 * @param state BASIC state
 * @param callback Input callback function
 * @param user_data User data passed to callback
 */
void fmrb_basic_set_input_cb(basic_state_t* state, basic_input_cb_t callback, void* user_data);

#ifdef __cplusplus
}
#endif
