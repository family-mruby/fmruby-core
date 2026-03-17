/**
 * @file runtime.c
 * @brief BASIC runtime and expression evaluator
 */

#include "basic_internal.h"
#include "fmrb_log.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "basic_runtime";

/**
 * Output text to user-defined callback or default output
 */
void basic_output(basic_state_t* state, const char* format, ...) {
    char buffer[BASIC_MAX_LINE_LENGTH];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (state->output_cb) {
        state->output_cb(state->output_user_data, buffer);
    } else {
        FMRB_LOGI(TAG, "%s", buffer);
    }
}

/**
 * Get input from user-defined callback or default input
 */
int basic_input(basic_state_t* state, char* buffer, size_t max_len) {
    if (state->input_cb) {
        return state->input_cb(state->input_user_data, buffer, max_len);
    }
    return -1;  // No input available
}

/**
 * Find variable by name
 */
variable_t* find_variable(basic_state_t* state, const char* name) {
    for (int i = 0; i < state->var_count; i++) {
        if (state->variables[i].used && strcmp(state->variables[i].name, name) == 0) {
            return &state->variables[i];
        }
    }
    return NULL;
}

/**
 * Create new variable or return existing one
 */
variable_t* create_variable(basic_state_t* state, const char* name) {
    variable_t* var = find_variable(state, name);
    if (var) {
        return var;
    }

    if (state->var_count >= BASIC_MAX_VARIABLES) {
        return NULL;
    }

    var = &state->variables[state->var_count++];
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->name[sizeof(var->name) - 1] = '\0';
    var->used = true;
    var->value.type = VAL_NUMBER;
    var->value.num = 0;

    return var;
}

/**
 * Find program line index by line number
 */
int find_line_index(basic_state_t* state, int32_t line_num) {
    for (int i = 0; i < state->line_count; i++) {
        if (state->lines[i].used && state->lines[i].line_num == line_num) {
            return i;
        }
    }
    return -1;
}

/**
 * Parse and evaluate a primary expression (number, string, variable, or parenthesized expression)
 */
static value_t eval_primary(basic_state_t* state) {
    value_t val;
    memset(&val, 0, sizeof(val));

    token_t tok = lexer_next_token();

    if (tok.type == TOK_NUMBER) {
        val.type = VAL_NUMBER;
        val.num = tok.num_val;
        return val;
    }

    if (tok.type == TOK_STRING) {
        val.type = VAL_STRING;
        strncpy(val.str, tok.str_val, sizeof(val.str) - 1);
        return val;
    }

    if (tok.type == TOK_IDENT) {
        variable_t* var = find_variable(state, tok.ident);
        if (var) {
            return var->value;
        }
        // Undefined variable defaults to 0
        val.type = VAL_NUMBER;
        val.num = 0;
        return val;
    }

    if (tok.type == TOK_LPAREN) {
        val = eval_expression(state);
        tok = lexer_next_token();  // Consume ')'
        return val;
    }

    if (tok.type == TOK_MINUS) {
        // Unary minus
        val = eval_primary(state);
        if (val.type == VAL_NUMBER) {
            val.num = -val.num;
        }
        return val;
    }

    // Default to 0
    val.type = VAL_NUMBER;
    val.num = 0;
    return val;
}

/**
 * Parse and evaluate multiplicative expression (* /)
 */
static value_t eval_multiplicative(basic_state_t* state) {
    value_t left = eval_primary(state);

    while (true) {
        token_t tok = lexer_peek_token();
        if (tok.type != TOK_MULT && tok.type != TOK_DIV) {
            break;
        }
        lexer_next_token();  // Consume operator

        value_t right = eval_primary(state);

        // Type checking - both must be numbers
        if (left.type != VAL_NUMBER || right.type != VAL_NUMBER) {
            left.type = VAL_NUMBER;
            left.num = 0;
            return left;
        }

        if (tok.type == TOK_MULT) {
            left.num *= right.num;
        } else {  // TOK_DIV
            if (right.num != 0) {
                left.num /= right.num;
            } else {
                left.num = 0;  // Division by zero returns 0
            }
        }
    }

    return left;
}

/**
 * Parse and evaluate additive expression (+ -)
 */
static value_t eval_additive(basic_state_t* state) {
    value_t left = eval_multiplicative(state);

    while (true) {
        token_t tok = lexer_peek_token();
        if (tok.type != TOK_PLUS && tok.type != TOK_MINUS) {
            break;
        }
        lexer_next_token();  // Consume operator

        value_t right = eval_multiplicative(state);

        // Type checking - both must be numbers
        if (left.type != VAL_NUMBER || right.type != VAL_NUMBER) {
            left.type = VAL_NUMBER;
            left.num = 0;
            return left;
        }

        if (tok.type == TOK_PLUS) {
            left.num += right.num;
        } else {  // TOK_MINUS
            left.num -= right.num;
        }
    }

    return left;
}

/**
 * Parse and evaluate comparison expression (= <> < > <= >=)
 */
static value_t eval_comparison(basic_state_t* state) {
    value_t left = eval_additive(state);

    token_t tok = lexer_peek_token();
    if (tok.type != TOK_EQ && tok.type != TOK_NE &&
        tok.type != TOK_LT && tok.type != TOK_GT &&
        tok.type != TOK_LE && tok.type != TOK_GE) {
        return left;
    }
    lexer_next_token();  // Consume operator

    value_t right = eval_additive(state);

    // Type checking - both must be numbers
    if (left.type != VAL_NUMBER || right.type != VAL_NUMBER) {
        left.type = VAL_NUMBER;
        left.num = 0;
        return left;
    }

    int32_t result = 0;
    switch (tok.type) {
        case TOK_EQ: result = (left.num == right.num); break;
        case TOK_NE: result = (left.num != right.num); break;
        case TOK_LT: result = (left.num < right.num); break;
        case TOK_GT: result = (left.num > right.num); break;
        case TOK_LE: result = (left.num <= right.num); break;
        case TOK_GE: result = (left.num >= right.num); break;
        default: break;
    }

    left.type = VAL_NUMBER;
    left.num = result ? -1 : 0;  // BASIC uses -1 for true, 0 for false
    return left;
}

/**
 * Parse and evaluate complete expression
 */
value_t eval_expression(basic_state_t* state) {
    return eval_comparison(state);
}
