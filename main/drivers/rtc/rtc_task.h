#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the RTC task.
 * @return FMRB_OK on success
 */
fmrb_err_t rtc_task_start(void);

#ifdef __cplusplus
}
#endif
