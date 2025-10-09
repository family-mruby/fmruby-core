#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "fmrb";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello World from Family mruby!");

#ifdef CONFIG_IDF_TARGET_LINUX
    ESP_LOGI(TAG, "Running on Linux target - ready for mruby execution");
#else
    ESP_LOGI(TAG, "ESP32-S3 ready for mruby execution");
#endif

    int count = 0;
    while (1) {
        ESP_LOGI(TAG, "Loop count: %d", count++);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

#ifdef CONFIG_IDF_TARGET_LINUX
        // Linux版では5回ループしたら終了
        if (count >= 5) {
            ESP_LOGI(TAG, "Linux demo complete, exiting...");
            return;
        }
#endif
    }
}