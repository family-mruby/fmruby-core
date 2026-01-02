#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Keyboard layout types
 */
typedef enum {
    FMRB_KEYMAP_LAYOUT_US = 0,  // US keyboard layout
    FMRB_KEYMAP_LAYOUT_JP = 1,  // Japanese keyboard layout
} fmrb_keymap_layout_t;

/**
 * @brief Modifier key masks
 */
#define FMRB_KEYMAP_MOD_LSHIFT  0x01
#define FMRB_KEYMAP_MOD_RSHIFT  0x02
#define FMRB_KEYMAP_MOD_LCTRL   0x04
#define FMRB_KEYMAP_MOD_RCTRL   0x08
#define FMRB_KEYMAP_MOD_LALT    0x10
#define FMRB_KEYMAP_MOD_RALT    0x20

/**
 * @brief Convert scancode to character
 *
 * @param scancode USB HID scancode
 * @param modifier Modifier key state (bitmap of FMRB_KEYMAP_MOD_*)
 * @param layout Keyboard layout
 * @return Converted character (0 if not convertible)
 */
char fmrb_keymap_scancode_to_char(uint8_t scancode, uint8_t modifier, fmrb_keymap_layout_t layout);

/**
 * @brief Set keyboard layout
 *
 * @param layout Keyboard layout to use
 */
void fmrb_keymap_set_layout(fmrb_keymap_layout_t layout);

/**
 * @brief Get current keyboard layout
 *
 * @return Current keyboard layout
 */
fmrb_keymap_layout_t fmrb_keymap_get_layout(void);

#ifdef __cplusplus
}
#endif
