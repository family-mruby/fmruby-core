#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB Host and start HID input processing
 *
 * The root port is left unpowered at install time so that bus-powered
 * hubs do not see the host probing them before Vbus is stable. Call
 * usb_task_power_on_root_port() after Vbus has been raised and given
 * time to stabilize to start enumeration.
 */
fmrb_err_t usb_task_init(void);

/**
 * @brief Power on the USB root port to trigger device enumeration
 *
 * Must be called after usb_task_init() and after the external Vbus
 * (FMRB_PIN_USB_POWER) has been raised. A short settle delay between
 * Vbus HIGH and this call helps bus-powered hubs enumerate reliably.
 */
fmrb_err_t usb_task_power_on_root_port(void);

/**
 * @brief Start USB Host tasks
 */
void usb_task_start(void);

/**
 * @brief Stop USB Host tasks and release resources
 */
void usb_task_stop(void);

#ifdef __cplusplus
}
#endif
