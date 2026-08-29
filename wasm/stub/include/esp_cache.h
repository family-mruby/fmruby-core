/* esp_cache.h stub: one linear memory, no cache to maintain. */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_CACHE_MSYNC_FLAG_DIR_C2M     (1 << 0)
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C     (1 << 1)
#define ESP_CACHE_MSYNC_FLAG_INVALIDATE  (1 << 2)
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED   (1 << 3)

static inline esp_err_t esp_cache_msync(void *addr, size_t size, int flags)
{
    (void)addr; (void)size; (void)flags;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
