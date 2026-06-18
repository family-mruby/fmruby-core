#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Family mruby Modern (ESP32-P4 / Tab5) local display task.
// Renders the gfx command stream (received over the in-process local link)
// to the Tab5 MIPI-DSI panel. Replaces the WROVER/NTSC path of the Retro build.

/**
 * @brief Initialize the Modern display task (Tab5 MIPI-DSI).
 * @return FMRB_OK on success, error code otherwise.
 */
fmrb_err_t display_p4_task_init(void);

/**
 * @brief Deinitialize the Modern display task.
 * @return FMRB_OK on success.
 */
fmrb_err_t display_p4_task_deinit(void);

#ifdef __cplusplus
}
#endif
