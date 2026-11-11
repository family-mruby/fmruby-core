#include "hw_proxy.h"
#include "hw_proxy_internal.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"
#include "fmrb_attr.h"
#include "fmrb_hal_time.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_memory_utils.h"
#endif

static const char *TAG = "hw_proxy";

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;   // serialize callers
static SemaphoreHandle_t s_done = NULL;    // signal completion
static hw_proxy_request_t *s_current_req = NULL;

static void execute_request(hw_proxy_request_t *req)
{
    if (req->op <= HW_PROXY_OP_DIR_STAT) {
        hw_proxy_file_execute(req);
    } else if (req->op >= HW_PROXY_OP_I2C_INIT && req->op <= HW_PROXY_OP_I2C_RELEASE) {
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

// A request that holds the proxy this long is logged (op, path or handle,
// size, time), so a stall elsewhere can be matched to it by the log's time.
// At most one line a second; the ones in between are counted into the next.
// The state is in PSRAM (no internal RAM for a diagnostic).
#define HW_PROXY_SLOW_MS     50
#define HW_PROXY_SLOW_GAP_MS 1000
FMRB_EXT_RAM_BSS_ATTR static uint32_t s_slow_logged_ms;
FMRB_EXT_RAM_BSS_ATTR static uint32_t s_slow_skipped, s_slow_skipped_max_ms;

static void note_request_time(const hw_proxy_request_t *req, uint32_t ms)
{
    if (ms < HW_PROXY_SLOW_MS) return;
    const uint32_t now = (uint32_t)fmrb_hal_time_get_ms();
    if (s_slow_logged_ms != 0 && now - s_slow_logged_ms < HW_PROXY_SLOW_GAP_MS) {
        s_slow_skipped++;
        if (ms > s_slow_skipped_max_ms) s_slow_skipped_max_ms = ms;
        return;
    }
    const void *pr = req->params;
    const char *path = NULL;
    const void *handle = NULL;
    size_t size = 0;
    switch (req->op) {
    case HW_PROXY_OP_FILE_OPEN:  path = ((const hw_proxy_file_open_params_t *)pr)->path; break;
    case HW_PROXY_OP_FILE_STAT:  path = ((const hw_proxy_file_stat_params_t *)pr)->path; break;
    case HW_PROXY_OP_DIR_OPEN:   path = ((const hw_proxy_dir_open_params_t *)pr)->path; break;
    case HW_PROXY_OP_DIR_STAT:   path = ((const hw_proxy_dir_stat_params_t *)pr)->path; break;
    case HW_PROXY_OP_FILE_CLOSE: handle = ((const hw_proxy_file_close_params_t *)pr)->handle; break;
    case HW_PROXY_OP_FILE_SEEK:  handle = ((const hw_proxy_file_seek_params_t *)pr)->handle; break;
    case HW_PROXY_OP_FILE_TELL:  handle = ((const hw_proxy_file_tell_params_t *)pr)->handle; break;
    case HW_PROXY_OP_FILE_SIZE:  handle = ((const hw_proxy_file_size_params_t *)pr)->handle; break;
    case HW_PROXY_OP_FILE_READ:
        handle = ((const hw_proxy_file_read_params_t *)pr)->handle;
        size = ((const hw_proxy_file_read_params_t *)pr)->size;
        break;
    case HW_PROXY_OP_FILE_WRITE:
        handle = ((const hw_proxy_file_write_params_t *)pr)->handle;
        size = ((const hw_proxy_file_write_params_t *)pr)->size;
        break;
    case HW_PROXY_OP_DIR_READ:   handle = ((const hw_proxy_dir_read_params_t *)pr)->dir; break;
    case HW_PROXY_OP_DIR_CLOSE:  handle = ((const hw_proxy_dir_close_params_t *)pr)->dir; break;
    default: break;
    }
    const char *caller = req->caller ? pcTaskGetName((TaskHandle_t)req->caller) : "?";
    if (path) {
        FMRB_LOGW(TAG, "slow op 0x%02x %s by %s: %ums (+%u more, max %ums)",
                  (unsigned)req->op, path, caller, (unsigned)ms,
                  (unsigned)s_slow_skipped, (unsigned)s_slow_skipped_max_ms);
    } else {
        FMRB_LOGW(TAG, "slow op 0x%02x handle=%p size=%u by %s: %ums (+%u more, max %ums)",
                  (unsigned)req->op, handle, (unsigned)size, caller, (unsigned)ms,
                  (unsigned)s_slow_skipped, (unsigned)s_slow_skipped_max_ms);
    }
    s_slow_logged_ms = now ? now : 1;
    s_slow_skipped = 0;
    s_slow_skipped_max_ms = 0;
}

static void hw_proxy_task(void *arg)
{
    (void)arg;
    FMRB_LOGI(TAG, "HW proxy task started (internal RAM stack)");
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        hw_proxy_request_t *req = s_current_req;
        if (req) {
            const uint64_t t0 = fmrb_hal_time_get_ms();
            execute_request(req);
            // Noted before the caller is let go: req and its params live on
            // the caller's stack.
            note_request_time(req, (uint32_t)(fmrb_hal_time_get_ms() - t0));
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
    // Is the current stack in PSRAM? esp_ptr_external_ram knows each chip's
    // range (S3 0x3C000000-0x3DFFFFFF, P4 0x48000000-...); the S3 range alone
    // let a PSRAM-stack task on the P4 reach the SD card directly.
    return esp_ptr_external_ram(__builtin_frame_address(0));
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
