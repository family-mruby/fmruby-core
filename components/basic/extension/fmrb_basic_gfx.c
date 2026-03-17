/**
 * @file fmrb_basic_gfx.c
 * @brief BASIC graphics extension implementation (placeholder)
 *
 * Future implementation will include Family BASIC-style graphics commands.
 */

#include "fmrb_basic_gfx.h"
#include "fmrb_log.h"

static const char *TAG = "basic_gfx";

/**
 * Register graphics extension commands
 */
fmrb_err_t basic_register_gfx_extension(basic_state_t* state) {
    if (!state) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // TODO: Register custom graphics commands here
    // This will be implemented when graphics commands are added

    FMRB_LOGI(TAG, "Graphics extension placeholder registered");
    return FMRB_OK;
}

// Future implementations:
//
// Example extension pattern:
//
// static fmrb_err_t exec_sprite(basic_state_t* state) {
//     // Parse SPRITE parameters
//     value_t sprite_num = eval_expression(state);
//     // ... parse x, y, pattern, color
//     // Call graphics API
//     return FMRB_OK;
// }
//
// To add new commands, you would:
// 1. Add token types to basic_internal.h (e.g., TOK_SPRITE)
// 2. Add keyword to lexer.c keyword table
// 3. Add case in parser.c parse_statement()
// 4. Implement command handler function
