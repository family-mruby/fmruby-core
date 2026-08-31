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
    HID_MSG_GAMEPAD_BUTTON_DOWN = 6,
    HID_MSG_GAMEPAD_BUTTON_UP = 7,
    HID_MSG_GAMEPAD_AXIS = 8,
    HID_MSG_KANA_MODE = 9,
    HID_MSG_MOUSE_WHEEL = 10,
} hid_msg_subtype_t;

// Keyboard event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_KEY_DOWN or HID_MSG_KEY_UP
    uint8_t keycode;       // SDL keycode
    uint8_t scancode;      // SDL scancode
    uint8_t modifier;      // Modifier keys
    char character;        // Converted character (0 if not convertible)
} __attribute__((packed)) fmrb_hid_key_event_t;

// Mouse button event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_MOUSE_BUTTON_DOWN or UP
    uint8_t button;        // 1=left, 2=middle, 3=right
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) fmrb_hid_mouse_button_event_t;

// Mouse motion has no payload struct of its own: the sender in host_task.c
// writes the button event layout with button left at 0, so
// fmrb_hid_mouse_button_event_t is what a HID_MSG_MOUSE_MOVE payload is.
// (There used to be a 5-byte fmrb_hid_mouse_motion_event_t here that did not
// match the wire, and every decoder carried a comment warning about it.)

// Mouse wheel event payload. delta is in notches, the unit the wheel itself
// reports: one click of the wheel is 1, away from the user is positive (which
// scrolls a view up, towards the start). How many lines a notch means is the
// reader's business -- system_conf.toml's wheel_lines, as FmrbConst::WHEEL_LINES.
// x and y ride along so an app can tell where the pointer was, even though the
// event is delivered to the focused window rather than the one under it.
typedef struct {
    uint8_t subtype;       // HID_MSG_MOUSE_WHEEL
    int8_t delta;          // notches, positive = away from the user
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) fmrb_hid_mouse_wheel_event_t;

// Gamepad button event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_GAMEPAD_BUTTON_DOWN or UP
    uint8_t gamepad_id;    // Gamepad ID (0-1)
    uint8_t button_num;    // Button number (0-15)
} __attribute__((packed)) fmrb_hid_gamepad_button_event_t;

// Kana input mode change (JP layout only). Sent to the HID target when the
// user toggles kana input so an app can show which mode it is in; apps that
// do not know the subtype ignore it.
typedef struct {
    uint8_t subtype;       // HID_MSG_KANA_MODE
    uint8_t mode;          // 0=off (ASCII), 1=hiragana, 2=katakana
} __attribute__((packed)) fmrb_hid_kana_mode_event_t;

// Gamepad axis event payload
typedef struct {
    uint8_t subtype;       // HID_MSG_GAMEPAD_AXIS
    uint8_t gamepad_id;    // Gamepad ID (0-1)
    uint8_t axis_num;      // Axis number (0-5)
    int16_t value;         // Axis value
} __attribute__((packed)) fmrb_hid_gamepad_axis_event_t;

#ifdef __cplusplus
}
#endif
