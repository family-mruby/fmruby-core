#ifndef HID_DEVICE_CONFIG_H
#define HID_DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "hid_report_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HID_DEVICE_CONFIG_PATH "/etc/hid_devices.toml"
#define HID_DEVICE_CONFIG_MAX_ENTRIES 16

/**
 * Initialize HID device configuration from TOML file.
 * Must be called after filesystem is mounted and before usb_task_init().
 * If the config file does not exist, logs a warning and continues.
 */
void hid_device_config_init(void);

/**
 * Look up a mouse layout by VID/PID.
 *
 * @param vid           USB Vendor ID
 * @param pid           USB Product ID
 * @param layout_out    Output: mouse report layout
 * @param copy_len_out  Output: report copy length (report_len + 1 if report_id)
 * @return true if a matching entry was found
 */
bool hid_device_config_find_mouse(uint16_t vid, uint16_t pid,
                                   hid_mouse_report_layout_t *layout_out,
                                   uint8_t *copy_len_out);

/**
 * Check if control transfers should be skipped for this device.
 *
 * @param vid  USB Vendor ID
 * @param pid  USB Product ID
 * @return true if skip_control_transfer is set for this device
 */
bool hid_device_config_skip_control_transfer(uint16_t vid, uint16_t pid);

#ifdef __cplusplus
}
#endif

#endif // HID_DEVICE_CONFIG_H
