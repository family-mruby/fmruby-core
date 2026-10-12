// Measurement probe for the blue flash-write flicker (doc/flash_write_flicker/).
// Compiled in only with -DFMRB_FLASH_PROBE=ON; see flash_flicker_probe.c.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FMRB_FLASH_PROBE
// Log the counters (task context). Prints nothing when nothing changed.
void flash_probe_log(void);
#else
static inline void flash_probe_log(void) {}
#endif

#ifdef __cplusplus
}
#endif
