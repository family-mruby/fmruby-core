#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_app.h"
#include "host/host_task.h"
#include "toml.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t kernel_irep[];

static const char *TAG = "kernel";

static void read_system_config(void)
{
    const char *config_path = "/flash/etc/system_conf.toml";
    fmrb_file_t file;
    char *buffer = NULL;
    size_t file_size = 0;

    ESP_LOGI(TAG, "Loading system configuration from %s", config_path);

    // Get file info first to determine size
    fmrb_file_info_t info;
    fmrb_err_t ret = fmrb_hal_file_stat(config_path, &info);
    if (ret != FMRB_OK) {
        ESP_LOGW(TAG, "Config file not found, using defaults");
        return;
    }

    if (info.size == 0) {
        ESP_LOGW(TAG, "Config file is empty");
        return;
    }
    file_size = info.size;

    // Open config file
    ret = fmrb_hal_file_open(config_path, FMRB_O_RDONLY, &file);
    if (ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to open config file");
        return;
    }

    // Allocate buffer for file content
    buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for config (%zu bytes)", file_size);
        fmrb_hal_file_close(file);
        return;
    }

    // Read file content
    size_t bytes_read;
    ret = fmrb_hal_file_read(file, buffer, file_size, &bytes_read);
    fmrb_hal_file_close(file);

    if (ret != FMRB_OK || bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read config file (read %zu of %zu bytes)", bytes_read, file_size);
        free(buffer);
        return;
    }
    buffer[bytes_read] = '\0';

    // Parse TOML
    char errbuf[200];
    toml_table_t *conf = toml_parse(buffer, errbuf, sizeof(errbuf));
    free(buffer);

    if (!conf) {
        ESP_LOGE(TAG, "TOML parse error: %s", errbuf);
        return;
    }

    // Example: Parse configuration values
    // Get system name
    toml_datum_t system_name = toml_string_in(conf, "system_name");
    if (system_name.ok) {
        ESP_LOGI(TAG, "System name: %s", system_name.u.s);
        free(system_name.u.s);
    }

    // Get debug mode flag
    toml_datum_t debug_mode = toml_bool_in(conf, "debug_mode");
    if (debug_mode.ok) {
        ESP_LOGI(TAG, "Debug mode: %s", debug_mode.u.b ? "enabled" : "disabled");
    }

    // Get memory pool size (example)
    toml_datum_t memory_pool = toml_int_in(conf, "memory_pool_size");
    if (memory_pool.ok) {
        ESP_LOGI(TAG, "Memory pool size: %lld bytes", memory_pool.u.i);
    }

    // TODO: Store parsed configuration in global structure or apply settings

    // Clean up
    toml_free(conf);
    ESP_LOGI(TAG, "System configuration loaded successfully");
}

/**
 * Start the kernel task
 */
int32_t fmrb_kernel_start(void)
{
    ESP_LOGI(TAG, "Starting Family mruby OS Kernel...");

    // Initialize app context management (first time only)
    static bool context_initialized = false;
    if (!context_initialized) {
        read_system_config();
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
