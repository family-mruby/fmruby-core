#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <picoruby.h>
#include "fmrb_log.h"
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_app.h"
#include "fmrb_kernel.h"
#include "../boot.h"
#include "fmrb_link_transport.h"
#include "host/host_task.h"
#include "fmrb_toml.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t kernel_irep[];

static const char *TAG = "kernel";

// Forward declarations
static bool init_hid_routing(void);

// System configuration (global, initialized once)
static fmrb_system_config_t g_system_config = {
    .system_name = "Family mruby OS",
    .display_width = 480,
    .display_height = 320,
    .default_user_app_width = 320,
    .default_user_app_height = 240,
    .display_mode = FMRB_DISPLAY_MODE_NTSC_IPC,
    .debug_mode = true
};

// Parse display_mode string to enum
static fmrb_display_mode_t parse_display_mode(const char* mode_str)
{
    if (strcmp(mode_str, "ntsc_ipc") == 0) {
        return FMRB_DISPLAY_MODE_NTSC_IPC;
    } else if (strcmp(mode_str, "spi_direct") == 0) {
        return FMRB_DISPLAY_MODE_SPI_DIRECT;
    } else if (strcmp(mode_str, "headless") == 0) {
        return FMRB_DISPLAY_MODE_HEADLESS;
    }
    return FMRB_DISPLAY_MODE_NTSC_IPC;  // Default
}

static bool read_system_config(void)
{
    const char *config_path = "/etc/system_conf.toml";

    FMRB_LOGI(TAG, "Loading system configuration from %s", config_path);

    // Load TOML file using helper function
    char errbuf[200];
    toml_table_t *conf = fmrb_toml_load_file(config_path, errbuf, sizeof(errbuf));

    if (!conf) {
        FMRB_LOGW(TAG, "Config load failed: %s", errbuf);
        FMRB_LOGI(TAG, "Using default configuration");
        return false;
    }

    // Read system_name
    const char* system_name = fmrb_toml_get_string(conf, "system_name", g_system_config.system_name);
    if (system_name != g_system_config.system_name) {
        strncpy(g_system_config.system_name, system_name, sizeof(g_system_config.system_name) - 1);
        g_system_config.system_name[sizeof(g_system_config.system_name) - 1] = '\0';
        fmrb_sys_free((void*)system_name);
    }

    // Read display dimensions
    g_system_config.display_width = (uint16_t)fmrb_toml_get_int(conf, "display_width",
                                                                 g_system_config.display_width);
    g_system_config.display_height = (uint16_t)fmrb_toml_get_int(conf, "display_height",
                                                                  g_system_config.display_height);

    // Read default user app window size
    g_system_config.default_user_app_width = (uint16_t)fmrb_toml_get_int(conf, "default_user_app_width",
                                                                          g_system_config.default_user_app_width);
    g_system_config.default_user_app_height = (uint16_t)fmrb_toml_get_int(conf, "default_user_app_height",
                                                                           g_system_config.default_user_app_height);

    // Read display mode
    const char* display_mode_str = fmrb_toml_get_string(conf, "display_mode", "ntsc_ipc");
    g_system_config.display_mode = parse_display_mode(display_mode_str);
    if (display_mode_str != NULL && strcmp(display_mode_str, "ntsc_ipc") != 0) {
        fmrb_sys_free((void*)display_mode_str);
    }

    // Read debug mode
    g_system_config.debug_mode = fmrb_toml_get_bool(conf, "debug_mode", g_system_config.debug_mode);

    // Log loaded configuration
    FMRB_LOGI(TAG, "System Name: %s", g_system_config.system_name);
    FMRB_LOGI(TAG, "Display: %dx%d", g_system_config.display_width, g_system_config.display_height);
    FMRB_LOGI(TAG, "Default User App Window: %dx%d",
              g_system_config.default_user_app_width, g_system_config.default_user_app_height);
    FMRB_LOGI(TAG, "Display Mode: %d", g_system_config.display_mode);
    FMRB_LOGI(TAG, "Debug Mode: %s", g_system_config.debug_mode ? "enabled" : "disabled");

    // Dump full configuration for debugging
    FMRB_LOGI(TAG, "Full configuration:");
    dump_toml_table(conf, 0);

    // Clean up
    toml_free(conf);
    FMRB_LOGI(TAG, "System configuration loaded successfully");
    return true;
}


/**
 * Initialize HAL layer and subsystems
 */
