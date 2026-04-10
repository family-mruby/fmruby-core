#pragma once

#include "fmrb_hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize RMT transmit channel
 * @param gpio GPIO pin number for output
 * @param t0h_ns Duration for bit-0 high (nanoseconds)
 * @param t0l_ns Duration for bit-0 low (nanoseconds)
 * @param t1h_ns Duration for bit-1 high (nanoseconds)
 * @param t1l_ns Duration for bit-1 low (nanoseconds)
 * @param reset_ns Duration for reset signal (nanoseconds)
 * @return 0 on success, -1 on error
 */
int fmrb_hal_rmt_init(uint32_t gpio, uint32_t t0h_ns, uint32_t t0l_ns,
                       uint32_t t1h_ns, uint32_t t1l_ns, uint32_t reset_ns);

/**
 * @brief Transmit data via RMT channel
 * @param buffer Data buffer to transmit
 * @param nbytes Number of bytes to transmit
 * @return 0 on success, -1 on error
 */
int fmrb_hal_rmt_write(uint8_t *buffer, uint32_t nbytes);

#ifdef __cplusplus
}
#endif
