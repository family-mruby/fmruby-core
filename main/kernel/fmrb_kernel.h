#ifndef FMRB_KERNEL_H
#define FMRB_KERNEL_H

#include "picoruby.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the Family mruby OS kernel task
 * @return 0 on success, -1 on failure
 */
int fmrb_kernel_start(void);

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void);

/**
 * Get the kernel mruby state
 * @return mrb_state pointer, or NULL if not initialized
 */
mrb_state* fmrb_kernel_get_mrb(void);

#ifdef __cplusplus
}
#endif

#endif // FMRB_KERNEL_H
