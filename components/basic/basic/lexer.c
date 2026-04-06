/**
 * @file lexer.c
 * @brief Lexical analyzer for BASIC
 */

#include "basic_internal.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// Lexer state
static const char* lex_source = NULL;
static int lex_pos = 0;
static token_t lex_peek_tok;
static bool lex_peek_valid = false;

// Keyword table
typedef struct {
    const char* keyword;
    token_type_t type;
} keyword_entry_t;

static const keyword_entry_t keywords[] = {
    {"LET", TOK_LET},
    {"PRINT", TOK_PRINT},
    {"INPUT", TOK_INPUT},
    {"IF", TOK_IF},
    {"THEN", TOK_THEN},
    {"GOTO", TOK_GOTO},
    {"GOSUB", TOK_GOSUB},
    {"RETURN", TOK_RETURN},
    {"FOR", TOK_FOR},
    {"TO", TOK_TO},
    {"STEP", TOK_STEP},
    {"NEXT", TOK_NEXT},
    {"REM", TOK_REM},
    {"END", TOK_END},
    {"CLS", TOK_CLS},
    {"CIRCLE", TOK_CIRCLE},
    {"WAIT", TOK_WAIT},
    {"PRESENT", TOK_PRESENT},
    {NULL, TOK_EOF}
};

/**
 * Initialize lexer with source text
 */
void lexer_init(const char* source) {
    lex_source = source;
    lex_pos = 0;
    lex_peek_valid = false;
}

/**
 * Skip whitespace characters
 */
static void skip_whitespace(void) {
    while (lex_source[lex_pos] == ' ' || lex_source[lex_pos] == '\t') {
        lex_pos++;
    }
}

/**
 * Check if character is valid in identifier
 */
static bool is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '$';
}

/**
 * Look up keyword
 */
static token_type_t lookup_keyword(const char* str) {
    for (int i = 0; keywords[i].keyword != NULL; i++) {
        if (strcasecmp(str, keywords[i].keyword) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENT;
}

/**
 * Get next token from source
 */
token_t lexer_next_token(void) {
    token_t tok;
    memset(&tok, 0, sizeof(tok));

    // Return peeked token if available
    if (lex_peek_valid) {
        lex_peek_valid = false;
        return lex_peek_tok;
    }

    if (!lex_source) {
        tok.type = TOK_EOF;
        return tok;
    }

    skip_whitespace();

    // End of input
    if (lex_source[lex_pos] == '\0' || lex_source[lex_pos] == '\n') {
        tok.type = TOK_EOF;
        return tok;
    }

    char c = lex_source[lex_pos];

    // Number
    if (isdigit(c)) {
        int32_t num = 0;
        while (isdigit((unsigned char)lex_source[lex_pos])) {
            num = num * 10 + (lex_source[lex_pos] - '0');
            lex_pos++;
        }
        tok.type = TOK_NUMBER;
        tok.num_val = num;
        return tok;
    }

    // String literal
    if (c == '"') {
        lex_pos++;  // Skip opening quote
        int i = 0;
        while (lex_source[lex_pos] != '"' && lex_source[lex_pos] != '\0' &&
               lex_source[lex_pos] != '\n' && i < BASIC_MAX_LINE_LENGTH - 1) {
            tok.str_val[i++] = lex_source[lex_pos++];
        }
        tok.str_val[i] = '\0';
        if (lex_source[lex_pos] == '"') {
            lex_pos++;  // Skip closing quote
        }
        tok.type = TOK_STRING;
        return tok;
    }

    // Identifier or keyword
    if (isalpha((unsigned char)c)) {
        int i = 0;
        while (is_ident_char(lex_source[lex_pos]) && i < 31) {
            tok.ident[i++] = toupper((unsigned char)lex_source[lex_pos++]);
        }
        tok.ident[i] = '\0';

        // Check if it's a keyword
        tok.type = lookup_keyword(tok.ident);
        return tok;
    }

    // Operators and punctuation
    lex_pos++;
    switch (c) {
        case '+': tok.type = TOK_PLUS; break;
        case '-': tok.type = TOK_MINUS; break;
        case '*': tok.type = TOK_MULT; break;
        case '/': tok.type = TOK_DIV; break;
        case '(': tok.type = TOK_LPAREN; break;
        case ')': tok.type = TOK_RPAREN; break;
        case ',': tok.type = TOK_COMMA; break;
        case ';': tok.type = TOK_SEMICOLON; break;
        case '=': tok.type = TOK_EQ; break;
        case '<':
            if (lex_source[lex_pos] == '=') {
                lex_pos++;
                tok.type = TOK_LE;
            } else if (lex_source[lex_pos] == '>') {
                lex_pos++;
                tok.type = TOK_NE;
            } else {
                tok.type = TOK_LT;
            }
            break;
        case '>':
            if (lex_source[lex_pos] == '=') {
                lex_pos++;
                tok.type = TOK_GE;
            } else {
                tok.type = TOK_GT;
            }
            break;
        default:
            tok.type = TOK_EOF;
            break;
    }

    return tok;
}

/**
 * Peek at next token without consuming it
 */
token_t lexer_peek_token(void) {
    if (!lex_peek_valid) {
        lex_peek_tok = lexer_next_token();
        lex_peek_valid = true;
    }
    return lex_peek_tok;
}
