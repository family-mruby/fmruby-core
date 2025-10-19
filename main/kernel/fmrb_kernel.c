#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <mruby.h>
#include <mruby/irep.h>
#include "task_config.h"
#include "host/host_task.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t kernel_irep[];

static const char *TAG = "kernel";

// Global mruby state for the kernel
static mrb_state *g_kernel_mrb = NULL;

// FreeRTOS task handle
static TaskHandle_t g_kernel_task_handle = NULL;

// Forward declarations
extern void mrb_picoruby_fmrb_init(mrb_state *mrb);

mrb_state *global_mrb = NULL;  // Kernel VM only

/**
 * Initialize the kernel mruby VM
 */
static int fmrb_kernel_init_vm(void)
{
    ESP_LOGI(TAG, "Initializing kernel VM...");
    // Create mruby state
    g_kernel_mrb = mrb_open();
    if (!g_kernel_mrb) {
        ESP_LOGE(TAG, "Failed to create mruby state");
        return -1;
    }
    // TODO: to check best way to handle prism
    global_mrb = g_kernel_mrb;

    // Initialize FMRB bindings
    mrb_picoruby_fmrb_init(g_kernel_mrb);
    ESP_LOGI(TAG, "FMRB bindings initialized");

    ESP_LOGI(TAG, "Kernel VM initialized successfully");
    return 0;
}

/**
 * Load and execute kernel.rb bytecode
 */
static int fmrb_kernel_load_bytecode(void)
{
    ESP_LOGI(TAG, "Loading kernel bytecode...");

    if (!g_kernel_mrb) {
        ESP_LOGE(TAG, "mruby state not initialized");
        return -1;
    }

    // Load kernel bytecode
    mrb_load_irep(g_kernel_mrb, kernel_irep);

    // Check for exceptions
    if (g_kernel_mrb->exc) {
        ESP_LOGE(TAG, "Exception during kernel load:");
        mrb_print_error(g_kernel_mrb);
        g_kernel_mrb->exc = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "Kernel bytecode loaded successfully");
    return 0;
}

/**
 * Cleanup kernel VM
 */
static void fmrb_kernel_deinit_vm(void)
{
    ESP_LOGI(TAG, "Shutting down kernel VM...");

    if (g_kernel_mrb) {
        mrb_close(g_kernel_mrb);
        g_kernel_mrb = NULL;
    }

    ESP_LOGI(TAG, "Kernel VM shutdown complete");
}

/**
 * Kernel FreeRTOS task
 */
static void fmrb_kernel_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Kernel task started");

    // Initialize VM
    if (fmrb_kernel_init_vm() != 0) {
        ESP_LOGE(TAG, "Failed to initialize kernel VM");
        vTaskDelete(NULL);
        return;
    }

    // Load and execute kernel bytecode
    // Note: kernel.rb contains an infinite loop, so this will not return
    // until the kernel is stopped
    if (fmrb_kernel_load_bytecode() != 0) {
        ESP_LOGE(TAG, "Failed to load kernel bytecode");
        fmrb_kernel_deinit_vm();
        vTaskDelete(NULL);
        return;
    }

    // Cleanup (only reached if kernel.rb exits)
    ESP_LOGI(TAG, "Kernel main loop exited");
    fmrb_kernel_deinit_vm();

    ESP_LOGI(TAG, "Kernel task ended");
    vTaskDelete(NULL);
}

/**
 * Start the kernel task
 */
int fmrb_kernel_start(void)
{
    ESP_LOGI(TAG, "Starting Family mruby OS Kernel...");
    // Create host task
    int result = fmrb_host_task_init();
    if (result < 0) {
        ESP_LOGE(TAG, "Failed to start host task");
        return -1;
    }

    // Create kernel task
    BaseType_t xResult = xTaskCreate(
        fmrb_kernel_task,
        "fmrb_kernel",
        FMRB_KERNEL_TASK_STACK_SIZE,
        NULL,
        FMRB_KERNEL_TASK_PRIORITY,
        &g_kernel_task_handle
    );

    if (xResult != pdPASS) {
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

    if (g_kernel_task_handle) {
        vTaskDelete(g_kernel_task_handle);
        g_kernel_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Kernel task stopped");
}

/**
 * Get the kernel mruby state
 */
mrb_state* fmrb_kernel_get_mrb(void)
{
    return g_kernel_mrb;
}
