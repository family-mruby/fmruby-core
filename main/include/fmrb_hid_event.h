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
