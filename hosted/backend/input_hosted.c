/*
 * input_hosted.c - usb_task.h on the hosted builds (wasm, native).
 *
 * Starts the build's input sources (input_source.h) where boot.c starts the
 * USB host on a device. The device-list surface answers "no devices": there
 * is no USB bus, so the HID Inspector lists nothing and raw-report
 * subscription is refused.
 */

#include <stddef.h>

#include "fmrb_err.h"
#include "fmrb_log.h"
#include "usb_task.h"
#include "input_source.h"

static const char *TAG = "input";

fmrb_err_t usb_task_init(void)
{
    int started = 0;
    for (int i = 0; fmrb_input_sources[i] != NULL; i++) {
        const fmrb_input_source_t *src = fmrb_input_sources[i];
        if (src->start() == FMRB_OK) {
            FMRB_LOGI(TAG, "input source up: %s", src->name);
            started++;
        } else {
            FMRB_LOGW(TAG, "input source failed to start: %s", src->name);
        }
    }
    return started > 0 ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t usb_task_power_on_root_port(void)
{
    return FMRB_OK;
}

void usb_task_start(void)
{
}

void usb_task_stop(void)
{
}

int usb_task_get_device_info(fmrb_usb_device_info_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

fmrb_err_t usb_task_subscribe_raw_reports(int8_t slot_index, uint16_t subscriber_pid)
{
    (void)slot_index;
    (void)subscriber_pid;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t usb_task_unsubscribe_raw_reports(int8_t slot_index)
{
    (void)slot_index;
    return FMRB_ERR_NOT_SUPPORTED;
}
