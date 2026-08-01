#include "fmrb_gfx_cmd.h"

#include <string.h>

#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"

static const char *TAG = "gfx_cmd";

// Host queue flow control. Owned by the host task, registered below.
static fmrb_semaphore_t g_flow_semaphore = NULL;

void fmrb_gfx_set_flow_semaphore(fmrb_semaphore_t sem)
{
    g_flow_semaphore = sem;
}

fmrb_err_t fmrb_gfx_submit(const gfx_cmd_t *cmd)
{
    if (!cmd) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "No app context for graphics command");
        return FMRB_ERR_INVALID_STATE;
    }

    // Reserving a slot keeps FMRB_HOST_HID_RESERVED_SLOTS free for input.
    fmrb_semaphore_t sem = g_flow_semaphore;
    if (sem) {
        // Wait as long as it takes: the queue draining is the only thing that
        // can make room, and dropping the command instead would lose drawing.
        if (fmrb_semaphore_take(sem, UINT32_MAX) != FMRB_PASS) {
            FMRB_LOGE(TAG, "Failed to acquire the graphics queue semaphore");
            return FMRB_ERR_TIMEOUT;
        }
    }

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_GFX,
        .src_pid = ctx->app_id,
        .size = sizeof(gfx_cmd_t)
    };
    memcpy(msg.data, cmd, sizeof(gfx_cmd_t));

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to send graphics command: %d", ret);
        if (sem) {
            fmrb_semaphore_give(sem);  // give back the slot we never used
        }
    }
    // On success the host task releases the semaphore once it has the command.
    return ret;
}
