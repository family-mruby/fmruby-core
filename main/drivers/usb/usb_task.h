#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB Host and start HID input processing
 */
fmrb_err_t usb_task_init(void);

/**
 * @brief Start USB Host tasks
 */
void usb_task_start(void);

/**
 * @brief Stop USB Host tasks and release resources
 */
void usb_task_stop(void);

#ifdef __cplusplus
}
#endif
