#pragma once

#include <stdint.h>
#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display mode enumeration
 */
typedef enum {
    FMRB_DISPLAY_MODE_NTSC_IPC = 0,  // NTSC via IPC (SPI to WROVER)
    FMRB_DISPLAY_MODE_SPI_DIRECT,    // Direct SPI control
    FMRB_DISPLAY_MODE_HEADLESS,      // No display output
    FMRB_DISPLAY_MODE_MAX
} fmrb_display_mode_t;

/**
 * @brief System configuration structure loaded from /etc/system_conf.toml
 */
typedef struct {
    char system_name[64];                   // System name string
    uint16_t display_width;                 // Physical display width in pixels
    uint16_t display_height;                // Physical display height in pixels
    uint16_t default_user_app_width;        // Default user app window width
    uint16_t default_user_app_height;       // Default user app window height
    fmrb_display_mode_t display_mode;       // Display output mode
    bool debug_mode;                        // Debug mode enabled/disabled
} fmrb_system_config_t;


/**
 * Start the Family mruby OS kernel task
 * @return FMRB_OK on success, FMRB_ERR_* on failure
 */
fmrb_err_t fmrb_kernel_start(void);

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void);


const fmrb_system_config_t* fmrb_kernel_get_config(void);

/**
 * @brief HID routing table structure
 */
typedef struct {
    uint8_t target_pid;      // Target app PID for HID events (0xFF = none)
    uint8_t focused_window;  // Focused window ID (for future use)
    bool routing_enabled;    // Global enable/disable
} fmrb_hid_routing_t;

/**
 * @brief Get HID routing table (for Host Task)
 * @param routing Pointer to routing structure to fill
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_get_hid_routing(fmrb_hid_routing_t *routing);

/**
 * @brief Set HID target PID (for Kernel Ruby)
 * @param target_pid Target app PID (0xFF = none)
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_set_hid_target(uint8_t target_pid);

/**
 * @brief Set focused window ID (for future use)
 * @param window_id Window ID
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_set_focused_window(uint8_t window_id);

/**
 * @brief Enable/disable HID routing
 * @param enable true to enable, false to disable
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_enable_hid_routing(bool enable);

#ifdef __cplusplus
}
#endif
