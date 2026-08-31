#include "fmrb_hid_event.h"

#include <string.h>

#include "fmrb_hid_msg.h"

bool fmrb_hid_event_decode(const uint8_t *data, uint32_t size,
                           fmrb_hid_event_t *out)
{
    if (!data || !out || size < 1) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    switch (data[0]) {
        case HID_MSG_KEY_DOWN:
        case HID_MSG_KEY_UP: {
            if (size < sizeof(fmrb_hid_key_event_t)) {
                return false;
            }
            const fmrb_hid_key_event_t *e = (const fmrb_hid_key_event_t *)data;
            out->type = (e->subtype == HID_MSG_KEY_DOWN) ? FMRB_HID_EVENT_KEY_DOWN
                                                         : FMRB_HID_EVENT_KEY_UP;
            out->keycode = e->keycode;
            out->scancode = e->scancode;
            out->modifier = e->modifier;
            out->character = e->character;
            return true;
        }

        // Motion shares the button event's layout, with button left at 0 (see
        // the sender in host_task.c). Both are decoded the same way.
        case HID_MSG_MOUSE_MOVE:
        case HID_MSG_MOUSE_BUTTON_DOWN:
        case HID_MSG_MOUSE_BUTTON_UP: {
            if (size < sizeof(fmrb_hid_mouse_button_event_t)) {
                return false;
            }
            const fmrb_hid_mouse_button_event_t *e =
                (const fmrb_hid_mouse_button_event_t *)data;
            if (e->subtype == HID_MSG_MOUSE_MOVE) {
                out->type = FMRB_HID_EVENT_MOUSE_MOVE;
            } else {
                out->type = (e->subtype == HID_MSG_MOUSE_BUTTON_DOWN)
                                ? FMRB_HID_EVENT_MOUSE_DOWN
                                : FMRB_HID_EVENT_MOUSE_UP;
                out->button = e->button;
            }
            out->x = e->x;
            out->y = e->y;
            return true;
        }

        case HID_MSG_MOUSE_WHEEL: {
            if (size < sizeof(fmrb_hid_mouse_wheel_event_t)) {
                return false;
            }
            const fmrb_hid_mouse_wheel_event_t *e =
                (const fmrb_hid_mouse_wheel_event_t *)data;
            out->type = FMRB_HID_EVENT_MOUSE_WHEEL;
            out->wheel = e->delta;
            out->x = e->x;
            out->y = e->y;
            return true;
        }

        case HID_MSG_GAMEPAD_BUTTON_DOWN:
        case HID_MSG_GAMEPAD_BUTTON_UP: {
            if (size < sizeof(fmrb_hid_gamepad_button_event_t)) {
                return false;
            }
            const fmrb_hid_gamepad_button_event_t *e =
                (const fmrb_hid_gamepad_button_event_t *)data;
            out->type = (e->subtype == HID_MSG_GAMEPAD_BUTTON_DOWN)
                            ? FMRB_HID_EVENT_GAMEPAD_DOWN
                            : FMRB_HID_EVENT_GAMEPAD_UP;
            out->gamepad_id = e->gamepad_id;
            out->button = e->button_num;
            return true;
        }

        case HID_MSG_GAMEPAD_AXIS: {
            if (size < sizeof(fmrb_hid_gamepad_axis_event_t)) {
                return false;
            }
            const fmrb_hid_gamepad_axis_event_t *e =
                (const fmrb_hid_gamepad_axis_event_t *)data;
            out->type = FMRB_HID_EVENT_GAMEPAD_AXIS;
            out->gamepad_id = e->gamepad_id;
            out->axis = e->axis_num;
            out->value = e->value;
            return true;
        }

        case HID_MSG_KANA_MODE: {
            if (size < sizeof(fmrb_hid_kana_mode_event_t)) {
                return false;
            }
            const fmrb_hid_kana_mode_event_t *e =
                (const fmrb_hid_kana_mode_event_t *)data;
            out->type = FMRB_HID_EVENT_KANA_MODE;
            out->kana_mode = e->mode;
            return true;
        }

        default:
            return false;
    }
}
