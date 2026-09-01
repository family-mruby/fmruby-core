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

#define GAMEPAD_MAX_AXES 6

typedef struct {
    uint16_t bit_offset;
    uint8_t bit_size;
    int16_t center;       // Center value (e.g. 128 for unsigned 0-255)
    bool found;
} hid_gamepad_axis_info_t;

typedef struct {
    bool valid;
    uint16_t vid;
    uint16_t pid;
    char name[32];
    uint8_t report_len;   // Total report length in bytes
    // Buttons bitmask field
    uint16_t buttons_bit_offset;
    uint8_t buttons_bit_size;
    // HAT switch field
    bool has_hat;
    uint16_t hat_bit_offset;
    uint8_t hat_bit_size;
    // Axes: left_x, left_y, right_x, right_y, l2, r2
    hid_gamepad_axis_info_t axes[GAMEPAD_MAX_AXES];
} hid_gamepad_report_layout_t;

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
 * Look up a gamepad layout by VID/PID.
 *
 * @param vid           USB Vendor ID
 * @param pid           USB Product ID
 * @param layout_out    Output: gamepad report layout
 * @return true if a matching entry was found
 */
bool hid_device_config_find_gamepad(uint16_t vid, uint16_t pid,
                                     hid_gamepad_report_layout_t *layout_out);

/**
 * Check if control transfers should be skipped for this device.
 *
 * @param vid  USB Vendor ID
 * @param pid  USB Product ID
 * @return true if skip_control_transfer is set for this device
 */
bool hid_device_config_skip_control_transfer(uint16_t vid, uint16_t pid);

/**
 * Whether this device asked for Report Protocol (protocol = "report").
 *
 * A Boot Interface mouse reports 3 bytes and has no wheel in them, so a
 * device whose wheel is wanted has to be asked for its own format. Only a
 * device named in the config file is asked; the default stays Boot.
 *
 * @param vid USB Vendor ID
 * @param pid USB Product ID
 * @return true if the entry says protocol = "report"
 */
bool hid_device_config_wants_report_protocol(uint16_t vid, uint16_t pid);


#ifdef __cplusplus
}
#endif

#endif // HID_DEVICE_CONFIG_H
