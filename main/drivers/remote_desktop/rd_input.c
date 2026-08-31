// Remote-desktop input bridge (browser -> fmrb_host_send_*).
//
// Follows the same conventions as the local input drivers
// (usb_task / tab5_keyboard): key_code = scancode = HID usage ID,
// modifier = FMRB_KEYMAP_MOD_* mask, mouse buttons use SDL numbering
// (1=left, 2=middle, 3=right), coordinates are absolute in the virtual
// 426x240 display space. The injection entry points are queue-based and
// thread-safe, so calling from the httpd task context is fine.

#include "rd_input.h"

#include "fmrb_log.h"
#include "host_task.h"

static const char *TAG = "rd_input";

#define RD_VIRT_W 426
#define RD_VIRT_H 240

static int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int clamp_x(int v) { return v < 0 ? 0 : (v >= RD_VIRT_W ? RD_VIRT_W - 1 : v); }
static int clamp_y(int v) { return v < 0 ? 0 : (v >= RD_VIRT_H ? RD_VIRT_H - 1 : v); }

fmrb_err_t rd_input_handle(const uint8_t *data, size_t len)
{
    if (!data || len < 1) return FMRB_ERR_INVALID_PARAM;

    switch (data[0]) {
    case RD_INPUT_MSG_MOUSE_MOVE: {
        if (len < 5) return FMRB_ERR_INVALID_PARAM;
        int x = clamp_x(rd_i16(&data[1]));
        int y = clamp_y(rd_i16(&data[3]));
        fmrb_host_send_mouse_move(x, y);
        return FMRB_OK;
    }
    case RD_INPUT_MSG_MOUSE_BUTTON: {
        if (len < 7) return FMRB_ERR_INVALID_PARAM;
        int x = clamp_x(rd_i16(&data[1]));
        int y = clamp_y(rd_i16(&data[3]));
        int btn = data[5];
        int state = data[6] ? 1 : 0;
        if (btn < 1 || btn > 3) return FMRB_ERR_INVALID_PARAM;
        // Keep the cursor in sync with the click position first
        fmrb_host_send_mouse_move(x, y);
        fmrb_host_send_mouse_click(x, y, btn, state);
        return FMRB_OK;
    }
    case RD_INPUT_MSG_MOUSE_WHEEL: {
        if (len < 6) return FMRB_ERR_INVALID_PARAM;
        int x = clamp_x(rd_i16(&data[1]));
        int y = clamp_y(rd_i16(&data[3]));
        int notches = (int8_t)data[5];
        if (notches == 0) return FMRB_OK;
        // No move first, unlike a button: the wheel goes to the focused
        // window rather than the one under the pointer, and moving would
        // drag anything the viewer happens to be holding down.
        fmrb_host_send_mouse_wheel(x, y, notches);
        return FMRB_OK;
    }
    case RD_INPUT_MSG_KEY: {
        if (len < 4) return FMRB_ERR_INVALID_PARAM;
        int state = data[1];
        int scancode = data[2];
        int mod = data[3];
        if (scancode == 0) return FMRB_ERR_INVALID_PARAM;
        if (state) {
            fmrb_host_send_key_down(scancode, scancode, mod);
        } else {
            fmrb_host_send_key_up(scancode, scancode, mod);
        }
        return FMRB_OK;
    }
    case RD_INPUT_MSG_KEYFRAME_REQ:
    case RD_INPUT_MSG_PING:
        return FMRB_OK;  // handled elsewhere / no-op
    default:
        FMRB_LOGW(TAG, "unknown input msg type 0x%02x (len=%u)",
                  data[0], (unsigned)len);
        return FMRB_ERR_INVALID_PARAM;
    }
}
