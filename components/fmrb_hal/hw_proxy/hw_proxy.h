#pragma once

#include "fmrb_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Operation types for hw_proxy_call
typedef enum {
    // File ops (0x00-0x0F)
    HW_PROXY_OP_FILE_OPEN  = 0x00,
    HW_PROXY_OP_FILE_CLOSE = 0x01,
    HW_PROXY_OP_FILE_READ  = 0x02,
    HW_PROXY_OP_FILE_WRITE = 0x03,
    HW_PROXY_OP_FILE_SEEK  = 0x04,
    HW_PROXY_OP_FILE_TELL  = 0x05,
    HW_PROXY_OP_FILE_SIZE  = 0x06,
    HW_PROXY_OP_FILE_STAT  = 0x07,

    // I2C ops (0x10-0x1F)
    HW_PROXY_OP_I2C_INIT   = 0x10,
    HW_PROXY_OP_I2C_READ   = 0x11,
    HW_PROXY_OP_I2C_WRITE  = 0x12,

    // GPIO ops (0x20-0x2F)
    HW_PROXY_OP_GPIO_RESET    = 0x20,
    HW_PROXY_OP_GPIO_CONFIG   = 0x21,
    HW_PROXY_OP_GPIO_READ     = 0x22,
    HW_PROXY_OP_GPIO_WRITE    = 0x23,
    HW_PROXY_OP_GPIO_PULL     = 0x24,

    // RMT ops (0x30-0x3F)
    HW_PROXY_OP_RMT_INIT  = 0x30,
    HW_PROXY_OP_RMT_WRITE = 0x31,
} hw_proxy_op_t;

// Opaque task handle type (avoids FreeRTOS header dependency)
typedef void *hw_proxy_task_handle_t;

// Generic request
typedef struct {
    hw_proxy_op_t op;
    void *params;
    fmrb_err_t result;
    hw_proxy_task_handle_t caller;  // set automatically by hw_proxy_call
} hw_proxy_request_t;

// Initialize the hw_proxy task (call once during boot)
void hw_proxy_init(void);

// Check if current task needs proxy (running on PSRAM stack)
bool hw_proxy_needs_proxy(void);

// Synchronous call - thread-safe, blocks until operation completes
fmrb_err_t hw_proxy_call(hw_proxy_request_t *req);

// Release all HW resources owned by the specified task
void hw_proxy_release_resources(hw_proxy_task_handle_t owner);

#ifdef __cplusplus
}
#endif
