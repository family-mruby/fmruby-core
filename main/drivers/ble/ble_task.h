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
 * @brief Deinitialize BLE stack and stop advertising
 *
 * @return FMRB_OK on success
 */
fmrb_err_t ble_task_deinit(void);

#ifdef __cplusplus
}
#endif
