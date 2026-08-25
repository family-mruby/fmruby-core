#pragma once

#include <stdint.h>

void fmrb_os_init(void);

#ifdef CONFIG_IDF_TARGET_LINUX
/** Simulator only: route IDF logging through a tick-safe output
 *  (boot/sim_log_guard.c). Call before the first log line. */
void fmrb_sim_log_guard_init(void);
#endif
void fmrb_os_close(void);

bool fmrb_kernel_is_ready(void);
bool fmrb_host_is_ready(void);
void fmrb_kernel_set_ready(void);
void fmrb_host_set_ready(void);