#include "fmrb_rtos.h"

fmrb_base_type_t fmrb_task_create_ex(
    TaskFunction_t fn, const char *name, uint32_t stack,
    void *param, UBaseType_t prio, TaskHandle_t *handle,
    uint32_t flags)
{
#ifndef CONFIG_IDF_TARGET_LINUX
    if (flags & FMRB_TASK_FLAG_PSRAM) {
        return xTaskCreateWithCaps(fn, name, stack, param, prio, handle, MALLOC_CAP_SPIRAM);
    }
    if (flags & (FMRB_TASK_FLAG_PINNED_0 | FMRB_TASK_FLAG_PINNED_1)) {
        int core = (flags & FMRB_TASK_FLAG_PINNED_1) ? 1 : 0;
        return xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core);
    }
#else
    (void)flags;
#endif
    return xTaskCreate(fn, name, stack, param, prio, handle);
}
