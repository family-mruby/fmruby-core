#pragma once

#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of HID devices the host tracks concurrently. */
#define USB_TASK_MAX_DEVICES 4

/* Device type categories exposed to user code (Ruby / About dialog).
 * Decouples callers from the underlying HID protocol numbering.
 * Prefixed to avoid collision with ESP-IDF usb_types_stack.h. */
typedef enum {
    FMRB_USB_DEV_TYPE_NONE     = 0,
    FMRB_USB_DEV_TYPE_KEYBOARD = 1,
    FMRB_USB_DEV_TYPE_MOUSE    = 2,
    FMRB_USB_DEV_TYPE_GAMEPAD  = 3,
    FMRB_USB_DEV_TYPE_OTHER    = 4,
} fmrb_usb_dev_type_t;

/* Public snapshot of a connected USB HID device. */
typedef struct {
    uint8_t  type;     /* fmrb_usb_dev_type_t */
    uint16_t vid;
    uint16_t pid;
    uint8_t  dev_addr; /* USB bus address (1..127); 0 if unknown */
} fmrb_usb_device_info_t;

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

/**
 * @brief Snapshot the currently connected HID device list
 *
 * Fills @p out with up to @p max_count entries describing devices that
 * are currently connected. Returns the number of entries written.
 * Safe to call from any task — takes the internal device list mutex
 * with a short timeout so it can be invoked from a Ruby VM context
 * without blocking the kernel for long. On contention or before USB
 * init the function returns 0.
 */
int usb_task_get_device_info(fmrb_usb_device_info_t *out, int max_count);

#ifdef __cplusplus
}
#endif
