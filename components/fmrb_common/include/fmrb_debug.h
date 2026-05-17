#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Query the current debug mode flag.
 *
 * Mirrors the `debug_mode` entry in /etc/system_conf.toml. Used to gate
 * periodic diagnostic logs (GFX stats, transport stats, task status dump)
 * so production builds stay quiet on the serial console.
 *
 * The default before kernel config load is `true`, matching the default
 * value of fmrb_system_config_t.debug_mode.
 */
bool fmrb_debug_mode_enabled(void);

/**
 * @brief Update the debug mode flag.
 *
 * Called once by the kernel after parsing system_conf.toml. Config dialog
 * edits the TOML file and reboots, so there is no runtime re-apply path.
 */
void fmrb_debug_mode_set(bool enabled);

#ifdef __cplusplus
}
#endif
