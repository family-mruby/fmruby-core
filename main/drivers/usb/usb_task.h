#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB Host
 */
fmrb_err_t usb_task_init(void);

/**
 * @brief Start USB Host task
 */
void usb_task_start(void);

#ifdef __cplusplus
}
#endif
