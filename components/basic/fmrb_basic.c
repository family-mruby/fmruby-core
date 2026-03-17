/**
 * @file fmrb_basic.c
 * @brief FMRuby BASIC interpreter wrapper implementation
 */

#include "fmrb_basic.h"
#include "basic_internal.h"
#include "fmrb_app.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "fmrb_basic";

/**
 * Initialize FMRuby BASIC subsystem
 */
fmrb_err_t fmrb_basic_init(void) {
    FMRB_LOGI(TAG, "BASIC subsystem initialized");
    return FMRB_OK;
}

/**
 * Create new BASIC interpreter state with per-task memory pool
 */
basic_state_t* fmrb_basic_newstate(fmrb_app_task_context_t* ctx) {
    if (!ctx) {
        FMRB_LOGE(TAG, "Context is NULL");
        return NULL;
    }

    basic_state_t* state = (basic_state_t*)fmrb_malloc(ctx->mem_handle, sizeof(basic_state_t));
    if (!state) {
        FMRB_LOGE(TAG, "Failed to allocate BASIC state for task %s", ctx->app_name);
        return NULL;
    }

    memset(state, 0, sizeof(basic_state_t));
    state->mem_handle = ctx->mem_handle;

    // Allocate program line storage
    state->lines = (program_line_t*)fmrb_malloc(ctx->mem_handle,
                                                 sizeof(program_line_t) * BASIC_MAX_LINES);
    if (!state->lines) {
        FMRB_LOGE(TAG, "Failed to allocate line storage");
        fmrb_free(ctx->mem_handle, state);
        return NULL;
    }
    memset(state->lines, 0, sizeof(program_line_t) * BASIC_MAX_LINES);

    // Allocate variable storage
    state->variables = (variable_t*)fmrb_malloc(ctx->mem_handle,
                                                sizeof(variable_t) * BASIC_MAX_VARIABLES);
    if (!state->variables) {
        FMRB_LOGE(TAG, "Failed to allocate variable storage");
        fmrb_free(ctx->mem_handle, state->lines);
        fmrb_free(ctx->mem_handle, state);
        return NULL;
    }
    memset(state->variables, 0, sizeof(variable_t) * BASIC_MAX_VARIABLES);

    state->line_count = 0;
    state->var_count = 0;
    state->current_line_idx = 0;
    state->running = false;
    state->for_stack_ptr = 0;
    state->gosub_stack_ptr = 0;

    FMRB_LOGI(TAG, "BASIC state created for task %s (pool=%d)",
              ctx->app_name, ctx->mempool_id);
    return state;
}

/**
 * Close BASIC interpreter state and free resources
 */
void fmrb_basic_close(basic_state_t* state) {
    if (state) {
        if (state->lines) {
            fmrb_free(state->mem_handle, state->lines);
        }
        if (state->variables) {
            fmrb_free(state->mem_handle, state->variables);
        }
        fmrb_free(state->mem_handle, state);
        FMRB_LOGI(TAG, "BASIC state closed");
    }
}

/**
 * Compare function for sorting program lines by line number
 */
static int compare_lines(const void* a, const void* b) {
    const program_line_t* line_a = (const program_line_t*)a;
    const program_line_t* line_b = (const program_line_t*)b;

    if (!line_a->used && !line_b->used) return 0;
    if (!line_a->used) return 1;
    if (!line_b->used) return -1;

    return line_a->line_num - line_b->line_num;
}

/**
 * Load BASIC program from string
 */
