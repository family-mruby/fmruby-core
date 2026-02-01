#include "usb_hid_conn_check.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"

#ifndef CONFIG_IDF_TARGET_LINUX

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

static const char *TAG = "usb_hid_conn";

// Maximum number of HID devices (interfaces) to support
#define MAX_HID_DEVICES 4

// HID device information structure
typedef struct {
    hid_host_device_handle_t handle;
    uint8_t proto;  // HID_PROTOCOL_KEYBOARD or HID_PROTOCOL_MOUSE
    bool connected;
} hid_device_info_t;

// Task and state
static fmrb_task_handle_t usb_task_handle = NULL;
static fmrb_task_handle_t hid_task_handle = NULL;
static volatile int usb_running = 0;
static volatile int usb_task_exited = 0;
static volatile int hid_task_exited = 0;

// HID devices array and connection flags
static hid_device_info_t hid_devices[MAX_HID_DEVICES];
static volatile int keyboard_connected = 0;
static volatile int mouse_connected = 0;

// Forward declarations
static void usb_host_lib_task(void *arg);
static void hid_host_task(void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                         const hid_host_interface_event_t event,
                                         void *arg);

/**
 * @brief Find device slot by handle
 */
static hid_device_info_t* find_device_slot(hid_host_device_handle_t handle)
{
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (hid_devices[i].connected && hid_devices[i].handle == handle) {
            return &hid_devices[i];
        }
    }
    return NULL;
}

/**
 * @brief Find empty device slot
 */
static hid_device_info_t* find_empty_slot(void)
{
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (!hid_devices[i].connected) {
            return &hid_devices[i];
        }
    }
    return NULL;
}

/**
 * @brief Remove device from tracking and update connection flags
 */
static void remove_device(hid_host_device_handle_t handle)
{
    hid_device_info_t* device = find_device_slot(handle);
    if (device != NULL) {
        // Update connection flags based on protocol
        if (device->proto == HID_PROTOCOL_KEYBOARD) {
            keyboard_connected = 0;
        } else if (device->proto == HID_PROTOCOL_MOUSE) {
            mouse_connected = 0;
        }
        device->handle = NULL;
        device->proto = 0;
        device->connected = false;
    }
}

/**
 * @brief Initialize all device slots
 */
static void init_device_slots(void)
{
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        hid_devices[i].handle = NULL;
        hid_devices[i].proto = 0;
        hid_devices[i].connected = false;
    }
    keyboard_connected = 0;
    mouse_connected = 0;
}

/**
 * @brief HID Host interface event callback
 *
 * Handles interface-level events including input reports and disconnection.
 */
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                         const hid_host_interface_event_t event,
                                         void *arg)
{
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            // Input report received - could process here if needed
            break;

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            FMRB_LOGW(TAG, "HID transfer error");
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            {
                hid_device_info_t* device = find_device_slot(hid_device_handle);
                if (device != NULL) {
                    FMRB_LOGI(TAG, "HID Device Disconnected (proto=%d)", device->proto);
                    hid_host_device_close(hid_device_handle);
                    remove_device(hid_device_handle);
                }
            }
            break;

        default:
            FMRB_LOGW(TAG, "Unknown HID interface event: %d", event);
            break;
    }
}

/**
 * @brief HID Host driver event callback
 *
 * Handles driver-level events (device connection).
 */
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                      const hid_host_driver_event_t event,
                                      void *arg)
{
    switch (event) {
        case HID_HOST_DRIVER_EVENT_CONNECTED:
            {
                // Find empty slot for this device
                hid_device_info_t* slot = find_empty_slot();
                if (slot == NULL) {
                    FMRB_LOGW(TAG, "No empty slot for HID device");
                    return;
                }

                // Get device info (VID/PID)
                hid_host_dev_info_t dev_info;
                if (hid_host_get_device_info(hid_device_handle, &dev_info) == ESP_OK) {
                    FMRB_LOGI(TAG, "HID Device Connected - VID: 0x%04X, PID: 0x%04X",
                              dev_info.VID, dev_info.PID);
                }

                // Get device params (protocol info)
                hid_host_dev_params_t dev_params;
                uint8_t proto = 0;
                if (hid_host_device_get_params(hid_device_handle, &dev_params) == ESP_OK) {
                    proto = dev_params.proto;
                    const char* proto_name = "Unknown";
                    if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                        if (proto == HID_PROTOCOL_KEYBOARD) {
                            proto_name = "Keyboard";
                            keyboard_connected = 1;
                        } else if (proto == HID_PROTOCOL_MOUSE) {
                            proto_name = "Mouse";
                            mouse_connected = 1;
                        }
                    } else {
                        proto_name = "Non-Boot";
                    }
                    FMRB_LOGI(TAG, "  Protocol: %s", proto_name);
                }

                // Register device in slot
                slot->handle = hid_device_handle;
                slot->proto = proto;
                slot->connected = true;

                // Open the device with interface callback for disconnect handling
                const hid_host_device_config_t dev_config = {
                    .callback = hid_host_interface_callback,
                    .callback_arg = NULL
                };
                ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
                ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
            }
            break;

        default:
            FMRB_LOGW(TAG, "Unknown HID driver event: %d", event);
            break;
    }
}

