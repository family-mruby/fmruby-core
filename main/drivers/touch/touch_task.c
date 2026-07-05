// Tab5 GT911 touch input driver (trackpad mode).
// Touch movement is relative: finger displacement is added to the current
// cursor position, like a laptop trackpad or remote desktop touchpad.
//
// Gestures:
//   move          finger moves right away -> cursor movement only,
//                 no button events (does not grab whatever is under
//                 the cursor)
//   tap           quick stationary touch -> click (button down + up)
//                 at the cursor position on release
//   hold + move   finger stays put for TOUCH_HOLD_MS -> button down at
//                 the cursor position; subsequent movement drags,
//                 release sends button up
//
// Button down must not fire on plain touch: the cursor stays where a
// drag ended (e.g. on a window title bar), so an immediate down would
// re-grab the window on the next touch and make it impossible to move
// the cursor away.

#include "touch_task.h"
#include "display_p4_task.h"
#include "host/host_task.h"
#include "fmrb_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "touch";

#define TOUCH_POLL_MS        33   // ~30 Hz
#define TOUCH_READY_POLL_MS  200  // Poll for display ready at startup
#define TOUCH_TASK_STACK     4096
#define TOUCH_TASK_PRIORITY  5

// Virtual display bounds for cursor clamping
#define TOUCH_VIRTUAL_W   426
#define TOUCH_VIRTUAL_H   240

// Panel-space pixel to virtual-pixel ratio (3x scale)
#define TOUCH_SCALE       3

// Hold this long without moving to press the button (drag start).
// Taps must release within this window; 150 ms keeps drags snappy
// while normal taps (~100 ms) still register as clicks.
#define TOUCH_HOLD_MS        150
// Panel-space displacement from the touch-down point that switches a
// pending touch into cursor-move mode (jitter tolerance)
#define TOUCH_MOVE_THRES     5

typedef enum {
    TOUCH_STATE_IDLE,
    TOUCH_STATE_PENDING,  // touched; tap / hold-drag / move not decided yet
    TOUCH_STATE_MOVE,     // cursor movement only, button not pressed
    TOUCH_STATE_DRAG,     // button held down (press-and-hold), dragging
} touch_state_t;

static touch_state_t g_state = TOUCH_STATE_IDLE;

// Current cursor position in virtual display coordinates
static int g_cursor_x = TOUCH_VIRTUAL_W / 2;
static int g_cursor_y = TOUCH_VIRTUAL_H / 2;

// Touch anchor: panel-space coordinates the movement delta is taken from
static int16_t g_anchor_tx = 0;
static int16_t g_anchor_ty = 0;

// Touch-down point and time, for tap / hold detection
static int16_t  g_down_tx = 0;
static int16_t  g_down_ty = 0;
static uint32_t g_down_ms = 0;

static uint32_t now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void clamp_cursor(void) {
    if (g_cursor_x < 0) g_cursor_x = 0;
    if (g_cursor_y < 0) g_cursor_y = 0;
    if (g_cursor_x >= TOUCH_VIRTUAL_W) g_cursor_x = TOUCH_VIRTUAL_W - 1;
    if (g_cursor_y >= TOUCH_VIRTUAL_H) g_cursor_y = TOUCH_VIRTUAL_H - 1;
}

static void touch_task(void *arg) {
    (void)arg;

    // Wait for display_p4 LGFX init to complete
    while (!display_p4_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_READY_POLL_MS));
    }
    FMRB_LOGI(TAG, "Touch task started (trackpad mode, poll=%dms)", TOUCH_POLL_MS);

    uint32_t poll_count = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));

        // Headphone jack polling shares this task so its lgfx-level I2C
        // access stays serialized with the GT911 reads (every ~165 ms)
        if ((poll_count++ % 5) == 0) {
            display_p4_poll_headphone();
        }

        int16_t tx, ty;
        int count = display_p4_get_touch(&tx, &ty);
        uint32_t now = now_ms();

        if (count > 0) {
            if (g_state == TOUCH_STATE_IDLE) {
                // Touch down: record anchor and tap origin, decide later
                g_anchor_tx = tx;
                g_anchor_ty = ty;
                g_down_tx = tx;
                g_down_ty = ty;
                g_down_ms = now;
                g_state = TOUCH_STATE_PENDING;
            } else if (g_state == TOUCH_STATE_PENDING) {
                int mdx = (int)tx - (int)g_down_tx;
                int mdy = (int)ty - (int)g_down_ty;
                if (mdx < 0) mdx = -mdx;
                if (mdy < 0) mdy = -mdy;
                if (mdx > TOUCH_MOVE_THRES || mdy > TOUCH_MOVE_THRES) {
                    // Finger moved before the hold expired: cursor move only
                    g_state = TOUCH_STATE_MOVE;
                } else if ((uint32_t)(now - g_down_ms) >= TOUCH_HOLD_MS) {
                    // Press-and-hold: press the button at the cursor position
                    fmrb_host_send_mouse_move(g_cursor_x, g_cursor_y);
                    fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 1, 1);
                    g_state = TOUCH_STATE_DRAG;
                }
            }

            if (g_state == TOUCH_STATE_MOVE || g_state == TOUCH_STATE_DRAG) {
                // Relative movement: delta from anchor in panel space,
                // converted to virtual pixels
                int dx = ((int)tx - (int)g_anchor_tx) / TOUCH_SCALE;
                int dy = ((int)ty - (int)g_anchor_ty) / TOUCH_SCALE;
                if (dx != 0 || dy != 0) {
                    g_cursor_x += dx;
                    g_cursor_y += dy;
                    clamp_cursor();
                    fmrb_host_send_mouse_move(g_cursor_x, g_cursor_y);
                    // Update anchor to current position for continuous tracking
                    g_anchor_tx = tx;
                    g_anchor_ty = ty;
                }
            }
        } else {
            switch (g_state) {
            case TOUCH_STATE_PENDING:
                // Tap: quick stationary touch -> click at the cursor
                // (a longer hold would already have moved to DRAG)
                fmrb_host_send_mouse_move(g_cursor_x, g_cursor_y);
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 1, 1);
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 1, 0);
                break;
            case TOUCH_STATE_DRAG:
                // Release the held button
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 1, 0);
                break;
            default:
                break;
            }
            g_state = TOUCH_STATE_IDLE;
        }
    }
}

fmrb_err_t touch_task_init(void) {
    BaseType_t ok = xTaskCreatePinnedToCore(
        touch_task, "touch", TOUCH_TASK_STACK, NULL,
        TOUCH_TASK_PRIORITY, NULL, 1);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create touch task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_err_t touch_task_deinit(void) {
    return FMRB_OK;
}
