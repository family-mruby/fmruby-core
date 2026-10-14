#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize BLE stack (NimBLE) and start advertising
 *
 * The device will be discoverable as "FamilyMruby".
 *
 * @return FMRB_OK on success, FMRB_ERR_FAILED on initialization failure
 */
fmrb_err_t ble_task_init(void);

#ifdef CONFIG_IDF_TARGET_ESP32P4
/**
 * @brief Bring the C6 link up without offering any BLE service (Modern only).
 *
 * On esp_hosted 1.4.0 the SDIO link to the C6 is established by the transport
 * init inside nimble_port_init(), and nothing else brings it up -- so WiFi
 * cannot start unless that ran. This does exactly that much and stops: no GATT
 * services, no host task, no advertising, so the device is not connectable and
 * ble_ui_state() keeps reporting "off".
 *
 * Used when ble_auto_start is false but WiFi is wanted. Terminal until reboot:
 * ble_task_init() must not run afterwards (nimble_port_init is not re-entrant).
 *
 * @return FMRB_OK when the link is up, FMRB_ERR_FAILED otherwise
 */
fmrb_err_t ble_link_only_init(void);
#endif

/**
 * @brief Start the BLE service asynchronously (idempotent)
 *
 * Spawns a one-shot task that runs ble_task_init(), so the caller (boot when
 * ble_auto_start is set, or the desktop menu for a manual start) never carries
 * the init frames. Safe to call repeatedly; a second call while BLE is up or
 * starting is a logged no-op.
 *
 * @return FMRB_OK when BLE is running/starting, FMRB_ERR_FAILED otherwise
 */
fmrb_err_t ble_service_start(void);

/**
 * @brief True while BLE is running or its start task is in flight.
 *
 * Used by wifi_task_init for the retro one-radio-at-a-time policy.
 */
bool ble_service_is_started(void);

/**
 * @brief BLE state for the menu-bar indicator.
 *
 * @return 0 = BLE off, 1 = enabled and waiting for a central,
 *         2 = a central is connected. Allocation- and lock-free.
 */
int ble_ui_state(void);

/**
 * @brief Open (seconds > 0) or close (0) the settings window.
 *
 * While it is open the BLE file service accepts writes, deletes and renames
 * of the connection settings and secrets (WiFi, the API keys in
 * secrets.toml, Asterism config and credentials; fmrb_secret_path.h), and
 * reads of the WiFi password and the API keys. Reading the Asterism keys
 * stays refused.
 * Opening again restarts the countdown. Called from the desktop menu.
 *
 * @return FMRB_OK, FMRB_ERR_INVALID_STATE when BLE is not running (the
 *         window would be of no use), FMRB_ERR_NOT_SUPPORTED without BLE.
 */
fmrb_err_t ble_settings_window_set(uint32_t seconds);

/**
 * @brief Boot into the setup mode (doc/ruby_asterism/report/cf3.md).
 *
 * Starts BLE (ble_service_start) and holds the settings window open until
 * the board restarts. While it is held the file service also takes the
 * console's restart command, and a press of the button (active low; -1 for
 * none) restarts the board. Called once by boot instead of starting WiFi.
 *
 * @return FMRB_OK when BLE is starting, FMRB_ERR_* otherwise
 */
fmrb_err_t ble_setup_mode_start(int button_gpio);

/**
 * @brief Give BLE's static memory to the heap for the rest of this boot.
 *
 * For a board that decides at boot whether BLE runs (the setup mode): on a
 * normal boot the controller and NimBLE never start, so their data and BSS
 * and the ROM controller's DRAM become heap. BLE cannot start again until
 * the next boot; ble_service_start() refuses after this.
 *
 * @return FMRB_OK, FMRB_ERR_INVALID_STATE when BLE has been started,
 *         FMRB_ERR_NOT_SUPPORTED without BLE
 */
fmrb_err_t ble_release_memory(void);

/**
 * @brief Seconds left in the settings window, 0 when it is closed, and
 *        UINT32_MAX while the setup mode holds it open. Allocation- and
 *        lock-free (the desktop polls it once a second).
 */
uint32_t ble_settings_window_left(void);

/**
 * @brief Deinitialize BLE stack and stop advertising
 *
 * @return FMRB_OK on success
 */
fmrb_err_t ble_task_deinit(void);

#ifdef __cplusplus
}
#endif
