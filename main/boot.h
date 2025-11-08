#pragma once

#include <stdint.h>

void fmrb_os_init(void);
void fmrb_os_close(void);

bool fmrb_kernel_is_ready(void);
bool fmrb_host_is_ready(void);
void fmrb_kernel_set_ready(void);
void fmrb_host_set_ready(void);