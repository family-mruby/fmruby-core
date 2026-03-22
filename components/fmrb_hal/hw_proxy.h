#pragma once

#include "fmrb_err.h"
#include "fmrb_hal_file.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Operation types for hw_proxy_call
typedef enum {
    HW_PROXY_OP_FILE_OPEN,
    HW_PROXY_OP_FILE_CLOSE,
    HW_PROXY_OP_FILE_READ,
    HW_PROXY_OP_FILE_WRITE,
    HW_PROXY_OP_FILE_SEEK,
    HW_PROXY_OP_FILE_TELL,
    HW_PROXY_OP_FILE_SIZE,
    HW_PROXY_OP_FILE_STAT,
} hw_proxy_op_t;

// Operation-specific parameter structs
typedef struct {
    const char *path;
    uint32_t flags;
    fmrb_file_t *out_handle;
} hw_proxy_file_open_params_t;

typedef struct {
    fmrb_file_t handle;
} hw_proxy_file_close_params_t;

typedef struct {
    fmrb_file_t handle;
    void *buf;
    size_t size;
    size_t *out_read;
} hw_proxy_file_read_params_t;

typedef struct {
    fmrb_file_t handle;
    const void *buf;
    size_t size;
    size_t *out_written;
} hw_proxy_file_write_params_t;

typedef struct {
    fmrb_file_t handle;
    int32_t offset;
    uint32_t whence;
} hw_proxy_file_seek_params_t;

typedef struct {
    fmrb_file_t handle;
    uint32_t *position;
} hw_proxy_file_tell_params_t;

typedef struct {
    fmrb_file_t handle;
    uint32_t *size;
} hw_proxy_file_size_params_t;

typedef struct {
    const char *path;
    fmrb_file_info_t *info;
} hw_proxy_file_stat_params_t;

// Generic request
typedef struct {
    hw_proxy_op_t op;
    void *params;
    fmrb_err_t result;
} hw_proxy_request_t;

// Initialize the hw_proxy task (call once during boot)
void hw_proxy_init(void);

// Check if current task needs proxy (running on PSRAM stack)
bool hw_proxy_needs_proxy(void);

// Synchronous call - thread-safe, blocks until operation completes
fmrb_err_t hw_proxy_call(hw_proxy_request_t *req);

// Convenience wrappers for file I/O
fmrb_err_t hw_proxy_file_open(const char *path, uint32_t flags, fmrb_file_t *out);
fmrb_err_t hw_proxy_file_close(fmrb_file_t handle);
fmrb_err_t hw_proxy_file_read(fmrb_file_t handle, void *buf, size_t size, size_t *out_read);
fmrb_err_t hw_proxy_file_write(fmrb_file_t handle, const void *buf, size_t size, size_t *out_written);
fmrb_err_t hw_proxy_file_seek(fmrb_file_t handle, int32_t offset, uint32_t whence);
fmrb_err_t hw_proxy_file_tell(fmrb_file_t handle, uint32_t *position);
fmrb_err_t hw_proxy_file_size(fmrb_file_t handle, uint32_t *size);
fmrb_err_t hw_proxy_file_stat(const char *path, fmrb_file_info_t *info);

#ifdef __cplusplus
}
#endif
