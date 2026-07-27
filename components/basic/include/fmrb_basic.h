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
#include <stdbool.h>

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

/**
 * @brief Graphics operations for the fmruby specific BASIC statements
 *
 * CLS / CIRCLE / PRESENT are fmruby extensions, not Family BASIC statements.
 * The console window (extension/fmrb_basic_gfx.c) fills this in.
 */
typedef struct {
    void (*cls)(void* user_data);
    void (*circle)(void* user_data, int16_t x, int16_t y, int16_t r, uint8_t color,
                   bool filled);
    void (*present)(void* user_data);
    void* user_data;
} basic_gfx_ops_t;

/**
 * @brief Connect the graphics extension statements to a drawing target
 *
 * @param state BASIC state
 * @param ops Callback table, copied into the state
 */
void fmrb_basic_set_gfx_ops(basic_state_t* state, const basic_gfx_ops_t* ops);

/**
 * @brief Text screen renderer hooks
 *
 * The interpreter owns the 28x24 shadow buffer and reports every changed cell
 * here; the renderer (extension/fmrb_basic_gfx.c) mirrors it onto a canvas.
 */
typedef struct {
    void (*cell)(void* user_data, uint8_t x, uint8_t y, uint8_t code, uint8_t attr);
    void (*present)(void* user_data);
    void (*palette)(void* user_data, uint8_t attr, uint8_t backdrop, uint8_t c1,
                    uint8_t c2, uint8_t c3);
    void* user_data;
} basic_screen_ops_t;

/**
 * @brief Connect the text screen to a renderer
 *
 * @param state BASIC state
 * @param ops Callback table, copied into the state
 */
void fmrb_basic_set_screen_ops(basic_state_t* state, const basic_screen_ops_t* ops);

#ifdef __cplusplus
}
#endif
