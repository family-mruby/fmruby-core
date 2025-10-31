#pragma once

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// Time types
typedef uint64_t fmrb_time_t;  // microseconds since boot


/**
 * @brief Get current time in microseconds since boot
 * @return Current time in microseconds
 */
fmrb_time_t fmrb_hal_time_get_us(void);

/**
 * @brief Get current time in milliseconds since boot
 * @return Current time in milliseconds
 */
uint64_t fmrb_hal_time_get_ms(void);

/**
 * @brief Sleep for specified microseconds
 * @param us Microseconds to sleep
 */
void fmrb_hal_time_delay_us(uint32_t us);

/**
 * @brief Sleep for specified milliseconds
 * @param ms Milliseconds to sleep
 */
void fmrb_hal_time_delay_ms(uint32_t ms);

/**
 * @brief Check if time has elapsed since start_time
 * @param start_time Start time in microseconds
 * @param timeout_us Timeout in microseconds
 * @return true if timeout elapsed, false otherwise
 */
bool fmrb_hal_time_is_timeout(fmrb_time_t start_time, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif
