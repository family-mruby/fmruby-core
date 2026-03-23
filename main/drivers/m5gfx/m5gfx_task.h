#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize M5GFX receiver task
 *
 * Starts a task that receives GFX commands via fmrb_hal_link
 * and logs them. Future: dispatch to M5GFX display driver.
 *
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t m5gfx_task_init(void);

/**
 * @brief Deinitialize M5GFX receiver task
 *
 * @return FMRB_OK on success
 */
fmrb_err_t m5gfx_task_deinit(void);

#ifdef __cplusplus
}
#endif
