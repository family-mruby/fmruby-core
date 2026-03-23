// M5GFX receiver task
// Receives GFX commands from fmrb_hal_link (local Message Buffer transport)
// and logs them. Future: dispatch to M5GFX display driver.

#include <string.h>
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_task_config.h"
#include "fmrb_hal_link.h"
#include "fmrb_link_types.h"
#include "m5gfx_task.h"

static const char *TAG = "m5gfx";

#define M5GFX_RECV_BUF_SIZE 4096

static fmrb_task_handle_t g_task_handle;
static volatile bool g_running = false;

static void m5gfx_task(void *arg)
{
    static uint8_t recv_buf[M5GFX_RECV_BUF_SIZE];

    FMRB_LOGI(TAG, "M5GFX receiver task started");

    while (g_running) {
        fmrb_link_message_t msg = {
            .data = recv_buf,
            .size = sizeof(recv_buf),
        };

        fmrb_err_t err = fmrb_hal_link_receive(FMRB_LINK_GRAPHICS, &msg, 1000);
        if (err == FMRB_ERR_TIMEOUT) {
            continue;
        }
        if (err != FMRB_OK) {
            FMRB_LOGW(TAG, "Receive error: %d", err);
            fmrb_task_delay_ms(100);
            continue;
        }

        // Log received command (initial implementation)
        FMRB_LOGI(TAG, "GFX command received: %zu bytes [%02x %02x ...]",
                   msg.size,
                   msg.size > 0 ? recv_buf[0] : 0,
                   msg.size > 1 ? recv_buf[1] : 0);

        // TODO: decode msgpack and dispatch to M5GFX display API
    }

    FMRB_LOGI(TAG, "M5GFX receiver task stopped");
    fmrb_task_delete(NULL);
}

fmrb_err_t m5gfx_task_init(void)
{
    if (g_task_handle) {
        FMRB_LOGW(TAG, "Already initialized");
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Initializing M5GFX receiver...");

    g_running = true;

    fmrb_base_type_t result = fmrb_task_create_ex(
        m5gfx_task,
        "m5gfx",
        FMRB_M5GFX_TASK_STACK_SIZE,
        NULL,
        FMRB_M5GFX_TASK_PRIORITY,
        &g_task_handle,
        FMRB_M5GFX_TASK_FLAGS
    );

    if (result != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create M5GFX task");
        g_running = false;
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "M5GFX receiver initialized");
    return FMRB_OK;
}

fmrb_err_t m5gfx_task_deinit(void)
{
    if (!g_task_handle) {
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Deinitializing M5GFX receiver...");
    g_running = false;
    // Task will self-delete after g_running becomes false
    fmrb_task_delay_ms(1500);
    g_task_handle = NULL;

    FMRB_LOGI(TAG, "M5GFX receiver deinitialized");
    return FMRB_OK;
}
