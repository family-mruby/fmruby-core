#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <picoruby.h>
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "host/host_task.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t kernel_irep[];

static const char *TAG = "kernel";

// FreeRTOS task handle
static TaskHandle_t g_kernel_task_handle = NULL;

/**
 * Kernel FreeRTOS task
 */
static void fmrb_kernel_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Kernel task started");

    mrb_state *mrb = mrb_open_with_custom_alloc(
        fmrb_get_mempool_ptr(POOL_ID_KERNEL),
        fmrb_get_mempool_size(POOL_ID_KERNEL));

    mrc_irep *irep = mrb_read_irep(mrb, kernel_irep);
    mrc_ccontext *cc = mrc_ccontext_new(mrb);
    mrb_value name = mrb_str_new_lit(mrb, "kernel");
    mrb_value task = mrc_create_task(cc, irep, name, mrb_nil_value(), mrb_obj_value(mrb->top_self));
    if (mrb_nil_p(task)) {
        ESP_LOGE(TAG, "mrbc_create_task failed");
    }
    else {
        mrb_tasks_run(mrb);
    }
    if (mrb->exc) {
        mrb_print_error(mrb);
    }
    mrb_close(mrb);
    mrc_ccontext_free(cc);

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
