#include "hw_proxy.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_hal_file.h"
#include "fmrb_log.h"

static const char *TAG = "hw_proxy";

static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;   // serialize callers
static SemaphoreHandle_t s_done = NULL;    // signal completion
static hw_proxy_request_t *s_current_req = NULL;

// Actual file I/O implementations (called on internal RAM stack)
static void execute_request(hw_proxy_request_t *req)
{
    switch (req->op) {
    case HW_PROXY_OP_FILE_OPEN: {
        hw_proxy_file_open_params_t *p = (hw_proxy_file_open_params_t *)req->params;
        req->result = fmrb_hal_file_open(p->path, p->flags, p->out_handle);
        break;
    }
    case HW_PROXY_OP_FILE_CLOSE: {
        hw_proxy_file_close_params_t *p = (hw_proxy_file_close_params_t *)req->params;
        req->result = fmrb_hal_file_close(p->handle);
        break;
    }
    case HW_PROXY_OP_FILE_READ: {
        hw_proxy_file_read_params_t *p = (hw_proxy_file_read_params_t *)req->params;
        req->result = fmrb_hal_file_read(p->handle, p->buf, p->size, p->out_read);
        break;
    }
    case HW_PROXY_OP_FILE_WRITE: {
        hw_proxy_file_write_params_t *p = (hw_proxy_file_write_params_t *)req->params;
        req->result = fmrb_hal_file_write(p->handle, p->buf, p->size, p->out_written);
        break;
    }
    case HW_PROXY_OP_FILE_SEEK: {
        hw_proxy_file_seek_params_t *p = (hw_proxy_file_seek_params_t *)req->params;
        req->result = fmrb_hal_file_seek(p->handle, p->offset, p->whence);
        break;
    }
    case HW_PROXY_OP_FILE_TELL: {
        hw_proxy_file_tell_params_t *p = (hw_proxy_file_tell_params_t *)req->params;
        req->result = fmrb_hal_file_tell(p->handle, p->position);
        break;
    }
    case HW_PROXY_OP_FILE_SIZE: {
        hw_proxy_file_size_params_t *p = (hw_proxy_file_size_params_t *)req->params;
        req->result = fmrb_hal_file_size(p->handle, p->size);
        break;
    }
    case HW_PROXY_OP_FILE_STAT: {
        hw_proxy_file_stat_params_t *p = (hw_proxy_file_stat_params_t *)req->params;
        req->result = fmrb_hal_file_stat(p->path, p->info);
        break;
    }
    default:
        FMRB_LOGE(TAG, "Unknown op: %d", req->op);
        req->result = FMRB_ERR_INVALID_PARAM;
        break;
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

// --- Convenience wrappers ---

fmrb_err_t hw_proxy_file_open(const char *path, uint32_t flags, fmrb_file_t *out)
{
    hw_proxy_file_open_params_t params = { .path = path, .flags = flags, .out_handle = out };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_OPEN, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_close(fmrb_file_t handle)
{
    hw_proxy_file_close_params_t params = { .handle = handle };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_CLOSE, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_read(fmrb_file_t handle, void *buf, size_t size, size_t *out_read)
{
    hw_proxy_file_read_params_t params = { .handle = handle, .buf = buf, .size = size, .out_read = out_read };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_READ, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_write(fmrb_file_t handle, const void *buf, size_t size, size_t *out_written)
{
    hw_proxy_file_write_params_t params = { .handle = handle, .buf = buf, .size = size, .out_written = out_written };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_WRITE, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_seek(fmrb_file_t handle, int32_t offset, uint32_t whence)
{
    hw_proxy_file_seek_params_t params = { .handle = handle, .offset = offset, .whence = whence };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_SEEK, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_tell(fmrb_file_t handle, uint32_t *position)
{
    hw_proxy_file_tell_params_t params = { .handle = handle, .position = position };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_TELL, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_size(fmrb_file_t handle, uint32_t *size)
{
    hw_proxy_file_size_params_t params = { .handle = handle, .size = size };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_SIZE, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t hw_proxy_file_stat(const char *path, fmrb_file_info_t *info)
{
    hw_proxy_file_stat_params_t params = { .path = path, .info = info };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_FILE_STAT, .params = &params };
    return hw_proxy_call(&req);
}
