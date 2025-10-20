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
int fmrb_kernel_start(void)
{
    ESP_LOGI(TAG, "Starting Family mruby OS Kernel...");

    // Initialize app context management (first time only)
    static bool context_initialized = false;
    if (!context_initialized) {
        fmrb_app_init();
        context_initialized = true;
    }

    // Create host task
    int result = fmrb_host_task_init();
    if (result < 0) {
        ESP_LOGE(TAG, "Failed to start host task");
        return -1;
    }

    // Create kernel task using unified API
    result = fmrb_app_create_task(
        PROC_ID_KERNEL,
        "fmrb_kernel",
        APP_TYPE_KERNEL,
        (unsigned char*)kernel_irep,
        FMRB_KERNEL_TASK_STACK_SIZE,
        FMRB_KERNEL_TASK_PRIORITY
    );

    if (result != 0) {
        ESP_LOGE(TAG, "Failed to create kernel task");
        return -1;
    }

    ESP_LOGI(TAG, "Kernel task created successfully");
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
