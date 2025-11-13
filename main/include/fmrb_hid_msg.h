#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HID event message definitions for inter-task communication
 */

// HID event subtypes
typedef enum {
    HID_MSG_KEY_DOWN = 1,
    HID_MSG_KEY_UP = 2,
    HID_MSG_MOUSE_MOVE = 3,
    HID_MSG_MOUSE_BUTTON_DOWN = 4,
    HID_MSG_MOUSE_BUTTON_UP = 5,
} hid_msg_subtype_t;

// Keyboard event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_KEY_DOWN or HID_MSG_KEY_UP
    uint8_t keycode;       // SDL keycode
    uint8_t scancode;      // SDL scancode
    uint8_t modifier;      // Modifier keys
} __attribute__((packed)) fmrb_hid_key_event_t;

// Mouse button event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_MOUSE_BUTTON_DOWN or UP
    uint8_t button;        // 1=left, 2=middle, 3=right
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) fmrb_hid_mouse_button_event_t;

// Mouse motion event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_MOUSE_MOVE
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) fmrb_hid_mouse_motion_event_t;

#ifdef __cplusplus
}
#endif
