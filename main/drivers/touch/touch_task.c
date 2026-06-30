// Tab5 GT911 touch input driver.
// Polls the touch panel via display_p4_get_touch() (which wraps
// LovyanGFX g_lcd.getTouch()) and feeds mouse events into the
// existing HID pipeline via fmrb_host_send_mouse_move/click.

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

// Coordinate conversion from panel space to virtual display.
// Tab5 LCD (rotation=3): 1280x720 landscape.
// Framebuffer: 320x240 rendered at 3x scale, centered on LCD.
#define TOUCH_SCALE       3
#define TOUCH_X_OFFSET    160   // (1280 - 960) / 2
#define TOUCH_Y_OFFSET    0     // (720 - 720) / 2
#define TOUCH_VIRTUAL_W   320
#define TOUCH_VIRTUAL_H   240

typedef enum {
    TOUCH_STATE_IDLE,
    TOUCH_STATE_PRESSED,
    TOUCH_STATE_MOVED,
} touch_state_t;

static touch_state_t g_state = TOUCH_STATE_IDLE;
static int g_last_vx = 0;
static int g_last_vy = 0;

static bool touch_to_virtual(int16_t tx, int16_t ty, int *vx, int *vy) {
    int x = ((int)tx - TOUCH_X_OFFSET) / TOUCH_SCALE;
    int y = ((int)ty - TOUCH_Y_OFFSET) / TOUCH_SCALE;
    if (x < 0 || x >= TOUCH_VIRTUAL_W || y < 0 || y >= TOUCH_VIRTUAL_H) {
        return false;
    }
    *vx = x;
    *vy = y;
    return true;
}

static void touch_release(void) {
    fmrb_host_send_mouse_click(g_last_vx, g_last_vy, 1, 0);
    g_state = TOUCH_STATE_IDLE;
}

static void touch_task(void *arg) {
    (void)arg;

    // Wait for display_p4 LGFX init to complete
    while (!display_p4_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_READY_POLL_MS));
    }
    FMRB_LOGI(TAG, "Touch task started (poll=%dms)", TOUCH_POLL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));

        int16_t tx, ty;
        int count = display_p4_get_touch(&tx, &ty);

        if (count > 0) {
            int vx, vy;
            if (!touch_to_virtual(tx, ty, &vx, &vy)) {
                // Touch outside virtual display area
                if (g_state != TOUCH_STATE_IDLE) {
                    touch_release();
                }
                continue;
            }

            switch (g_state) {
            case TOUCH_STATE_IDLE:
                // New touch: send move + button down
                fmrb_host_send_mouse_move(vx, vy);
                fmrb_host_send_mouse_click(vx, vy, 1, 1);
                g_last_vx = vx;
                g_last_vy = vy;
                g_state = TOUCH_STATE_PRESSED;
                break;

            case TOUCH_STATE_PRESSED:
            case TOUCH_STATE_MOVED:
                // Dragging: send move if position changed
                if (vx != g_last_vx || vy != g_last_vy) {
                    fmrb_host_send_mouse_move(vx, vy);
                    g_last_vx = vx;
                    g_last_vy = vy;
                    g_state = TOUCH_STATE_MOVED;
                }
                break;
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
