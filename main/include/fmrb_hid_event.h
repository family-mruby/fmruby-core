/**
 * @file fmrb_hid_event.h
 * @brief HID (Human Interface Device) event definitions for keyboard and mouse
 */

#ifndef FMRB_HID_EVENT_H
#define FMRB_HID_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// HID Event Types
#define HID_EVENT_KEY_DOWN      0x01
#define HID_EVENT_KEY_UP        0x02
#define HID_EVENT_MOUSE_BUTTON  0x10
#define HID_EVENT_MOUSE_MOTION  0x11
#define HID_EVENT_MOUSE_WHEEL   0x12

/**
 * @brief Keyboard event structure
 */
typedef struct {
    uint8_t scancode;   // SDL scancode or HID usage ID
    uint8_t keycode;    // SDL keycode (lower 8 bits)
    uint8_t modifier;   // Modifier keys (Shift, Ctrl, Alt, etc.)
} __attribute__((packed)) hid_keyboard_event_t;

/**
 * @brief Mouse button event structure
 */
typedef struct {
    uint8_t button;     // Button number (1=left, 2=middle, 3=right, etc.)
    uint8_t state;      // 0=released, 1=pressed
    uint16_t x;         // X coordinate
    uint16_t y;         // Y coordinate
} __attribute__((packed)) hid_mouse_button_event_t;

/**
 * @brief Mouse motion event structure
 */
typedef struct {
    uint16_t x;         // X coordinate
    uint16_t y;         // Y coordinate
} __attribute__((packed)) hid_mouse_motion_event_t;

/**
 * @brief Mouse wheel event structure
 *
 * A new packet type rather than a field on motion: the header carries a
 * length, so a receiver that does not know 0x12 skips it and everything else
 * keeps working. That is what lets one side be updated before the other.
 */
typedef struct {
    int8_t delta;       // Wheel notches, positive = away from the user
    uint16_t x;         // X coordinate at the time of the wheel
    uint16_t y;         // Y coordinate
} __attribute__((packed)) hid_mouse_wheel_event_t;

/**
 * @brief HID packet header (simple protocol, no reliability)
 */
typedef struct {
    uint8_t type;       // HID_EVENT_*
    uint16_t data_len;  // Length of following data
} __attribute__((packed)) hid_packet_header_t;

#ifdef __cplusplus
}
#endif

#endif // FMRB_HID_EVENT_H
