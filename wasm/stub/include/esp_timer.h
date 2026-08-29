/* esp_timer.h stub: only the clock. No timer objects -- nothing in the wasm
 * source set creates one. Implementation in wasm/stub/esp_stub.c. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif
