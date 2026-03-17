/**
 * @file basic_internal.h
 * @brief Internal definitions for BASIC interpreter
 */

#pragma once

#include "fmrb_basic.h"
#include "fmrb_mem.h"
#include <stdint.h>
#include <stdbool.h>

// Maximum program size
#define BASIC_MAX_LINES 1000
#define BASIC_MAX_LINE_LENGTH 256
#define BASIC_MAX_VARIABLES 256
#define BASIC_MAX_FOR_NEST 16
#define BASIC_MAX_GOSUB_NEST 16

// Token types
typedef enum {
    TOK_EOF = 0,
    TOK_NUMBER,      // Numeric literal
    TOK_STRING,      // String literal
    TOK_IDENT,       // Variable name
    TOK_LINENUM,     // Line number

    // Operators
    TOK_PLUS,        // +
    TOK_MINUS,       // -
    TOK_MULT,        // *
    TOK_DIV,         // /
    TOK_EQ,          // =
    TOK_NE,          // <>
    TOK_LT,          // <
    TOK_GT,          // >
    TOK_LE,          // <=
    TOK_GE,          // >=
    TOK_LPAREN,      // (
    TOK_RPAREN,      // )
    TOK_COMMA,       // ,
    TOK_SEMICOLON,   // ;

    // Keywords
    TOK_LET,
    TOK_PRINT,
    TOK_INPUT,
    TOK_IF,
    TOK_THEN,
    TOK_GOTO,
    TOK_GOSUB,
    TOK_RETURN,
    TOK_FOR,
    TOK_TO,
    TOK_STEP,
    TOK_NEXT,
    TOK_REM,
    TOK_END,
} token_type_t;

// Token structure
typedef struct {
    token_type_t type;
    union {
        int32_t num_val;
        char str_val[BASIC_MAX_LINE_LENGTH];
        char ident[32];
    };
} token_t;

// Value types
typedef enum {
    VAL_NUMBER,
    VAL_STRING,
} value_type_t;

// Value structure
typedef struct {
    value_type_t type;
    union {
        int32_t num;
        char str[BASIC_MAX_LINE_LENGTH];
    };
} value_t;

// Variable structure
typedef struct {
    char name[32];
    value_t value;
    bool used;
} variable_t;

// Program line structure
typedef struct {
    int32_t line_num;
    char text[BASIC_MAX_LINE_LENGTH];
    bool used;
} program_line_t;

// FOR loop context
typedef struct {
    char var_name[32];
    int32_t target;
    int32_t step;
    int32_t line_idx;  // Line to return to after NEXT
} for_context_t;

// GOSUB context
typedef struct {
    int32_t return_line_idx;
} gosub_context_t;

// BASIC interpreter state
struct basic_state {
    // Memory management
    fmrb_mem_handle_t mem_handle;

    // Program storage
    program_line_t* lines;
    int line_count;

    // Variables
    variable_t* variables;
    int var_count;

    // Execution state
    int current_line_idx;
    bool running;

    // FOR/NEXT stack
    for_context_t for_stack[BASIC_MAX_FOR_NEST];
    int for_stack_ptr;

    // GOSUB/RETURN stack
    gosub_context_t gosub_stack[BASIC_MAX_GOSUB_NEST];
    int gosub_stack_ptr;

    // I/O callbacks
    basic_output_cb_t output_cb;
    void* output_user_data;
    basic_input_cb_t input_cb;
    void* input_user_data;
};

// Lexer functions
void lexer_init(const char* source);
token_t lexer_next_token(void);
token_t lexer_peek_token(void);

// Parser functions
fmrb_err_t parse_statement(basic_state_t* state, const char* line);

// Runtime functions
fmrb_err_t runtime_execute_line(basic_state_t* state, int line_idx);
value_t eval_expression(basic_state_t* state);
variable_t* find_variable(basic_state_t* state, const char* name);
variable_t* create_variable(basic_state_t* state, const char* name);
int find_line_index(basic_state_t* state, int32_t line_num);

// Utility functions
void basic_output(basic_state_t* state, const char* format, ...);
int basic_input(basic_state_t* state, char* buffer, size_t max_len);
