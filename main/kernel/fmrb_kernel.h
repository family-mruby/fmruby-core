#pragma once

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


#ifdef __cplusplus
}
#endif
