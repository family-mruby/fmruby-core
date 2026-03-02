#include <stdio.h>
#include <string.h>

#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "usb_task.h"
#include "host_task.h"
#include "boot.h"
#include "fmrb_kernel.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

static const char *TAG = "usb_task";

#define MAX_HID_DEVICES 4

typedef struct {
    hid_host_device_handle_t handle;
    uint8_t proto;
    bool connected;
} hid_device_info_t;

// Task handles and state
static fmrb_task_handle_t g_usb_lib_task_handle = NULL;
static fmrb_task_handle_t g_hid_task_handle = NULL;
static volatile int g_usb_running = 0;
static volatile int g_usb_lib_task_exited = 0;
static volatile int g_hid_task_exited = 0;

// Device tracking
static hid_device_info_t g_hid_devices[MAX_HID_DEVICES];

// Keyboard state
static hid_keyboard_input_report_boot_t g_prev_kbd_report;

// Mouse state
static int g_cursor_x = 0;
static int g_cursor_y = 0;
static uint8_t g_prev_mouse_buttons = 0;
static int g_screen_width = 0;
static int g_screen_height = 0;

// Forward declarations
static void usb_host_lib_task(void *arg);
static void hid_host_task(void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                         const hid_host_interface_event_t event,
                                         void *arg);

static void init_device_slots(void)
{
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        g_hid_devices[i].handle = NULL;
        g_hid_devices[i].proto = 0;
        g_hid_devices[i].connected = false;
    }
}

static hid_device_info_t* find_device_slot(hid_host_device_handle_t handle)
{
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (g_hid_devices[i].connected && g_hid_devices[i].handle == handle) {
            return &g_hid_devices[i];
        }
    }
    return NULL;
}

static hid_device_info_t* find_empty_slot(void)
{
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (!g_hid_devices[i].connected) {
            return &g_hid_devices[i];
        }
    }
    return NULL;
}

static void remove_device(hid_host_device_handle_t handle)
{
    hid_device_info_t* device = find_device_slot(handle);
    if (device != NULL) {
        device->handle = NULL;
        device->proto = 0;
        device->connected = false;
    }
}

static bool key_in_report(uint8_t key, const hid_keyboard_input_report_boot_t *report)
{
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (report->key[i] == key) {
            return true;
        }
    }
    return false;
}

static void process_keyboard_report(const uint8_t *data, size_t len)
{
    if (len < sizeof(hid_keyboard_input_report_boot_t)) {
        return;
    }

    const hid_keyboard_input_report_boot_t *report = (const hid_keyboard_input_report_boot_t *)data;
    uint8_t modifier = report->modifier.val;

    // Detect modifier changes and generate events for individual modifier bits
    uint8_t mod_changed = modifier ^ g_prev_kbd_report.modifier.val;
    if (mod_changed) {
        static const uint8_t mod_scancodes[] = {
            HID_KEY_LEFT_CONTROL, HID_KEY_LEFT_SHIFT, HID_KEY_LEFT_ALT, HID_KEY_LEFT_GUI,
            HID_KEY_RIGHT_CONTROL, HID_KEY_RIGHT_SHIFT, HID_KEY_RIGHT_ALT, HID_KEY_RIGHT_GUI,
        };
        for (int i = 0; i < 8; i++) {
            if (mod_changed & (1 << i)) {
                if (modifier & (1 << i)) {
                    fmrb_host_send_key_down(mod_scancodes[i], mod_scancodes[i], modifier);
                } else {
                    fmrb_host_send_key_up(mod_scancodes[i], mod_scancodes[i], modifier);
                }
            }
        }
    }

    // Detect key releases (was in prev report, not in current)
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        uint8_t prev_key = g_prev_kbd_report.key[i];
        if (prev_key != HID_KEY_NO_PRESS && !key_in_report(prev_key, report)) {
            FMRB_LOGD(TAG, "Key UP: scancode=0x%02X modifier=0x%02X", prev_key, modifier);
            fmrb_host_send_key_up(prev_key, prev_key, modifier);
        }
    }

    // Detect key presses (in current report, not in prev)
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        uint8_t cur_key = report->key[i];
        if (cur_key != HID_KEY_NO_PRESS && !key_in_report(cur_key, &g_prev_kbd_report)) {
            FMRB_LOGD(TAG, "Key DOWN: scancode=0x%02X modifier=0x%02X", cur_key, modifier);
            fmrb_host_send_key_down(cur_key, cur_key, modifier);
        }
    }

    memcpy(&g_prev_kbd_report, report, sizeof(hid_keyboard_input_report_boot_t));
}

