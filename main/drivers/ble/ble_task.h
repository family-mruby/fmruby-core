#pragma once

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
 * @brief Deinitialize BLE stack and stop advertising
 *
 * @return FMRB_OK on success
 */
fmrb_err_t ble_task_deinit(void);

#ifdef __cplusplus
}
#endif
