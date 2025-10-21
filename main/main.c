#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#ifndef ESP_PLATFORM
#include <unistd.h>
#endif

#include "boot.h"

static const char *TAG = "app_main";

void show_config(void)
{
    ESP_LOGI(TAG, "------------------------------------------------");
    ESP_LOGI("CONFIG", "configTICK_RATE_HZ           = %d", configTICK_RATE_HZ);
    ESP_LOGI("CONFIG", "configMAX_PRIORITIES         = %d", configMAX_PRIORITIES);
    ESP_LOGI("CONFIG", "configMINIMAL_STACK_SIZE     = %d", configMINIMAL_STACK_SIZE);
    ESP_LOGI("CONFIG", "configTOTAL_HEAP_SIZE        = %d", configTOTAL_HEAP_SIZE);
    ESP_LOGI("CONFIG", "configUSE_PREEMPTION         = %d", configUSE_PREEMPTION);
    ESP_LOGI("CONFIG", "configUSE_TIME_SLICING       = %d", configUSE_TIME_SLICING);
    ESP_LOGI("CONFIG", "configUSE_MUTEXES            = %d", configUSE_MUTEXES);
    ESP_LOGI("CONFIG", "configNUM_THREAD_LOCAL_STORAGE_POINTERS = %d", configNUM_THREAD_LOCAL_STORAGE_POINTERS);
    ESP_LOGI("CONFIG", "configCHECK_FOR_STACK_OVERFLOW = %d", configCHECK_FOR_STACK_OVERFLOW);
    ESP_LOGI("CONFIG", "configUSE_TRACE_FACILITY     = %d", configUSE_TRACE_FACILITY);
    ESP_LOGI(TAG, "------------------------------------------------");
    ESP_LOGI(TAG, "tick=%u", (unsigned)xTaskGetTickCount());
}

void app_main(void)
{
    ESP_LOGI(TAG, "Family mruby Core Firmware Starting...");
    show_config();

#ifdef CONFIG_IDF_TARGET_LINUX
    ESP_LOGI(TAG, "Running on Linux target - Development mode");
#else
    ESP_LOGI(TAG, "Running on ESP32-S3-N16R8 - Production mode");
#endif

    // Initialize Family mruby OS
    fmrb_os_init();

    // for linux target
    while (1) {
        ESP_LOGI(TAG, "app_main keep wakeup");
#ifdef ESP_PLATFORM
        vTaskDelay(pdMS_TO_TICKS(10000));
#else
        usleep(10000000);
#endif
    }
    ESP_LOGI(TAG, "app_main exited");
    vTaskDelete(NULL);
}
