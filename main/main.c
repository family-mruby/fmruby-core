#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "boot.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Family mruby Core Firmware Starting...");

#ifdef CONFIG_IDF_TARGET_LINUX
    ESP_LOGI(TAG, "Running on Linux target - Development mode");
#else
    ESP_LOGI(TAG, "Running on ESP32-S3-N16R8 - Production mode");
#endif

    // Initialize Family mruby OS
    fmrb_os_init();

    ESP_LOGI(TAG, "app_main exited");
    vTaskDelete(NULL);
}
