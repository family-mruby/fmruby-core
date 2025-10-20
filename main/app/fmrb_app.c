#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <picoruby.h>
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "host/host_task.h"

static const char *TAG = "app";

static fmrb_app_task_context_t g_app_context_list[APP_ID_MAX];

static int app_task_from_file(char* path){
    return 0;
}

static int app_task_from_mem(char* app_name, int pool_id, unsigned char* app_irep){
    ESP_LOGI(TAG, "App task started");

    mrb_state *mrb = mrb_open_with_custom_alloc(
        fmrb_get_mempool_ptr(pool_id),
        fmrb_get_mempool_size(pool_id));

    mrc_irep *irep = mrb_read_irep(mrb, app_irep);
    mrc_ccontext *cc = mrc_ccontext_new(mrb);
    mrb_value name = mrb_str_new_lit(mrb, app_name);
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

    ESP_LOGI(TAG, "App task ended");
    vTaskDelete(NULL);

    return 0;
}

static void app_task(void *pvParameters)
{

}

int create_app_task(int type, char* task_name, int stack_size, int prio, TaskHandle_t* handle){
    void (*function_ptr)(void*) func;
    func = 
    BaseType_t xResult = xTaskCreate(
        app_task,
        task_name,
        stack_size,
        NULL,
        prio,
        &handle
    );

    if (xResult != pdPASS) {
        ESP_LOGE(TAG, "Failed to create App task");
        return -1;
    }

    ESP_LOGI(TAG, "App task created successfully");
    return 0;
}

int create_app_task_from_file(char* path){
    // Create kernel task

}

int create_app_task_from_mem(char* app_name, unsigned char* app_irep){

}