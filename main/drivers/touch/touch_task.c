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
//   two-finger tap  a second finger lands before any button action ->
//                 right click (button 3 down + up) at the cursor position
//                 on release. Long-press cannot mean right click here
//                 because hold already starts a drag.
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
#include "fmrb_task_config.h"
#include "fmrb_attr.h"

static const char *TAG = "touch";

#define TOUCH_POLL_MS        33   // ~30 Hz
#define TOUCH_READY_POLL_MS  200  // Poll for display ready at startup

// Virtual display bounds before the screen size is known (the base screen)
#define TOUCH_VIRTUAL_W   426
#define TOUCH_VIRTUAL_H   240

// The panel's short side, which the virtual screen is scaled up to: panel
// pixels per virtual pixel = TOUCH_PANEL_SHORT / virtual height (3 on the
// 426x240 base screen, 2 on the 640x360 high-resolution one). Dividing the
// finger's movement by that keeps the cursor under the same physical distance
// in both modes.
#define TOUCH_PANEL_SHORT 720

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
    TOUCH_STATE_TWO,      // two fingers down; right click on release
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

// Virtual screen the cursor lives in. It follows the host's screen size
// (fullscreen high-resolution mode, doc/fullscreen_hires/). PSRAM: no new
// internal RAM statics.
FMRB_EXT_RAM_BSS_ATTR static int g_virt_w;
FMRB_EXT_RAM_BSS_ATTR static int g_virt_h;

static uint32_t now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

// Pick up a screen size change: keep the cursor at the same place on the
// panel by scaling it with the ratio of the two sizes.
static void sync_screen_size(void) {
    int w, h;
    fmrb_host_get_screen_size(&w, &h);
    if (w <= 0 || h <= 0) {
        w = TOUCH_VIRTUAL_W;
        h = TOUCH_VIRTUAL_H;
    }
    if (w == g_virt_w && h == g_virt_h) return;
    if (g_virt_w > 0 && g_virt_h > 0) {
        g_cursor_x = g_cursor_x * w / g_virt_w;
        g_cursor_y = g_cursor_y * h / g_virt_h;
    }
    g_virt_w = w;
    g_virt_h = h;
}

static int touch_scale(void) {
    int scale = TOUCH_PANEL_SHORT / (g_virt_h > 0 ? g_virt_h : TOUCH_VIRTUAL_H);
    return scale > 0 ? scale : 1;
}

static void clamp_cursor(void) {
    if (g_cursor_x < 0) g_cursor_x = 0;
    if (g_cursor_y < 0) g_cursor_y = 0;
    if (g_cursor_x >= g_virt_w) g_cursor_x = g_virt_w - 1;
    if (g_cursor_y >= g_virt_h) g_cursor_y = g_virt_h - 1;
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
        sync_screen_size();

        if (count > 0) {
            // Second finger before any button action promotes to a
            // right-click-on-release. Deliberately NOT entered from MOVE
            // or DRAG: a finger resting down mid-drag must not turn the
            // drag into a right click. In TWO the cursor stays put and
            // GT911 point-0 flapping between fingers is ignored.
            if (count >= 2 &&
                (g_state == TOUCH_STATE_IDLE || g_state == TOUCH_STATE_PENDING)) {
                g_state = TOUCH_STATE_TWO;
            }

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
                const int scale = touch_scale();
                int dx = ((int)tx - (int)g_anchor_tx) / scale;
                int dy = ((int)ty - (int)g_anchor_ty) / scale;
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
            case TOUCH_STATE_TWO:
                // Two-finger tap: right click at the cursor position
                fmrb_host_send_mouse_move(g_cursor_x, g_cursor_y);
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 3, 1);
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 3, 0);
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
        touch_task, "touch", FMRB_TOUCH_TASK_STACK_SIZE, NULL,
        FMRB_TOUCH_TASK_PRIORITY, NULL, FMRB_TOUCH_TASK_CORE);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create touch task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_err_t touch_task_deinit(void) {
    return FMRB_OK;
}