static bool init_hal(void)
{
    // Initialize HAL layer
    fmrb_err_t ret = fmrb_hal_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize HAL: %d", ret);
        return false;
    }
    FMRB_LOGI(TAG, "HAL initialized successfully");

    // Initialize message queue registry
    ret = fmrb_msg_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize message queue: %d", ret);
        return false;
    }
    FMRB_LOGI(TAG, "Message queue initialized");

    // Initialize transport (singleton)
    fmrb_link_transport_config_t transport_config = {
        .timeout_ms = 1000,
        .enable_retransmit = true,
        .max_retries = 3,
        .window_size = 8
    };

    ret = fmrb_link_transport_init(&transport_config);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize Transport");
        return false;
    }

    // Note: Version check moved to host_task_init() after host_task starts

    return true;
}

/**
 * Start the kernel task
 */
fmrb_err_t fmrb_kernel_start(void)
{
    FMRB_LOGI(TAG, "Starting Family mruby OS Kernel...");

    // Initialize app context management (first time only)
    static bool context_initialized = false;
    if (context_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if(!read_system_config()){
        return FMRB_ERR_FAILED;
    }
    if(!init_hal()){
        return FMRB_ERR_FAILED;
    }
    if(!init_hid_routing()){
        return FMRB_ERR_FAILED;
    }
    if(!fmrb_app_init()){
        return FMRB_ERR_FAILED;
    }

    // Create host task
    int32_t result = fmrb_host_task_init();
    if (result < 0) {
        FMRB_LOGE(TAG, "Failed to start host task");
        return FMRB_ERR_FAILED;
    }

    int cnt = 0;
    while(!fmrb_host_is_ready())
    {
        FMRB_LOGI(TAG, "Waiting for host to be ready...");
        fmrb_task_delay_ms(100);
        if(cnt >= 30){
            FMRB_LOGE(TAG, "waiting host timeout!");
            return FMRB_ERR_FAILED;        
        }
        cnt++;
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
        .headless = false
    };

    int32_t kernel_id;
    fmrb_err_t ret = fmrb_app_spawn(&attr, &kernel_id);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to spawn kernel task: %d", ret);
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "Kernel task spawned successfully (id=%ld)", kernel_id);

    context_initialized = true;
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

const fmrb_system_config_t* fmrb_kernel_get_config(void)
{
    return &g_system_config;
}

// HID routing table (protected by mutex)
static fmrb_hid_routing_t g_hid_routing = {
    .target_pid = 0xFF,        // No target initially
    .focused_window = 0xFF,
    .routing_enabled = true
};

static fmrb_semaphore_t g_hid_routing_mutex = NULL;

// Initialize HID routing table
static bool init_hid_routing(void)
{
    g_hid_routing_mutex = fmrb_semaphore_create_mutex();
    if (!g_hid_routing_mutex) {
        FMRB_LOGE(TAG, "Failed to create HID routing mutex");
        return false;
    }
    FMRB_LOGI(TAG, "HID routing initialized");
    return true;
}

// Get HID routing table (for Host Task)
fmrb_err_t fmrb_kernel_get_hid_routing(fmrb_hid_routing_t *routing)
{
    if (!routing) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_semaphore_take(g_hid_routing_mutex, portMAX_DELAY);
    *routing = g_hid_routing;
    fmrb_semaphore_give(g_hid_routing_mutex);

    return FMRB_OK;
}

// Set HID target PID (for Kernel Ruby)
fmrb_err_t fmrb_kernel_set_hid_target(uint8_t target_pid)
{
    fmrb_semaphore_take(g_hid_routing_mutex, portMAX_DELAY);
    g_hid_routing.target_pid = target_pid;
    fmrb_semaphore_give(g_hid_routing_mutex);

    FMRB_LOGI(TAG, "HID target set to PID=%d", target_pid);
    return FMRB_OK;
}

// Set focused window ID (for future use)
fmrb_err_t fmrb_kernel_set_focused_window(uint8_t window_id)
{
    fmrb_semaphore_take(g_hid_routing_mutex, portMAX_DELAY);
    g_hid_routing.focused_window = window_id;
    fmrb_semaphore_give(g_hid_routing_mutex);

    FMRB_LOGI(TAG, "Focused window set to ID=%d", window_id);
    return FMRB_OK;
}

// Enable/disable HID routing
fmrb_err_t fmrb_kernel_enable_hid_routing(bool enable)
{
    fmrb_semaphore_take(g_hid_routing_mutex, portMAX_DELAY);
    g_hid_routing.routing_enabled = enable;
    fmrb_semaphore_give(g_hid_routing_mutex);

    FMRB_LOGI(TAG, "HID routing %s", enable ? "enabled" : "disabled");
    return FMRB_OK;
}
