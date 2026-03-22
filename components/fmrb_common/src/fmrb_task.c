#include "fmrb_task_config.h"
#include "fmrb_rtos.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "fmrb_task";

// Task monitor registry
typedef struct {
    TaskHandle_t handle;
    const char  *name;
    uint32_t     stack_size;  // requested size in bytes
    uint32_t     flags;
    UBaseType_t  priority;
    uint8_t      active;
} task_entry_t;

static task_entry_t s_tasks[FMRB_TASK_MONITOR_MAX];
static int s_task_count = 0;

static void register_task(TaskHandle_t handle, const char *name,
                          uint32_t stack_size, UBaseType_t prio, uint32_t flags)
{
    if (s_task_count >= FMRB_TASK_MONITOR_MAX) {
        ESP_LOGW(TAG, "Task monitor full, cannot register '%s'", name);
        return;
    }
    task_entry_t *e = &s_tasks[s_task_count++];
    e->handle = handle;
    e->name = name;
    e->stack_size = stack_size;
    e->flags = flags;
    e->priority = prio;
    e->active = 1;
}

static void unregister_task(TaskHandle_t handle)
{
    for (int i = 0; i < s_task_count; i++) {
        if (s_tasks[i].handle == handle) {
            s_tasks[i].active = 0;
            return;
        }
    }
}

fmrb_base_type_t fmrb_task_create_ex(
    TaskFunction_t fn, const char *name, uint32_t stack,
    void *param, UBaseType_t prio, TaskHandle_t *handle,
    uint32_t flags)
{
    fmrb_base_type_t ret;

#ifndef CONFIG_IDF_TARGET_LINUX
    if (flags & FMRB_TASK_FLAG_PSRAM) {
        ret = xTaskCreateWithCaps(fn, name, stack, param, prio, handle, MALLOC_CAP_SPIRAM);
    } else if (flags & (FMRB_TASK_FLAG_PINNED_0 | FMRB_TASK_FLAG_PINNED_1)) {
        int core = (flags & FMRB_TASK_FLAG_PINNED_1) ? 1 : 0;
        ret = xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core);
    } else {
        ret = xTaskCreate(fn, name, stack, param, prio, handle);
    }
#else
    (void)flags;
    ret = xTaskCreate(fn, name, stack, param, prio, handle);
#endif

    if (ret == pdPASS && handle && *handle) {
        register_task(*handle, name, stack * sizeof(StackType_t), prio, flags);
    }
    return ret;
}

void fmrb_task_delete_ex(TaskHandle_t handle)
{
    unregister_task(handle ? handle : xTaskGetCurrentTaskHandle());
    vTaskDelete(handle);
}

int fmrb_task_get_status_list(fmrb_task_info_t *out, int max_count)
{
    int count = 0;
    for (int i = 0; i < s_task_count && count < max_count; i++) {
        task_entry_t *e = &s_tasks[i];
        fmrb_task_info_t *info = &out[count];
        info->name = e->name;
        info->handle = e->handle;
        info->flags = e->flags;
        info->stack_size = e->stack_size;
        info->priority = e->priority;
        info->active = e->active;
        if (e->active && e->handle) {
            info->stack_free = (uint32_t)(uxTaskGetStackHighWaterMark(e->handle) * sizeof(StackType_t));
        } else {
            info->stack_free = 0;
        }
        count++;
    }
    return count;
}

void fmrb_task_dump_status(void)
{
    ESP_LOGI(TAG, "--- Task Status (%d tracked) ---", s_task_count);
    ESP_LOGI(TAG, "%-16s %s %4s %5s %5s %-5s %s",
             "Name", "Act", "Core", "Stack", "Free", "Mem", "Pin");
    for (int i = 0; i < s_task_count; i++) {
        task_entry_t *e = &s_tasks[i];
        uint32_t free_bytes = 0;
        int core = -1;
        if (e->active && e->handle) {
            free_bytes = (uint32_t)(uxTaskGetStackHighWaterMark(e->handle) * sizeof(StackType_t));
#ifndef CONFIG_IDF_TARGET_LINUX
            core = xTaskGetAffinity(e->handle);  // -1 (0x7FFFFFFF) = no affinity
            if (core == 0x7FFFFFFF) core = -1;
#endif
        }
        const char *mem_str = (e->flags & FMRB_TASK_FLAG_PSRAM) ? "PSRAM" : "IRAM";
        const char *pin_str = (e->flags & FMRB_TASK_FLAG_PINNED_0) ? "PIN0" :
                              (e->flags & FMRB_TASK_FLAG_PINNED_1) ? "PIN1" : "ANY";
        char core_str[4];
        if (core >= 0) {
            core_str[0] = '0' + core;
            core_str[1] = '\0';
        } else {
            core_str[0] = '-';
            core_str[1] = '\0';
        }
        ESP_LOGI(TAG, "%-16s %3s %4s %5u %5u %-5s %s",
                 e->name,
                 e->active ? "Y" : "N",
                 core_str,
                 (unsigned)e->stack_size,
                 (unsigned)free_bytes,
                 mem_str,
                 pin_str);
    }
    ESP_LOGI(TAG, "IRAM free: %u bytes, PSRAM free: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "--------------------------------");
}
