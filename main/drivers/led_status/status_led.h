#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FMRB_LED_STATUS_FATAL 0

/**
 * Start the status LED task.
 * Normal: LED solid ON.
 * Error:  LED blinks at ~2Hz.
 */
void status_led_start(void);

/**
 * Set error flag — LED starts blinking.
 */
void status_led_set_error(int level);

/**
 * Clear error flag — LED returns to solid ON.
 */
void status_led_clear_error(void);

#ifdef __cplusplus
}
#endif
