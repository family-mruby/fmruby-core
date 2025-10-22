#include <stdint.h>
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
#include "kernel/host/host_task.h"
#include "fmrb_app.h"
#include "fmrb_task_config.h"

static const char *TAG = "boot";

// Generated from system_gui.app.rb (will be compiled by picorbc)
extern const uint8_t system_gui_irep[];

static void create_system_app(void)
{
    ESP_LOGI(TAG, "Creating system GUI app...");

    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_SYSTEM_APP,
        .type = APP_TYPE_SYSTEM_APP,
        .name = "system_gui",
        .irep = system_gui_irep,
        .stack_words = FMRB_SYSTEM_APP_TASK_STACK_SIZE,
        .priority = FMRB_SYSTEM_APP_TASK_PRIORITY,
        .core_affinity = -1,  // No core affinity
        .event_queue_len = 10 // Event queue for GUI events
    };

    int32_t app_id;
    bool result = false;
    #if 1
    ESP_LOGI(TAG, "normal spawn");
    fmrb_app_init();
    result = fmrb_app_spawn(&attr, &app_id);
    #else
    ESP_LOGI(TAG, "simple spawn");
    result = fmrb_app_spawn_simple(&attr, &app_id);
    #endif

    if (!result) {
        ESP_LOGE(TAG, "Failed to spawn system GUI app");
        return;
    }

    ESP_LOGI(TAG, "System GUI app spawned successfully (id=%ld)", app_id);
}

// Family mruby OS initialization
void fmrb_os_init(void)
{
    ESP_LOGI(TAG, "Initializing Family mruby OS...");

    // Read setting file
    // /etc/system_config.yaml

    // Reserve Heap Mem for Apps
    // 1M per 1App

    //Start Frmb Kernel
    int32_t result = fmrb_kernel_start();
    if(result < 0){
        ESP_LOGE(TAG, "Failed to kernel");
        return;
    }
    ESP_LOGI(TAG, "fmrb_kernel_start done");

    //TODO wait for kernel startup

    //debug
    create_system_app();

    ESP_LOGI(TAG, "Family mruby OS initialization complete");
}