fmrb_err_t fmrb_basic_load(basic_state_t* state, const char* program) {
    if (!state || !program) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Parse program line by line
    const char* line_start = program;
    char line_buffer[BASIC_MAX_LINE_LENGTH];

    while (*line_start != '\0') {
        // Extract one line
        int i = 0;
        while (*line_start != '\0' && *line_start != '\n' && i < BASIC_MAX_LINE_LENGTH - 1) {
            line_buffer[i++] = *line_start++;
        }
        line_buffer[i] = '\0';

        if (*line_start == '\n') {
            line_start++;
        }

        // Skip empty lines
        if (i == 0) {
            continue;
        }

        // Parse line number
        lexer_init(line_buffer);
        token_t tok = lexer_next_token();

        if (tok.type != TOK_NUMBER) {
            // Direct mode command, skip for now
            continue;
        }

        int32_t line_num = tok.num_val;

        // Find rest of line (after line number)
        const char* rest = line_buffer;
        while (*rest && (*rest == ' ' || *rest == '\t' || (*rest >= '0' && *rest <= '9'))) {
            rest++;
        }

        // Find existing line or create new one
        int line_idx = -1;
        for (int j = 0; j < BASIC_MAX_LINES; j++) {
            if (state->lines[j].used && state->lines[j].line_num == line_num) {
                line_idx = j;
                break;
            }
        }

        if (line_idx < 0) {
            // Find free slot
            for (int j = 0; j < BASIC_MAX_LINES; j++) {
                if (!state->lines[j].used) {
                    line_idx = j;
                    break;
                }
            }
        }

        if (line_idx < 0) {
            FMRB_LOGE(TAG, "Too many program lines");
            return FMRB_ERR_NO_MEMORY;
        }

        // Store line
        state->lines[line_idx].line_num = line_num;
        strncpy(state->lines[line_idx].text, rest, BASIC_MAX_LINE_LENGTH - 1);
        state->lines[line_idx].text[BASIC_MAX_LINE_LENGTH - 1] = '\0';
        state->lines[line_idx].used = true;

        if (line_idx >= state->line_count) {
            state->line_count = line_idx + 1;
        }
    }

    // Sort lines by line number
    qsort(state->lines, BASIC_MAX_LINES, sizeof(program_line_t), compare_lines);

    // Update line count
    state->line_count = 0;
    for (int i = 0; i < BASIC_MAX_LINES; i++) {
        if (state->lines[i].used) {
            state->line_count = i + 1;
        }
    }

    FMRB_LOGI(TAG, "Loaded %d program lines", state->line_count);
    return FMRB_OK;
}

/**
 * Run loaded BASIC program
 */
fmrb_err_t fmrb_basic_run(basic_state_t* state) {
    if (!state) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (state->line_count == 0) {
        basic_output(state, "No program to run\n");
        return FMRB_ERR_INVALID_STATE;
    }

    // Reset execution state
    state->current_line_idx = 0;
    state->running = true;
    state->for_stack_ptr = 0;
    state->gosub_stack_ptr = 0;

    // Execute lines
    while (state->running && state->current_line_idx < state->line_count) {
        if (!state->lines[state->current_line_idx].used) {
            state->current_line_idx++;
            continue;
        }

        fmrb_err_t err = runtime_execute_line(state, state->current_line_idx);
        if (err != FMRB_OK) {
            basic_output(state, "Error at line %d\n",
                        state->lines[state->current_line_idx].line_num);
            state->running = false;
            return err;
        }

        state->current_line_idx++;
    }

    state->running = false;
    return FMRB_OK;
}

/**
 * Execute a single BASIC statement in direct mode
 */
fmrb_err_t fmrb_basic_exec(basic_state_t* state, const char* statement) {
    if (!state || !statement) {
        return FMRB_ERR_INVALID_PARAM;
    }

    return parse_statement(state, statement);
}

/**
 * Clear loaded program
 */
void fmrb_basic_clear(basic_state_t* state) {
    if (state && state->lines) {
        memset(state->lines, 0, sizeof(program_line_t) * BASIC_MAX_LINES);
        state->line_count = 0;
        FMRB_LOGI(TAG, "Program cleared");
    }
}

/**
 * List program lines
 */
fmrb_err_t fmrb_basic_list(basic_state_t* state) {
    if (!state) {
        return FMRB_ERR_INVALID_PARAM;
    }

    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i].used) {
            basic_output(state, "%d %s\n", state->lines[i].line_num, state->lines[i].text);
        }
    }

    return FMRB_OK;
}

/**
 * Set output callback
 */
void fmrb_basic_set_output_cb(basic_state_t* state, basic_output_cb_t callback, void* user_data) {
    if (state) {
        state->output_cb = callback;
        state->output_user_data = user_data;
    }
}

/**
 * Set input callback
 */
void fmrb_basic_set_input_cb(basic_state_t* state, basic_input_cb_t callback, void* user_data) {
    if (state) {
        state->input_cb = callback;
        state->input_user_data = user_data;
    }
}
