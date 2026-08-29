/* esp_cache_private.h stub: a fixed 64-byte "cache line" for the aligned
 * allocations the display path makes. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t esp_cache_get_alignment(uint32_t caps, size_t *out_alignment)
{
    (void)caps;
    if (out_alignment) *out_alignment = 64;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
