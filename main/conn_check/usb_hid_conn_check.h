#pragma once

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB HID Host
 * @return 0 on success, -1 on failure
 */
int usb_hid_conn_check_init(void);

/**
 * @brief Start USB HID connection check task
 */
void usb_hid_conn_check_start(void);

/**
 * @brief Stop USB HID connection check
 */
void usb_hid_conn_check_stop(void);

/**
 * @brief Check if USB HID device is connected
 * @return 1 if connected, 0 otherwise
 */
int usb_hid_is_connected(void);

#ifdef __cplusplus
}
#endif
