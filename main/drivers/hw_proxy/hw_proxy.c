#include "hw_proxy.h"
#include "hw_proxy_internal.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"

static const char *TAG = "hw_proxy";

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;   // serialize callers
static SemaphoreHandle_t s_done = NULL;    // signal completion
static hw_proxy_request_t *s_current_req = NULL;

static void execute_request(hw_proxy_request_t *req)
{
    if (req->op <= HW_PROXY_OP_FILE_STAT) {
        hw_proxy_file_execute(req);
    } else if (req->op >= HW_PROXY_OP_I2C_INIT && req->op <= HW_PROXY_OP_I2C_WRITE) {
        hw_proxy_i2c_execute(req);
    } else if (req->op >= HW_PROXY_OP_GPIO_RESET && req->op <= HW_PROXY_OP_GPIO_PULL) {
        hw_proxy_gpio_execute(req);
    } else if (req->op >= HW_PROXY_OP_RMT_INIT && req->op <= HW_PROXY_OP_RMT_WRITE) {
        hw_proxy_rmt_execute(req);
    } else {
        FMRB_LOGE(TAG, "Unknown op: %d", req->op);
        req->result = FMRB_ERR_INVALID_PARAM;
    }
}

static void hw_proxy_task(void *arg)
{
    (void)arg;
    FMRB_LOGI(TAG, "HW proxy task started (internal RAM stack)");
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_current_req) {
            execute_request(s_current_req);
            xSemaphoreGive(s_done);
        }
    }
}

void hw_proxy_init(void)
{
    if (s_task != NULL) return;

    s_mutex = xSemaphoreCreateMutex();
    s_done = xSemaphoreCreateBinary();

    fmrb_task_create_ex(
        hw_proxy_task, "hw_proxy",
        FMRB_HW_PROXY_TASK_STACK_SIZE, NULL,
        FMRB_HW_PROXY_TASK_PRIORITY, &s_task,
        FMRB_HW_PROXY_TASK_FLAGS);

    FMRB_LOGI(TAG, "HW proxy initialized");
}

bool hw_proxy_needs_proxy(void)
{
#ifndef CONFIG_IDF_TARGET_LINUX
    // Check if current stack is in PSRAM address range (ESP32-S3: 0x3C000000-0x3DFFFFFF)
    volatile uint8_t local;
    uintptr_t addr = (uintptr_t)&local;
    return (addr >= 0x3C000000 && addr < 0x3E000000);
#else
    return false;
#endif
}

fmrb_err_t hw_proxy_call(hw_proxy_request_t *req)
{
    if (s_task == NULL) {
        FMRB_LOGE(TAG, "HW proxy not initialized");
        return FMRB_ERR_FAILED;
    }

    // Record caller task handle
    req->caller = (hw_proxy_task_handle_t)xTaskGetCurrentTaskHandle();

    // Serialize access from multiple callers
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_current_req = req;
    xTaskNotifyGive(s_task);
    xSemaphoreTake(s_done, portMAX_DELAY);

    fmrb_err_t result = req->result;
    s_current_req = NULL;

    xSemaphoreGive(s_mutex);
    return result;
}

void hw_proxy_release_resources(hw_proxy_task_handle_t owner)
{
    hw_proxy_i2c_release(owner);
    hw_proxy_rmt_release(owner);
    fmrb_pin_manager_release_by_owner(owner);
}