static bool ensure_screen_size(void)
{
    if (g_screen_width > 0 && g_screen_height > 0) {
        return true;
    }
    if (!fmrb_kernel_is_ready()) {
        return false;
    }
    const fmrb_system_config_t* conf = fmrb_kernel_get_config();
    if (conf && conf->display_width > 0 && conf->display_height > 0) {
        g_screen_width = conf->display_width;
        g_screen_height = conf->display_height;
        FMRB_LOGI(TAG, "Screen size acquired: %dx%d", g_screen_width, g_screen_height);
        return true;
    }
    return false;
}

static void process_mouse_report(const uint8_t *data, size_t len)
{
    if (len < sizeof(hid_mouse_input_report_boot_t)) {
        return;
    }

    if (!ensure_screen_size()) {
        return;
    }

    const hid_mouse_input_report_boot_t *report = (const hid_mouse_input_report_boot_t *)data;

    // Update absolute cursor position from relative displacement
    int dx = report->x_displacement;
    int dy = report->y_displacement;

    if (dx != 0 || dy != 0) {
        g_cursor_x += dx;
        g_cursor_y += dy;

        // Clamp to screen bounds
        if (g_cursor_x < 0) g_cursor_x = 0;
        if (g_cursor_y < 0) g_cursor_y = 0;
        if (g_cursor_x >= g_screen_width) g_cursor_x = g_screen_width - 1;
        if (g_cursor_y >= g_screen_height) g_cursor_y = g_screen_height - 1;

        fmrb_host_send_mouse_move(g_cursor_x, g_cursor_y);
    }

    // Check button changes
    uint8_t buttons = report->buttons.val;
    uint8_t changed = buttons ^ g_prev_mouse_buttons;

    if (changed) {
        for (int btn = 0; btn < 3; btn++) {
            if (changed & (1 << btn)) {
                int state = (buttons & (1 << btn)) ? 1 : 0;
                FMRB_LOGD(TAG, "Mouse button %d %s at (%d,%d)",
                         btn + 1, state ? "pressed" : "released",
                         g_cursor_x, g_cursor_y);
                fmrb_host_send_mouse_click(g_cursor_x, g_cursor_y, btn + 1, state);
            }
        }
        g_prev_mouse_buttons = buttons;
    }
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                         const hid_host_interface_event_t event,
                                         void *arg)
{
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
            uint8_t report_data[64];
            size_t report_len = 0;
            esp_err_t ret = hid_host_device_get_raw_input_report_data(
                hid_device_handle, report_data, sizeof(report_data), &report_len);
            if (ret != ESP_OK) {
                FMRB_LOGW(TAG, "Failed to get input report: %d", ret);
                break;
            }

            hid_device_info_t* device = find_device_slot(hid_device_handle);
            if (device == NULL) {
                break;
            }

            if (device->proto == HID_PROTOCOL_KEYBOARD) {
                process_keyboard_report(report_data, report_len);
            } else if (device->proto == HID_PROTOCOL_MOUSE) {
                process_mouse_report(report_data, report_len);
            }
            break;
        }

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            FMRB_LOGW(TAG, "HID transfer error");
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED: {
            hid_device_info_t* device = find_device_slot(hid_device_handle);
            if (device != NULL) {
                FMRB_LOGI(TAG, "HID Device Disconnected (proto=%d)", device->proto);
                hid_host_device_close(hid_device_handle);
                remove_device(hid_device_handle);
            }
            break;
        }

        default:
            FMRB_LOGW(TAG, "Unknown HID interface event: %d", event);
            break;
    }
}

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                      const hid_host_driver_event_t event,
                                      void *arg)
{
    switch (event) {
        case HID_HOST_DRIVER_EVENT_CONNECTED: {
            hid_device_info_t* slot = find_empty_slot();
            if (slot == NULL) {
                FMRB_LOGW(TAG, "No empty slot for HID device");
                return;
            }

            hid_host_dev_info_t dev_info;
            if (hid_host_get_device_info(hid_device_handle, &dev_info) == ESP_OK) {
                FMRB_LOGI(TAG, "HID Device Connected - VID: 0x%04X, PID: 0x%04X",
                          dev_info.VID, dev_info.PID);
            }

            hid_host_dev_params_t dev_params;
            uint8_t proto = 0;
            if (hid_host_device_get_params(hid_device_handle, &dev_params) == ESP_OK) {
                proto = dev_params.proto;
                const char* proto_name = "Unknown";
                if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                    if (proto == HID_PROTOCOL_KEYBOARD) {
                        proto_name = "Keyboard";
                    } else if (proto == HID_PROTOCOL_MOUSE) {
                        proto_name = "Mouse";
                    }
                } else {
                    proto_name = "Non-Boot";
                }
                FMRB_LOGI(TAG, "  Protocol: %s (sub_class=%d, proto=%d)", proto_name, dev_params.sub_class, proto);
            }

            slot->handle = hid_device_handle;
            slot->proto = proto;
            slot->connected = true;

            const hid_host_device_config_t dev_config = {
                .callback = hid_host_interface_callback,
                .callback_arg = NULL
            };
            ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
            ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
            break;
        }

        default:
            FMRB_LOGW(TAG, "Unknown HID driver event: %d", event);
            break;
    }
}

