#include <stdio.h>
#include <stdint.h>
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "boot.h"

static const char *TAG = "app_main";

void app_main(void)
{
    // Initialize Family mruby OS
    fmrb_os_init();

    // for linux target
    while (1) {
        FMRB_LOGD(TAG, "app_main keep wakeup");
        FMRB_LOGD("SIG", "app_main tick=%u", (unsigned)fmrb_task_get_tick_count());
        fmrb_task_delay(FMRB_MS_TO_TICKS(100000));
    }

    fmrb_os_close();
    FMRB_LOGI(TAG, "app_main exited");
    fmrb_task_delete(0);
}
