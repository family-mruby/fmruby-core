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
 * Startup synchronization functions
 * Used to coordinate initialization between kernel, host, and app tasks
 */
bool fmrb_kernel_is_ready(void);
bool fmrb_host_is_ready(void);
void fmrb_kernel_set_ready(void);
void fmrb_host_set_ready(void);

#ifdef __cplusplus
}
#endif
