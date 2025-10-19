#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

// Family mruby modules
#include "lib/fmrb_hal/fmrb_hal.h"
#include "lib/fmrb_gfx/fmrb_gfx.h"
#include "lib/fmrb_audio/fmrb_audio.h"

#include "boot.h"
#include "kernel/fmrb_kernel.h"
#include "kernel/system_task.h"

static const char *TAG = "boot";

// Global contexts
static fmrb_gfx_context_t g_gfx_context = NULL;

// Family mruby OS initialization
void fmrb_os_init(void)
{
    ESP_LOGI(TAG, "Initializing Family mruby OS...");

    // Read setting file
    // /etc/system_config.yaml

    // Reserve Heap Mem for Apps
    // 1M per 1App

    // Start Frmb Kernel
    int result = fmrb_kernel_start();
    if(result < 0){
        ESP_LOGE(TAG, "Failed to kernel");
        return;
    }

    // Create system apps
    ESP_LOGI(TAG, "Family mruby OS initialization complete");
}

