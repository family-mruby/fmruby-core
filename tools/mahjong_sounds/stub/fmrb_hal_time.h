/* Host stand-in for fmrb_hal_time.h (tools/mahjong_sounds): only nes_apu.c's
 * write log reads it, and that log is compiled out. */
#pragma once
#include <stdint.h>
static inline int64_t fmrb_hal_time_get_ms(void) { return 0; }
