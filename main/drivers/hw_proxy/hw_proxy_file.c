#include "hw_proxy_internal.h"
#include "fmrb_hal_file.h"
#include "fmrb_log.h"

static const char *TAG = "hw_proxy_file";

void hw_proxy_file_execute(hw_proxy_request_t *req)
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
        FMRB_LOGE(TAG, "Unknown file op: %d", req->op);
        req->result = FMRB_ERR_INVALID_PARAM;
        break;
    }
}
