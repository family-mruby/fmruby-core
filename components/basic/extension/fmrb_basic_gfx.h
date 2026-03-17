/**
 * @file fmrb_basic_gfx.h
 * @brief BASIC graphics extension (placeholder for future implementation)
 *
 * This header defines the interface for graphics commands that will
 * extend the base BASIC interpreter with Family BASIC-style sprite
 * and drawing capabilities.
 */

#pragma once

#include "basic_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register graphics extension commands
 *
 * This function will register custom commands for graphics operations
 * such as SPRITE, DRAW, COLOR, etc.
 *
 * @param state BASIC state
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t basic_register_gfx_extension(basic_state_t* state);

// Future extension points:
// - SPRITE <num>, <x>, <y>, <pattern>, <color>
// - DRAW <x1>, <y1> TO <x2>, <y2>, <color>
// - CIRCLE <x>, <y>, <radius>, <color>
// - PAINT <x>, <y>, <color>
// - COLOR <fg>, <bg>
// - SCREEN <mode>
// - CLS
// - LOCATE <x>, <y>

#ifdef __cplusplus
}
#endif
