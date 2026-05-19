#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Error LED patterns
#define FMRB_LED_STATUS_NONE              0
#define FMRB_LED_STATUS_FATAL             1  // Solid red
#define FMRB_LED_STATUS_VERSION_MISMATCH  2  // 3 quick red pulses + long gap

/**
 * Start the status LED task.
 */
void status_led_start(void);

/**
 * Set error flag — LED starts blinking / showing the selected pattern.
 * @param level one of FMRB_LED_STATUS_*
 */
void status_led_set_error(int level);

/**
 * Clear error flag.
 */
void status_led_clear_error(void);

/**
 * Mark boot complete. Switches the green LED from the boot fast-blink
 * pattern to the normal heartbeat pattern. Safe to call multiple times.
 */
void status_led_set_boot_complete(void);

#ifdef __cplusplus
}
#endif
