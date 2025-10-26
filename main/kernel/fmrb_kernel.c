#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <picoruby.h>
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_app.h"
#include "host/host_task.h"
#include "fmrb_toml.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t kernel_irep[];

static const char *TAG = "kernel";

static void read_system_config(void)
{
    const char *config_path = "/etc/system_conf.toml";

    FMRB_LOGI(TAG, "Loading system configuration from %s", config_path);

    // Load TOML file using helper function
    char errbuf[200];
    toml_table_t *conf = fmrb_toml_load_file(config_path, errbuf, sizeof(errbuf));

    if (!conf) {
        FMRB_LOGW(TAG, "Config load failed: %s", errbuf);
        FMRB_LOGI(TAG, "Using default configuration");
        return;
    }

    // Use helper functions to get values with defaults
    const char* default_val = "undefined";
    const char* system_name = fmrb_toml_get_string(conf, "system_name", default_val);
    bool debug_mode = fmrb_toml_get_bool(conf, "debug_mode", false);
    int64_t log_level = fmrb_toml_get_int(conf, "log_level", 3);

    FMRB_LOGI(TAG, "System Name: %s", system_name);
    FMRB_LOGI(TAG, "Debug Mode: %s", debug_mode ? "enabled" : "disabled");
    FMRB_LOGI(TAG, "Log Level: %" PRId64, log_level);

    // Free system_name if it's not the default value
    if (system_name != default_val) {
        fmrb_sys_free((void*)system_name);
    }

    // Dump full configuration for debugging
    FMRB_LOGI(TAG, "Full configuration:");
    dump_toml_table(conf, 0);

    // Clean up
    toml_free(conf);
    FMRB_LOGI(TAG, "System configuration loaded successfully");
}

/**
 * Start the kernel task
 */
fmrb_err_t fmrb_kernel_start(void)
{
    FMRB_LOGI(TAG, "Starting Family mruby OS Kernel...");

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
        FMRB_LOGE(TAG, "Failed to start host task");
        return FMRB_ERR_FAILED;
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
        FMRB_LOGE(TAG, "Failed to spawn kernel task");
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "Kernel task spawned successfully (id=%ld)", kernel_id);
    return FMRB_OK;
}

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void)
{
    FMRB_LOGI(TAG, "Stopping kernel task...");

    // Use new kill API
    fmrb_app_kill(PROC_ID_KERNEL);

    FMRB_LOGI(TAG, "Kernel task stopped");
}
