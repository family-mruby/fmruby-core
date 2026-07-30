/**
 * @file fmrb_rtc.h
 * @brief Read the battery-backed clock and set the system clock from it.
 *
 * This lives in C rather than in the kernel Ruby because the kernel runs on
 * either engine. The Ruby version reached the RTC through picoruby's I2C and
 * RX8900 / RX8130 classes, which the Spinel build does not have, so it was
 * wrapped in a spinel-strip marker -- and on the device that left the Spinel
 * kernel with an empty method and a clock stuck at the epoch.
 */
#ifndef FMRB_RTC_H
#define FMRB_RTC_H

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read the RTC and set CLOCK_REALTIME from it.
 *
 * Call once during boot, after the hardware proxy is up (the I2C access is
 * arbitrated through it) and before anything reports a time to the user.
 *
 * @return FMRB_OK when the clock was set. FMRB_ERR_NOT_SUPPORTED off ESP32,
 *         FMRB_ERR_NOT_FOUND when the RTC does not answer, FMRB_ERR_FAILED
 *         when it answers with a time that cannot be true (an unset RTC).
 */
fmrb_err_t fmrb_rtc_sync_system_clock(void);

#ifdef __cplusplus
}
#endif

#endif  // FMRB_RTC_H
