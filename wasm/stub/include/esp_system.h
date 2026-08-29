/* esp_system.h stub for the wasm target. esp_restart aborts the module (the
 * page reload is the browser's restart); the heap answers come from the same
 * accounting heap_caps uses. Implementation in wasm/stub/esp_stub.c. */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void esp_restart(void) __attribute__((noreturn));
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_SW,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason(void);

#ifdef __cplusplus
}
#endif