static void usb_host_lib_task(void *arg)
{
    FMRB_LOGI(TAG, "USB Host Library task started");

    while (g_usb_running) {
        uint32_t event_flags;
        esp_err_t ret = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        if (ret == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                FMRB_LOGI(TAG, "No more clients");
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                FMRB_LOGI(TAG, "All devices freed");
            }
        }
    }

    FMRB_LOGI(TAG, "USB Host Library task exiting");
    g_usb_lib_task_exited = 1;
    fmrb_task_delete(NULL);
}

static void hid_host_task(void *arg)
{
    FMRB_LOGI(TAG, "HID Host task started");

    while (g_usb_running) {
        hid_host_handle_events(pdMS_TO_TICKS(100));
    }

    FMRB_LOGI(TAG, "HID Host task exiting");
    g_hid_task_exited = 1;
    fmrb_task_delete(NULL);
}

fmrb_err_t usb_task_init(void)
{
    if (g_usb_running) {
        FMRB_LOGW(TAG, "USB task already initialized");
        return FMRB_OK;
    }

    init_device_slots();
    memset(&g_prev_kbd_report, 0, sizeof(g_prev_kbd_report));
    g_cursor_x = 0;
    g_cursor_y = 0;
    g_prev_mouse_buttons = 0;
    g_screen_width = 0;
    g_screen_height = 0;

    FMRB_LOGI(TAG, "Initializing USB Host...");

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "USB Host install failed: %d", ret);
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "USB Host Library installed");

    const hid_host_driver_config_t hid_config = {
        .create_background_task = false,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };
    ret = hid_host_install(&hid_config);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "HID Host install failed: %d", ret);
        usb_host_uninstall();
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "HID Host driver installed");

    usb_task_start();
    return FMRB_OK;
}

void usb_task_start(void)
{
    if (g_usb_running) {
        FMRB_LOGW(TAG, "USB tasks already running");
        return;
    }

    g_usb_running = 1;
    g_usb_lib_task_exited = 0;
    g_hid_task_exited = 0;

    fmrb_base_type_t ret = fmrb_task_create_pinned(
        usb_host_lib_task,
        "usb_host_lib",
        4096,
        NULL,
        5,
        &g_usb_lib_task_handle,
        0
    );
    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create USB host lib task");
        g_usb_running = 0;
        return;
    }

    ret = fmrb_task_create_pinned(
        hid_host_task,
        "hid_host",
        4096,
        NULL,
        5,
        &g_hid_task_handle,
        0
    );
    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create HID host task");
        g_usb_running = 0;
        return;
    }

    FMRB_LOGI(TAG, "USB HID Host tasks started");
}

void usb_task_stop(void)
{
    if (!g_usb_running) {
        return;
    }

    FMRB_LOGI(TAG, "Stopping USB HID Host...");
    g_usb_running = 0;

    int timeout_count = 0;
    const int max_timeout = 30;
    while ((!g_hid_task_exited || !g_usb_lib_task_exited) && timeout_count < max_timeout) {
        fmrb_task_delay_ms(100);
        timeout_count++;
    }

    if (timeout_count >= max_timeout) {
        FMRB_LOGW(TAG, "Timeout waiting for tasks to exit (hid:%d, usb:%d)",
                  g_hid_task_exited, g_usb_lib_task_exited);
    }

    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (g_hid_devices[i].connected && g_hid_devices[i].handle != NULL) {
            hid_host_device_stop(g_hid_devices[i].handle);
            hid_host_device_close(g_hid_devices[i].handle);
            g_hid_devices[i].handle = NULL;
            g_hid_devices[i].connected = false;
        }
    }

    hid_host_uninstall();
    usb_host_uninstall();

    g_usb_lib_task_handle = NULL;
    g_hid_task_handle = NULL;
    FMRB_LOGI(TAG, "USB HID Host stopped");
}
