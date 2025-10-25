#pragma once

#include <stdint.h>
#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the Family mruby OS kernel task
 * @return FMRB_OK on success, FMRB_ERR_* on failure
 */
fmrb_err_t fmrb_kernel_start(void);

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void);


#ifdef __cplusplus
}
#endif
