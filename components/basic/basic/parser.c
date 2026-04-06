/**
 * @file parser.c
 * @brief BASIC statement parser and executor
 */

#include "basic_internal.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "basic_parser";

/**
 * Execute LET statement: LET var = expr or var = expr
 */
static fmrb_err_t exec_let(basic_state_t* state) {
    token_t tok = lexer_next_token();

    if (tok.type != TOK_IDENT) {
        basic_output(state, "Error: Expected variable name\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    char var_name[32];
    strncpy(var_name, tok.ident, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';

    tok = lexer_next_token();
    if (tok.type != TOK_EQ) {
        basic_output(state, "Error: Expected '='\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    value_t val = eval_expression(state);

    variable_t* var = create_variable(state, var_name);
    if (!var) {
        basic_output(state, "Error: Too many variables\n");
        return FMRB_ERR_NO_MEMORY;
    }

    var->value = val;
    return FMRB_OK;
}

/**
 * Execute PRINT statement: PRINT expr [, expr] [; expr]
 */
static fmrb_err_t exec_print(basic_state_t* state) {
    char output[BASIC_MAX_LINE_LENGTH] = "";
    bool first = true;

    while (true) {
        token_t tok = lexer_peek_token();
        if (tok.type == TOK_EOF) {
            break;
        }

        if (tok.type == TOK_COMMA || tok.type == TOK_SEMICOLON) {
            lexer_next_token();  // Consume separator
            continue;
        }

        value_t val = eval_expression(state);

        if (!first) {
            strcat(output, " ");
        }
        first = false;

        if (val.type == VAL_NUMBER) {
            char num_str[32];
            snprintf(num_str, sizeof(num_str), "%" PRId32, val.num);
            strcat(output, num_str);
        } else if (val.type == VAL_STRING) {
            size_t output_len = strlen(output);
            size_t available = sizeof(output) - output_len - 1;
            size_t to_copy = strlen(val.str);
            if (to_copy > available) to_copy = available;
            memcpy(output + output_len, val.str, to_copy);
            output[output_len + to_copy] = '\0';
        }

        tok = lexer_peek_token();
        if (tok.type != TOK_COMMA && tok.type != TOK_SEMICOLON) {
            break;
        }
    }

    strcat(output, "\n");
    basic_output(state, "%s", output);
    return FMRB_OK;
}

/**
 * Execute INPUT statement: INPUT var
 */
static fmrb_err_t exec_input(basic_state_t* state) {
    token_t tok = lexer_next_token();

    if (tok.type != TOK_IDENT) {
        basic_output(state, "Error: Expected variable name\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    char var_name[32];
    strncpy(var_name, tok.ident, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';

    basic_output(state, "? ");

    char buffer[BASIC_MAX_LINE_LENGTH];
    int len = basic_input(state, buffer, sizeof(buffer));

    if (len < 0) {
        basic_output(state, "Error: Input failed\n");
        return FMRB_ERR_INVALID_STATE;
    }

    variable_t* var = create_variable(state, var_name);
    if (!var) {
        basic_output(state, "Error: Too many variables\n");
        return FMRB_ERR_NO_MEMORY;
    }

    // Try to parse as number
    int32_t num = 0;
    if (sscanf(buffer, "%"SCNd32, &num) == 1) {
        var->value.type = VAL_NUMBER;
        var->value.num = num;
    } else {
        // Store as string
        var->value.type = VAL_STRING;
        strncpy(var->value.str, buffer, sizeof(var->value.str) - 1);
        var->value.str[sizeof(var->value.str) - 1] = '\0';
    }

    return FMRB_OK;
}

/**
 * Execute IF statement: IF expr THEN (line | statement)
 */
static fmrb_err_t exec_if(basic_state_t* state) {
    value_t condition = eval_expression(state);

    token_t tok = lexer_next_token();
    if (tok.type != TOK_THEN) {
        basic_output(state, "Error: Expected THEN\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    // Check condition (0 is false, non-zero is true)
    bool is_true = (condition.type == VAL_NUMBER && condition.num != 0);

    if (!is_true) {
        return FMRB_OK;  // Skip rest of line
    }

    // Check if THEN is followed by a line number or statement
    tok = lexer_peek_token();
    if (tok.type == TOK_NUMBER) {
        // THEN line_number -> GOTO
        lexer_next_token();
        int line_idx = find_line_index(state, tok.num_val);
        if (line_idx >= 0) {
            state->current_line_idx = line_idx - 1;  // -1 because it will be incremented
        } else {
            basic_output(state, "Error: Line %d not found\n", tok.num_val);
            return FMRB_ERR_NOT_FOUND;
        }
    } else {
        // THEN statement -> execute statement
        return parse_statement(state, NULL);  // Parse rest of line
    }

    return FMRB_OK;
}

/**
 * Execute GOTO statement: GOTO line
 */
static fmrb_err_t exec_goto(basic_state_t* state) {
    token_t tok = lexer_next_token();

    if (tok.type != TOK_NUMBER) {
        basic_output(state, "Error: Expected line number\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    int line_idx = find_line_index(state, tok.num_val);
    if (line_idx >= 0) {
        state->current_line_idx = line_idx - 1;  // -1 because it will be incremented
    } else {
        basic_output(state, "Error: Line %d not found\n", tok.num_val);
        return FMRB_ERR_NOT_FOUND;
    }

    return FMRB_OK;
}

/**
 * Execute GOSUB statement: GOSUB line
 */
static fmrb_err_t exec_gosub(basic_state_t* state) {
    token_t tok = lexer_next_token();

    if (tok.type != TOK_NUMBER) {
        basic_output(state, "Error: Expected line number\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    if (state->gosub_stack_ptr >= BASIC_MAX_GOSUB_NEST) {
        basic_output(state, "Error: GOSUB nesting too deep\n");
        return FMRB_ERR_NO_MEMORY;
    }

    // Push return address
    state->gosub_stack[state->gosub_stack_ptr].return_line_idx = state->current_line_idx;
    state->gosub_stack_ptr++;

    int line_idx = find_line_index(state, tok.num_val);
    if (line_idx >= 0) {
        state->current_line_idx = line_idx - 1;  // -1 because it will be incremented
    } else {
        state->gosub_stack_ptr--;  // Pop on error
        basic_output(state, "Error: Line %d not found\n", tok.num_val);
        return FMRB_ERR_NOT_FOUND;
    }

    return FMRB_OK;
}

/**
 * Execute RETURN statement: RETURN
 */
static fmrb_err_t exec_return(basic_state_t* state) {
    if (state->gosub_stack_ptr <= 0) {
        basic_output(state, "Error: RETURN without GOSUB\n");
        return FMRB_ERR_INVALID_STATE;
    }

    state->gosub_stack_ptr--;
    state->current_line_idx = state->gosub_stack[state->gosub_stack_ptr].return_line_idx;

    return FMRB_OK;
}

/**
 * Execute FOR statement: FOR var = start TO end [STEP step]
 */
static fmrb_err_t exec_for(basic_state_t* state) {
    token_t tok = lexer_next_token();

    if (tok.type != TOK_IDENT) {
        basic_output(state, "Error: Expected variable name\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    char var_name[32];
    strncpy(var_name, tok.ident, sizeof(var_name) - 1);
    var_name[sizeof(var_name) - 1] = '\0';

    tok = lexer_next_token();
    if (tok.type != TOK_EQ) {
        basic_output(state, "Error: Expected '='\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    value_t start_val = eval_expression(state);
    if (start_val.type != VAL_NUMBER) {
        basic_output(state, "Error: Expected numeric expression\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    tok = lexer_next_token();
    if (tok.type != TOK_TO) {
        basic_output(state, "Error: Expected TO\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    value_t end_val = eval_expression(state);
    if (end_val.type != VAL_NUMBER) {
        basic_output(state, "Error: Expected numeric expression\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    int32_t step = 1;
    tok = lexer_peek_token();
    if (tok.type == TOK_STEP) {
        lexer_next_token();
        value_t step_val = eval_expression(state);
        if (step_val.type != VAL_NUMBER) {
            basic_output(state, "Error: Expected numeric expression\n");
            return FMRB_ERR_INVALID_PARAM;
        }
        step = step_val.num;
    }

    if (state->for_stack_ptr >= BASIC_MAX_FOR_NEST) {
        basic_output(state, "Error: FOR nesting too deep\n");
        return FMRB_ERR_NO_MEMORY;
    }

    // Initialize loop variable
    variable_t* var = create_variable(state, var_name);
    if (!var) {
        basic_output(state, "Error: Too many variables\n");
        return FMRB_ERR_NO_MEMORY;
    }
    var->value.type = VAL_NUMBER;
    var->value.num = start_val.num;

    // Push FOR context
    for_context_t* ctx = &state->for_stack[state->for_stack_ptr];
    size_t len = strlen(var_name);
    if (len >= sizeof(ctx->var_name)) len = sizeof(ctx->var_name) - 1;
    memcpy(ctx->var_name, var_name, len);
    ctx->var_name[len] = '\0';
    ctx->target = end_val.num;
    ctx->step = step;
    ctx->line_idx = state->current_line_idx;
    state->for_stack_ptr++;

    return FMRB_OK;
}

/**
 * Execute NEXT statement: NEXT [var]
 */
static fmrb_err_t exec_next(basic_state_t* state) {
    if (state->for_stack_ptr <= 0) {
        basic_output(state, "Error: NEXT without FOR\n");
        return FMRB_ERR_INVALID_STATE;
    }

    // Optional variable name
    token_t tok = lexer_peek_token();
    if (tok.type == TOK_IDENT) {
        lexer_next_token();
        // TODO: Verify it matches the FOR variable
    }

    for_context_t* ctx = &state->for_stack[state->for_stack_ptr - 1];
    variable_t* var = find_variable(state, ctx->var_name);

    if (!var) {
        basic_output(state, "Error: FOR variable not found\n");
        return FMRB_ERR_NOT_FOUND;
    }

    // Increment/decrement loop variable
    var->value.num += ctx->step;

    // Check if loop should continue
    bool continue_loop;
    if (ctx->step > 0) {
        continue_loop = (var->value.num <= ctx->target);
    } else {
        continue_loop = (var->value.num >= ctx->target);
    }

    if (continue_loop) {
        // Jump back to FOR line
        state->current_line_idx = ctx->line_idx;
    } else {
        // Exit loop
        state->for_stack_ptr--;
    }

    return FMRB_OK;
}

/**
 * Execute CLS statement: CLS
 * Clears the graphics user area and text buffer.
 */
static fmrb_err_t exec_cls(basic_state_t* state) {
    if (state->gfx_ops.cls) {
        state->gfx_ops.cls(state->gfx_ops.user_data);
    }
    return FMRB_OK;
}

/**
 * Execute CIRCLE statement: CIRCLE x, y, r, color
 */
static fmrb_err_t exec_circle(basic_state_t* state) {
    value_t x_val = eval_expression(state);
    token_t tok = lexer_next_token();
    if (tok.type != TOK_COMMA) {
        basic_output(state, "Error: Expected ','\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    value_t y_val = eval_expression(state);
    tok = lexer_next_token();
    if (tok.type != TOK_COMMA) {
        basic_output(state, "Error: Expected ','\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    value_t r_val = eval_expression(state);
    tok = lexer_next_token();
    if (tok.type != TOK_COMMA) {
        basic_output(state, "Error: Expected ','\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    value_t c_val = eval_expression(state);

    if (x_val.type != VAL_NUMBER || y_val.type != VAL_NUMBER ||
        r_val.type != VAL_NUMBER || c_val.type != VAL_NUMBER) {
        basic_output(state, "Error: CIRCLE requires numeric arguments\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    if (state->gfx_ops.circle) {
        state->gfx_ops.circle(state->gfx_ops.user_data,
                              (int16_t)x_val.num, (int16_t)y_val.num,
                              (int16_t)r_val.num, (uint8_t)c_val.num, true);
    }
    return FMRB_OK;
}

/**
 * Execute WAIT statement: WAIT ms
 * Delays execution for the specified milliseconds.
 */
static fmrb_err_t exec_wait(basic_state_t* state) {
    value_t ms_val = eval_expression(state);
    if (ms_val.type != VAL_NUMBER) {
        basic_output(state, "Error: WAIT requires numeric argument\n");
        return FMRB_ERR_INVALID_PARAM;
    }

    if (ms_val.num > 0) {
        fmrb_task_delay_ms(ms_val.num);
    }
    return FMRB_OK;
}

/**
 * Execute PRESENT statement: PRESENT
 * Flushes the canvas to the screen.
 */
static fmrb_err_t exec_present(basic_state_t* state) {
    if (state->gfx_ops.present) {
        state->gfx_ops.present(state->gfx_ops.user_data);
    }
    return FMRB_OK;
}

/**
 * Execute REM statement: REM comment (do nothing)
 */
static fmrb_err_t exec_rem(basic_state_t* state) {
    // Skip to end of line
    return FMRB_OK;
}

/**
 * Execute END statement: END
 */
static fmrb_err_t exec_end(basic_state_t* state) {
    state->running = false;
    return FMRB_OK;
}

/**
 * Parse and execute a statement
 */
fmrb_err_t parse_statement(basic_state_t* state, const char* line) {
    // If line is provided, initialize lexer
    if (line) {
        lexer_init(line);
    }

    token_t tok = lexer_peek_token();

    // Empty line
    if (tok.type == TOK_EOF) {
        return FMRB_OK;
    }

    // Check for statement keyword
    switch (tok.type) {
        case TOK_LET:
            lexer_next_token();
            return exec_let(state);

        case TOK_IDENT:
            // Assignment without LET
            return exec_let(state);

        case TOK_PRINT:
            lexer_next_token();
            return exec_print(state);

        case TOK_INPUT:
            lexer_next_token();
            return exec_input(state);

        case TOK_IF:
            lexer_next_token();
            return exec_if(state);

        case TOK_GOTO:
            lexer_next_token();
            return exec_goto(state);

        case TOK_GOSUB:
            lexer_next_token();
            return exec_gosub(state);

        case TOK_RETURN:
            lexer_next_token();
            return exec_return(state);

        case TOK_FOR:
            lexer_next_token();
            return exec_for(state);

        case TOK_NEXT:
            lexer_next_token();
            return exec_next(state);

        case TOK_REM:
            return exec_rem(state);

        case TOK_END:
            lexer_next_token();
            return exec_end(state);

        case TOK_CLS:
            lexer_next_token();
            return exec_cls(state);

        case TOK_CIRCLE:
            lexer_next_token();
            return exec_circle(state);

        case TOK_WAIT:
            lexer_next_token();
            return exec_wait(state);

        case TOK_PRESENT:
            lexer_next_token();
            return exec_present(state);

        default:
            basic_output(state, "Error: Unknown statement\n");
            return FMRB_ERR_INVALID_PARAM;
    }
}

/**
 * Execute a single program line
 */
fmrb_err_t runtime_execute_line(basic_state_t* state, int line_idx) {
    if (line_idx < 0 || line_idx >= state->line_count) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (!state->lines[line_idx].used) {
        return FMRB_ERR_INVALID_PARAM;
    }

    return parse_statement(state, state->lines[line_idx].text);
}