/**
 * @brief USB Host Library handling task
 *
 * This task handles USB Host Library events including device enumeration,
 * connection, and disconnection. Uses a finite timeout to allow clean shutdown.
 */
static void usb_host_lib_task(void *arg)
{
    FMRB_LOGI(TAG, "USB Host Library task started");

    while (usb_running) {
        uint32_t event_flags;
        // Use finite timeout (100ms) to periodically check usb_running flag
        esp_err_t ret = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);

        if (ret == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                FMRB_LOGI(TAG, "No more clients");
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                FMRB_LOGI(TAG, "All devices freed");
            }
        }
        // ESP_ERR_TIMEOUT is expected when no events occur within timeout
    }

    FMRB_LOGI(TAG, "USB Host Library task exiting");
    usb_task_exited = 1;
    fmrb_task_delete(NULL);
}

/**
 * @brief HID Host handling task
 *
 * This task processes HID Host events and handles device callbacks.
 */
static void hid_host_task(void *arg)
{
    FMRB_LOGI(TAG, "HID Host task started");

    while (usb_running) {
        // Process HID host events with timeout
        hid_host_handle_events(pdMS_TO_TICKS(100));

        // Periodic status log
        if (keyboard_connected || mouse_connected) {
            static int count = 0;
            if (++count >= 50) {  // Every 5 seconds
                FMRB_LOGI(TAG, "HID active - Keyboard:%d Mouse:%d",
                          keyboard_connected, mouse_connected);
                count = 0;
            }
        }
    }

    FMRB_LOGI(TAG, "HID Host task exiting");
    hid_task_exited = 1;
    fmrb_task_delete(NULL);
}

int usb_hid_conn_check_init(void)
{
    if (usb_running) {
        FMRB_LOGW(TAG, "USB HID already initialized");
        return 0;
    }

    // Initialize device slots
    init_device_slots();

    FMRB_LOGI(TAG, "Initializing USB Host...");

    // Install USB Host Library
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "USB Host install failed: %d", ret);
        return -1;
    }
    FMRB_LOGI(TAG, "USB Host Library installed");

    // Install HID Host driver
    const hid_host_driver_config_t hid_config = {
        .create_background_task = false,  // We handle events manually
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
        return -1;
    }
    FMRB_LOGI(TAG, "HID Host driver installed");

    return 0;
}

void usb_hid_conn_check_start(void)
{
    if (usb_running) {
        FMRB_LOGW(TAG, "USB HID tasks already running");
        return;
    }

    usb_running = 1;
    usb_task_exited = 0;
    hid_task_exited = 0;

    // Create USB host library task
    fmrb_base_type_t ret = fmrb_task_create_pinned(
        usb_host_lib_task,
        "usb_host_lib",
        4096,
        NULL,
        5,
        &usb_task_handle,
        0  // Core 0
    );
    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create USB host task");
        usb_running = 0;
        return;
    }

    // Create HID host task
    ret = fmrb_task_create_pinned(
        hid_host_task,
        "hid_host",
        4096,
        NULL,
        5,
        &hid_task_handle,
        0  // Core 0
    );
    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create HID host task");
        usb_running = 0;
        return;
    }

    FMRB_LOGI(TAG, "USB HID connection check started");
}

void usb_hid_conn_check_stop(void)
{
    if (!usb_running) {
        return;
    }

    FMRB_LOGI(TAG, "Stopping USB HID connection check...");
    usb_running = 0;

    // Wait for tasks to exit with timeout
    int timeout_count = 0;
    const int max_timeout = 30;  // 3 seconds max
    while ((!hid_task_exited || !usb_task_exited) && timeout_count < max_timeout) {
        fmrb_task_delay_ms(100);
        timeout_count++;
    }

    if (timeout_count >= max_timeout) {
        FMRB_LOGW(TAG, "Timeout waiting for tasks to exit (hid:%d, usb:%d)",
                  hid_task_exited, usb_task_exited);
    }

    // Close all HID devices that are still connected
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (hid_devices[i].connected && hid_devices[i].handle != NULL) {
            hid_host_device_stop(hid_devices[i].handle);
            hid_host_device_close(hid_devices[i].handle);
            hid_devices[i].handle = NULL;
            hid_devices[i].connected = false;
        }
    }
    keyboard_connected = 0;
    mouse_connected = 0;

    // Uninstall drivers in reverse order
    hid_host_uninstall();
    usb_host_uninstall();

    usb_task_handle = NULL;
    hid_task_handle = NULL;
    FMRB_LOGI(TAG, "USB HID connection check stopped");
}

int usb_hid_is_connected(void)
{
    return (keyboard_connected || mouse_connected) ? 1 : 0;
}

int usb_hid_is_keyboard_connected(void)
{
    return keyboard_connected;
}

int usb_hid_is_mouse_connected(void)
{
    return mouse_connected;
}

#else  // CONFIG_IDF_TARGET_LINUX

// Linux stub implementation
int usb_hid_conn_check_init(void) { return 0; }
void usb_hid_conn_check_start(void) {}
void usb_hid_conn_check_stop(void) {}
int usb_hid_is_connected(void) { return 0; }
int usb_hid_is_keyboard_connected(void) { return 0; }
int usb_hid_is_mouse_connected(void) { return 0; }

#endif // !CONFIG_IDF_TARGET_LINUX
