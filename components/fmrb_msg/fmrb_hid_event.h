/**
 * @file fmrb_hid_event.h
 * @brief One decoder for the HID event payloads in fmrb_hid_msg.h.
 *
 * Every language binding turns the same payloads into its own event object,
 * and each used to validate the size and pick the struct apart itself. That is
 * how they drifted: Python never grew the gamepad cases, and both had to carry
 * a comment explaining that mouse motion does not arrive in the shape its
 * struct suggests.
 *
 * A binding now decodes once into fmrb_hid_event_t and only maps that onto its
 * own type - an mruby hash, a Python dict.
 *
 * Deliberately plain: stdint and stdbool only, no fmrb_msg_t. The MicroPython
 * qstr extractor preprocesses fmrb_module.c without the firmware headers, so
 * anything it includes has to stand on its own. Callers pass the payload
 * pointer and length they already hold.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FMRB_HID_EVENT_NONE = 0,
    FMRB_HID_EVENT_KEY_DOWN,
    FMRB_HID_EVENT_KEY_UP,
    FMRB_HID_EVENT_MOUSE_MOVE,
    FMRB_HID_EVENT_MOUSE_DOWN,
    FMRB_HID_EVENT_MOUSE_UP,
    FMRB_HID_EVENT_GAMEPAD_DOWN,
    FMRB_HID_EVENT_GAMEPAD_UP,
    FMRB_HID_EVENT_GAMEPAD_AXIS,
} fmrb_hid_event_type_t;

/**
 * @brief A decoded HID event.
 *
 * Only the fields belonging to the event's type are meaningful; the rest are
 * zero. `button` carries the mouse button for mouse events and the button
 * number for gamepad events, which is how the bindings already present it.
 */
typedef struct {
    fmrb_hid_event_type_t type;
    uint16_t x;          // mouse move / button
    uint16_t y;          // mouse move / button
    uint8_t  button;     // mouse button (1=left, 2=middle, 3=right), or gamepad button number
    uint8_t  keycode;    // key events
    uint8_t  scancode;   // key events
    uint8_t  modifier;   // key events
    char     character;  // key events, 0 when the key does not map to one
    uint8_t  gamepad_id; // gamepad events
    uint8_t  axis;       // gamepad axis events
    int16_t  value;      // gamepad axis value
} fmrb_hid_event_t;

/**
 * @brief Decode one FMRB_MSG_TYPE_HID_EVENT payload.
 *
 * Rejects a payload that is too short for its subtype, which is the check each
 * binding used to repeat.
 *
 * @param data Message payload (msg.data).
 * @param size Payload length in bytes (msg.size).
 * @param out Receives the decoded event; zeroed first.
 * @return true when the payload decoded, false for an unknown subtype or a
 *         short payload (out->type is then FMRB_HID_EVENT_NONE).
 */
bool fmrb_hid_event_decode(const uint8_t *data, uint32_t size,
                           fmrb_hid_event_t *out);

#ifdef __cplusplus
}
#endif
