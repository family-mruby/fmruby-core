// Tab5 GT911 touch input driver (trackpad mode).
// Touch movement is relative: finger displacement is added to the current
// cursor position, like a laptop trackpad or remote desktop touchpad.
// Tap = click at current cursor position.

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

typedef enum {
    TOUCH_STATE_IDLE,
    TOUCH_STATE_PRESSED,
    TOUCH_STATE_MOVED,
} touch_state_t;

static touch_state_t g_state = TOUCH_STATE_IDLE;

// Current cursor position in virtual display coordinates
static int g_cursor_x = TOUCH_VIRTUAL_W / 2;
static int g_cursor_y = TOUCH_VIRTUAL_H / 2;

// Touch anchor: panel-space coordinates at touch-down
static int16_t g_anchor_tx = 0;
static int16_t g_anchor_ty = 0;

static void clamp_cursor(void) {
    if (g_cursor_x < 0) g_cursor_x = 0;
    if (g_cursor_y < 0) g_cursor_y = 0;
    if (g_cursor_x >= TOUCH_VIRTUAL_W) g_cursor_x = TOUCH_VIRTUAL_W - 1;
    if (g_cursor_y >= TOUCH_VIRTUAL_H) g_cursor_y = TOUCH_VIRTUAL_H - 1;
}

static void touch_release(void) {
    fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 1, 0);
    g_state = TOUCH_STATE_IDLE;
}

static void touch_task(void *arg) {
    (void)arg;

    // Wait for display_p4 LGFX init to complete
    while (!display_p4_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_READY_POLL_MS));
    }
    FMRB_LOGI(TAG, "Touch task started (trackpad mode, poll=%dms)", TOUCH_POLL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));

        int16_t tx, ty;
        int count = display_p4_get_touch(&tx, &ty);

        if (count > 0) {
            switch (g_state) {
            case TOUCH_STATE_IDLE:
                // Touch down: record anchor, send click at current cursor pos
                g_anchor_tx = tx;
                g_anchor_ty = ty;
                fmrb_host_send_mouse_move(g_cursor_x, g_cursor_y);
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, 1, 1);
                g_state = TOUCH_STATE_PRESSED;
                break;

            case TOUCH_STATE_PRESSED:
            case TOUCH_STATE_MOVED: {
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
                    g_state = TOUCH_STATE_MOVED;
                }
                break;
            }
            }
        } else {
            // No touch: release if we were pressing
            if (g_state != TOUCH_STATE_IDLE) {
                touch_release();
            }
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
