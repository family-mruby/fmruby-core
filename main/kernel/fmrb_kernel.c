#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <picoruby.h>
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_app.h"
#include "host/host_task.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t kernel_irep[];

static const char *TAG = "kernel";

/**
 * Start the kernel task
 */
int32_t fmrb_kernel_start(void)
{
    ESP_LOGI(TAG, "Starting Family mruby OS Kernel...");

    // Initialize app context management (first time only)
    static bool context_initialized = false;
    if (!context_initialized) {
        fmrb_app_init();
        context_initialized = true;
    }

    // Create host task
    int32_t result = fmrb_host_task_init();
    if (result < 0) {
        ESP_LOGE(TAG, "Failed to start host task");
        return -1;
    }

    // Create kernel task using spawn API
    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_KERNEL,
        .type = APP_TYPE_KERNEL,
        .name = "fmrb_kernel",
        .irep = kernel_irep,
        .stack_words = FMRB_KERNEL_TASK_STACK_SIZE,
        .priority = FMRB_KERNEL_TASK_PRIORITY,
        .core_affinity = -1,  // No core affinity
        .event_queue_len = 0  // No event queue for kernel
    };

    int32_t kernel_id;
    if (!fmrb_app_spawn(&attr, &kernel_id)) {
        ESP_LOGE(TAG, "Failed to spawn kernel task");
        return -1;
    }

    ESP_LOGI(TAG, "Kernel task spawned successfully (id=%ld)", kernel_id);
    return 0;
}

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void)
{
    ESP_LOGI(TAG, "Stopping kernel task...");

    // Use new kill API
    fmrb_app_kill(PROC_ID_KERNEL);

    ESP_LOGI(TAG, "Kernel task stopped");
}
