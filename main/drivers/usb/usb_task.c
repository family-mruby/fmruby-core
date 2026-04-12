#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"
#include "usb_task.h"
#include "host_task.h"
#include "boot.h"
#include "fmrb_kernel.h"
#include "status_led.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"
#include "hid_report_parser.h"
#include "hid_device_config.h"

static const char *TAG = "usb_task";

#define MAX_HID_DEVICES 4
#define MAX_GAMEPAD_DEVICES 2
#define MAX_GAMEPAD_BUTTONS 16
#define MAX_GAMEPAD_AXES 6
#define HID_CALLBACK_MUTEX_TIMEOUT_MS 10  // Timeout for mutex in callbacks (10ms)

// Custom protocol ID for gamepad (not in standard HID protocol enum)
#define HID_PROTOCOL_GAMEPAD 0xFF

// Vendor/Product IDs for supported gamepads
#define VID_SONY 0x054C
#define PID_PS5_DUALSENSE 0x0CE6
#define VID_LOGITECH 0x046D
#define PID_LOGITECH_F310 0xC216
#define PID_LOGITECH_F710 0xC219

// Vendor/Product IDs for devices with known control transfer issues
#define VID_SIPEED 0x359F
#define PID_NANOKVM_USB 0x3302

// Gamepad axis indices
#define GAMEPAD_AXIS_LEFT_X 0
#define GAMEPAD_AXIS_LEFT_Y 1
#define GAMEPAD_AXIS_RIGHT_X 2
#define GAMEPAD_AXIS_RIGHT_Y 3
#define GAMEPAD_AXIS_L2 4
#define GAMEPAD_AXIS_R2 5

typedef struct {
    hid_host_device_handle_t handle;
    uint8_t proto;
    bool connected;
    uint16_t vid;  // Vendor ID
    uint16_t pid;  // Product ID
    uint8_t dev_addr;  // USB device address (to identify sibling interfaces)
    uint8_t report_copy_len;  // Bytes to copy from report (device-type specific)
    bool dump_mode;  // True when descriptor fetch failed - dump raw reports for debugging
    uint8_t dump_count;  // Number of reports dumped so far (to limit output)
    uint32_t generation;  // Generation counter (incremented on connect/disconnect)
    // Per-device state
    hid_keyboard_input_report_boot_t prev_kbd_report;
    struct {
        int cursor_x;
        int cursor_y;
        double accum_x;  // Fractional accumulator for scaled X
        double accum_y;  // Fractional accumulator for scaled Y
        uint8_t prev_buttons;
        bool initialized;  // True after first screen size is known
    } mouse_state;
    struct {
        int gamepad_id;  // 0 or 1
        uint16_t prev_buttons;  // Button state bitmask (16 bits)
        int16_t prev_axes[MAX_GAMEPAD_AXES];  // Previous axis values
    } gamepad_state;
    hid_mouse_report_layout_t report_layout;  // Parsed HID report layout for mouse
} hid_device_info_t;

// Task handles and state
static fmrb_task_handle_t g_usb_lib_task_handle = NULL;
static fmrb_task_handle_t g_hid_task_handle = NULL;
static volatile int g_usb_running = 0;
static volatile int g_usb_lib_task_exited = 0;
static volatile int g_hid_task_exited = 0;

// Device tracking
static hid_device_info_t g_hid_devices[MAX_HID_DEVICES];
static fmrb_semaphore_t g_hid_devices_mutex = NULL;

// Input report structure for queueing raw reports from callback
typedef struct {
    int8_t slot_index;  // Device slot index (0-3), -1 if invalid
    uint32_t generation;  // Generation counter (to detect stale reports)
    uint8_t report_data[64];  // Reduced from 128 (max is PS5=64B)
    uint8_t report_len;  // Actual length copied
} hid_input_report_t;

// Input report ring buffer (for deferred processing from callbacks)
#define INPUT_REPORT_QUEUE_SIZE 16  // Large enough to avoid overflow under normal load
static hid_input_report_t g_input_reports[INPUT_REPORT_QUEUE_SIZE];
static volatile int g_input_report_head = 0;  // Write index
static volatile int g_input_report_tail = 0;  // Read index
static volatile bool g_input_report_overflow = false;  // Ring overflow flag
static fmrb_spinlock_t g_input_report_spinlock = FMRB_SPINLOCK_INITIALIZER;

// Pending disconnect item with generation (to prevent slot reuse issues)
typedef struct {
    int8_t slot_index;   // Device slot index (0-3), -1 if invalid
    uint32_t generation; // Generation counter (must match to process)
} pending_disconnect_item_t;

// Pending disconnect ring buffer (for deferred cleanup from callbacks)
#define PENDING_DISCONNECT_QUEUE_SIZE 8  // Larger than MAX_HID_DEVICES to avoid overflow
static pending_disconnect_item_t g_pending_disconnects[PENDING_DISCONNECT_QUEUE_SIZE];
static volatile int g_pending_disconnect_head = 0;  // Write index
static volatile int g_pending_disconnect_tail = 0;  // Read index
static volatile bool g_pending_disconnect_overflow = false;  // Ring overflow flag
static volatile bool g_pending_disconnect_cleanup_needed = false;  // Full scan needed (mutex fail)
static fmrb_spinlock_t g_pending_disconnect_spinlock = FMRB_SPINLOCK_INITIALIZER;

// Pending protocol setup item (deferred from callback to task context)
// Used for both Boot devices (SET_PROTOCOL) and Non-boot mice (descriptor parse)
typedef struct {
    int8_t slot_index;   // Device slot index (0-3), -1 if invalid
    uint32_t generation; // Generation counter (must match to process)
    bool is_boot;        // true if Boot Interface device (needs SET_PROTOCOL(Boot))
    uint8_t proto;       // HID_PROTOCOL_KEYBOARD or HID_PROTOCOL_MOUSE
} pending_protocol_setup_item_t;

// Pending protocol setup ring buffer
#define PENDING_PROTOCOL_SETUP_QUEUE_SIZE 8
static pending_protocol_setup_item_t g_pending_protocol_setups[PENDING_PROTOCOL_SETUP_QUEUE_SIZE];
static volatile int g_pending_protocol_setup_head = 0;
static volatile int g_pending_protocol_setup_tail = 0;
static fmrb_spinlock_t g_pending_protocol_setup_spinlock = FMRB_SPINLOCK_INITIALIZER;

static void pending_protocol_setup_push(int8_t slot_index, uint32_t generation, bool is_boot, uint8_t proto)
{
    fmrb_enter_critical(&g_pending_protocol_setup_spinlock);
    int next_head = (g_pending_protocol_setup_head + 1) % PENDING_PROTOCOL_SETUP_QUEUE_SIZE;
    if (next_head != g_pending_protocol_setup_tail) {
        g_pending_protocol_setups[g_pending_protocol_setup_head].slot_index = slot_index;
        g_pending_protocol_setups[g_pending_protocol_setup_head].generation = generation;
        g_pending_protocol_setups[g_pending_protocol_setup_head].is_boot = is_boot;
        g_pending_protocol_setups[g_pending_protocol_setup_head].proto = proto;
        g_pending_protocol_setup_head = next_head;
    }
    fmrb_exit_critical(&g_pending_protocol_setup_spinlock);
}

static bool pending_protocol_setup_pop(pending_protocol_setup_item_t *item)
{
    fmrb_enter_critical(&g_pending_protocol_setup_spinlock);
    if (g_pending_protocol_setup_tail == g_pending_protocol_setup_head) {
        fmrb_exit_critical(&g_pending_protocol_setup_spinlock);
        return false;
    }
    *item = g_pending_protocol_setups[g_pending_protocol_setup_tail];
    g_pending_protocol_setup_tail = (g_pending_protocol_setup_tail + 1) % PENDING_PROTOCOL_SETUP_QUEUE_SIZE;
    fmrb_exit_critical(&g_pending_protocol_setup_spinlock);
    return true;
}

// Screen dimensions (shared for all mice)
static int g_screen_width = 0;
static int g_screen_height = 0;
static double g_mouse_scale_x = 1.0;
static double g_mouse_scale_y = 1.0;

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
        g_hid_devices[i].vid = 0;
        g_hid_devices[i].pid = 0;
        g_hid_devices[i].report_copy_len = 0;
        g_hid_devices[i].generation = 0;
        memset(&g_hid_devices[i].prev_kbd_report, 0, sizeof(hid_keyboard_input_report_boot_t));
        g_hid_devices[i].mouse_state.cursor_x = 0;
        g_hid_devices[i].mouse_state.cursor_y = 0;
        g_hid_devices[i].mouse_state.accum_x = 0.0;
        g_hid_devices[i].mouse_state.accum_y = 0.0;
        g_hid_devices[i].mouse_state.prev_buttons = 0;
        g_hid_devices[i].mouse_state.initialized = false;
        g_hid_devices[i].gamepad_state.gamepad_id = -1;
        g_hid_devices[i].gamepad_state.prev_buttons = 0;
        memset(g_hid_devices[i].gamepad_state.prev_axes, 0, sizeof(g_hid_devices[i].gamepad_state.prev_axes));
    }
}

// Push input report to ring buffer (called from callback - must be fast)
// IMPORTANT: No logging in this function (avoid callback bloat)
static void input_report_push(int8_t slot_index, uint32_t generation, const uint8_t *data, uint8_t len)
{
    // Critical section: update ring buffer atomically
    fmrb_enter_critical(&g_input_report_spinlock);

    int next_head = (g_input_report_head + 1) % INPUT_REPORT_QUEUE_SIZE;
    if (next_head == g_input_report_tail) {
        // Ring overflow - set flag (no logging here!)
        g_input_report_overflow = true;
    } else {
        // Copy report data to ring buffer
        g_input_reports[g_input_report_head].slot_index = slot_index;
        g_input_reports[g_input_report_head].generation = generation;
        g_input_reports[g_input_report_head].report_len = len;
        if (len > 0 && len <= sizeof(g_input_reports[g_input_report_head].report_data)) {
            memcpy(g_input_reports[g_input_report_head].report_data, data, len);
        }
        g_input_report_head = next_head;
    }

    fmrb_exit_critical(&g_input_report_spinlock);
}

// Pop input report from ring buffer (called from task context)
// Returns true if report was popped, false if ring is empty
static bool input_report_pop(hid_input_report_t *out_report)
{
    bool has_report = false;

    fmrb_enter_critical(&g_input_report_spinlock);

    if (g_input_report_tail != g_input_report_head) {
        // Copy report data from ring buffer
        memcpy(out_report, &g_input_reports[g_input_report_tail], sizeof(hid_input_report_t));
        g_input_report_tail = (g_input_report_tail + 1) % INPUT_REPORT_QUEUE_SIZE;
        has_report = true;
    }

    fmrb_exit_critical(&g_input_report_spinlock);

    return has_report;
}

// Check and clear input report overflow flag (called from task context)
static bool input_report_check_overflow(void)
{
    bool overflow = false;

    fmrb_enter_critical(&g_input_report_spinlock);
    overflow = g_input_report_overflow;
    g_input_report_overflow = false;
    fmrb_exit_critical(&g_input_report_spinlock);

    return overflow;
}

// Push slot_index+generation to pending disconnect ring (called from callback - must be fast)
// IMPORTANT: No logging in this function (avoid callback bloat)
static void pending_disconnect_push(int8_t slot_index, uint32_t generation)
{
    // Critical section: update ring buffer atomically
    fmrb_enter_critical(&g_pending_disconnect_spinlock);

    int next_head = (g_pending_disconnect_head + 1) % PENDING_DISCONNECT_QUEUE_SIZE;
    if (next_head == g_pending_disconnect_tail) {
        // Ring overflow - set flag to trigger full scan (no logging here!)
        g_pending_disconnect_overflow = true;
    } else {
        g_pending_disconnects[g_pending_disconnect_head].slot_index = slot_index;
        g_pending_disconnects[g_pending_disconnect_head].generation = generation;
        g_pending_disconnect_head = next_head;
    }

    fmrb_exit_critical(&g_pending_disconnect_spinlock);
}

// Pop disconnect item from pending disconnect ring (called from task context)
// Returns true if item was popped, false if ring is empty
static bool pending_disconnect_pop(pending_disconnect_item_t *out_item)
{
    bool has_item = false;

    fmrb_enter_critical(&g_pending_disconnect_spinlock);

    if (g_pending_disconnect_tail != g_pending_disconnect_head) {
        *out_item = g_pending_disconnects[g_pending_disconnect_tail];
        g_pending_disconnect_tail = (g_pending_disconnect_tail + 1) % PENDING_DISCONNECT_QUEUE_SIZE;
        has_item = true;
    }

    fmrb_exit_critical(&g_pending_disconnect_spinlock);

    return has_item;
}

// Check and clear overflow flag (called from task context)
static bool pending_disconnect_check_overflow(void)
{
    bool overflow = false;

    fmrb_enter_critical(&g_pending_disconnect_spinlock);
    overflow = g_pending_disconnect_overflow;
    g_pending_disconnect_overflow = false;
    fmrb_exit_critical(&g_pending_disconnect_spinlock);

    return overflow;
}

// Check and clear cleanup needed flag (called from task context)
static bool pending_disconnect_check_cleanup_needed(void)
{
    bool cleanup = false;

    fmrb_enter_critical(&g_pending_disconnect_spinlock);
    cleanup = g_pending_disconnect_cleanup_needed;
    g_pending_disconnect_cleanup_needed = false;
    fmrb_exit_critical(&g_pending_disconnect_spinlock);

    return cleanup;
}

// Request full cleanup scan (called from callback - must be fast)
static void pending_disconnect_request_cleanup(void)
{
    fmrb_enter_critical(&g_pending_disconnect_spinlock);
    g_pending_disconnect_cleanup_needed = true;
    fmrb_exit_critical(&g_pending_disconnect_spinlock);
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

// Clear a device slot (slot direct access version)
static void clear_slot(hid_device_info_t* device)
{
    device->generation++;  // Increment generation on disconnect (invalidates queued reports)
    device->handle = NULL;
    device->proto = 0;
    device->connected = false;
    device->vid = 0;
    device->pid = 0;
    device->dev_addr = 0;
    device->report_copy_len = 0;
    device->dump_mode = false;
    device->dump_count = 0;
    memset(&device->prev_kbd_report, 0, sizeof(hid_keyboard_input_report_boot_t));
    device->mouse_state.cursor_x = 0;
    device->mouse_state.cursor_y = 0;
    device->mouse_state.accum_x = 0.0;
    device->mouse_state.accum_y = 0.0;
    device->mouse_state.prev_buttons = 0;
    device->mouse_state.initialized = false;
    device->gamepad_state.gamepad_id = -1;
    device->gamepad_state.prev_buttons = 0;
    memset(device->gamepad_state.prev_axes, 0, sizeof(device->gamepad_state.prev_axes));
    memset(&device->report_layout, 0, sizeof(device->report_layout));
}

// Check if device needs control transfer workaround (known broken devices)
static bool is_control_transfer_broken(uint16_t vid, uint16_t pid)
{
    return (vid == VID_SIPEED && pid == PID_NANOKVM_USB);
}

// Check if another active slot shares the same USB device address.
// Used to avoid closing a device handle when sibling interfaces are still active.
// Caller must hold g_hid_devices_mutex.
static bool has_sibling_interface(int slot_index, uint8_t dev_addr)
{
    if (dev_addr == 0) return false;
    for (int i = 0; i < MAX_HID_DEVICES; i++) {
        if (i == slot_index) continue;
        if (g_hid_devices[i].connected && g_hid_devices[i].dev_addr == dev_addr) {
            return true;
        }
    }
    return false;
}

// // Set fallback layout for nanoKVM-USB absolute pointer interface
// // Based on Interface 3 Report Descriptor:
// //   Report ID 0x02, Buttons 5-bit, X 13-bit abs 0-4095, Y 13-bit abs 0-4095, Wheel 8-bit
// static void set_nanokvm_mouse_layout(hid_mouse_report_layout_t *layout)
// {
//     memset(layout, 0, sizeof(*layout));
//     layout->valid = true;
//     layout->has_report_id = true;
//     layout->report_id = 0x02;
//     layout->buttons.bit_offset = 0;
//     layout->buttons.bit_size = 5;
//     layout->buttons.logical_min = 0;
//     layout->buttons.logical_max = 1;
//     layout->buttons.is_relative = false;
//     layout->buttons.found = true;
//     // 5-bit buttons + 3-bit padding = 8 bits
//     layout->x.bit_offset = 8;
//     layout->x.bit_size = 13;
//     layout->x.logical_min = 0;
//     layout->x.logical_max = 4095;
//     layout->x.is_relative = false;
//     layout->x.found = true;
//     // 13-bit X + 3-bit padding = 16 bits (offset 24)
//     layout->y.bit_offset = 24;
//     layout->y.bit_size = 13;
//     layout->y.logical_min = 0;
//     layout->y.logical_max = 4095;
//     layout->y.is_relative = false;
//     layout->y.found = true;
//     // 13-bit Y + 3-bit padding = 16 bits, then 8-bit wheel
//     layout->report_byte_len = 6;  // Excluding Report ID
// }

static bool is_gamepad_device(uint16_t vid, uint16_t pid)
{
    // Check for PS5 DualSense
    if (vid == VID_SONY && pid == PID_PS5_DUALSENSE) {
        return true;
    }

    // Check for known Logitech gamepads by PID
    if (vid == VID_LOGITECH) {
        if (pid == PID_LOGITECH_F310 || pid == PID_LOGITECH_F710) {
            return true;
        }
    }

    return false;
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

static void process_keyboard_report(hid_device_info_t *device, const uint8_t *data, size_t len)
{
    if (len < sizeof(hid_keyboard_input_report_boot_t)) {
        return;
    }

    const hid_keyboard_input_report_boot_t *report = (const hid_keyboard_input_report_boot_t *)data;
    uint8_t modifier = report->modifier.val;

    // Detect modifier changes and generate events for individual modifier bits
    uint8_t mod_changed = modifier ^ device->prev_kbd_report.modifier.val;
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
        uint8_t prev_key = device->prev_kbd_report.key[i];
        if (prev_key != HID_KEY_NO_PRESS && !key_in_report(prev_key, report)) {
            FMRB_LOGD(TAG, "Key UP: scancode=0x%02X modifier=0x%02X", prev_key, modifier);
            fmrb_host_send_key_up(prev_key, prev_key, modifier);
        }
    }

    // Detect key presses (in current report, not in prev)
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        uint8_t cur_key = report->key[i];
        if (cur_key != HID_KEY_NO_PRESS && !key_in_report(cur_key, &device->prev_kbd_report)) {
            FMRB_LOGD(TAG, "Key DOWN: scancode=0x%02X modifier=0x%02X", cur_key, modifier);
            fmrb_host_send_key_down(cur_key, cur_key, modifier);
        }
    }

    memcpy(&device->prev_kbd_report, report, sizeof(hid_keyboard_input_report_boot_t));
}

static bool ensure_screen_size(void)
{
    if (!fmrb_kernel_is_ready()) {
        return false;
    }

    if (g_screen_width > 0 && g_screen_height > 0) {
        return true;
    }
    const fmrb_system_config_t* conf = fmrb_kernel_get_config();
    if (conf && conf->display_width > 0 && conf->display_height > 0) {
        g_screen_width = conf->display_width;
        g_screen_height = conf->display_height;
        g_mouse_scale_x = conf->mouse_scale_x;
        g_mouse_scale_y = conf->mouse_scale_y;
        FMRB_LOGI(TAG, "Screen size acquired: %dx%d, mouse scale: %.2f/%.2f",
                  g_screen_width, g_screen_height, g_mouse_scale_x, g_mouse_scale_y);
        return true;
    }
    return false;
}

// Auto-detect Report Protocol format for boot mice that didn't get SET_PROTOCOL.
// When boot layout (3-byte, no report ID) is set but actual reports are longer
// and start with a constant byte matching the mouse protocol ID (0x02),
// reconfigure to Report Protocol layout with 12-bit X/Y packing.
static void auto_detect_mouse_report_format(hid_device_info_t *device, const uint8_t *data, size_t len)
{
    hid_mouse_report_layout_t *layout = &device->report_layout;

    // Only auto-detect for boot layout (no report ID, 3-byte report)
    if (layout->has_report_id || layout->report_byte_len != 3) {
        return;
    }

    // If report is longer than boot format and starts with 0x02 (mouse report ID),
    // switch to Report Protocol format
    if (len >= 6 && data[0] == 0x02) {
        int slot_index = (int)(device - g_hid_devices);
        FMRB_LOGI(TAG, "Auto-detected Report Protocol format for slot %d (report_id=0x%02X, len=%d)",
                  slot_index, data[0], (int)len);

        layout->has_report_id = true;
        layout->report_id = 0x02;
        // After stripping report ID:
        // Byte 0: Buttons (8 bits)
        // Byte 1: Reserved
        // Byte 2: X[7:0]
        // Byte 3: Y[3:0]<<4 | X[11:8]
        // Byte 4: Y[11:4]
        layout->buttons.bit_offset = 0;
        layout->buttons.bit_size = 8;
        layout->buttons.found = true;
        layout->x.bit_offset = 16;  // byte 2
        layout->x.bit_size = 12;    // 12-bit signed
        layout->x.logical_min = -2048;
        layout->x.logical_max = 2047;
        layout->x.is_relative = true;
        layout->x.found = true;
        layout->y.bit_offset = 28;  // byte 3 high nibble + byte 4
        layout->y.bit_size = 12;    // 12-bit signed
        layout->y.logical_min = -2048;
        layout->y.logical_max = 2047;
        layout->y.is_relative = true;
        layout->y.found = true;
        layout->report_byte_len = 7;  // 7 bytes after report ID
        device->report_copy_len = 8;

        FMRB_LOGI(TAG, "  X: offset=%d size=%d, Y: offset=%d size=%d",
                  layout->x.bit_offset, layout->x.bit_size,
                  layout->y.bit_offset, layout->y.bit_size);
    }
}

// Maximum number of raw reports to dump in dump mode
#define DUMP_MODE_MAX_REPORTS 50

static void process_mouse_report(hid_device_info_t *device, const uint8_t *data, size_t len)
{
    // Dump mode: hex-dump raw reports for manual device configuration
    if (device->dump_mode) {
        if (device->dump_count < DUMP_MODE_MAX_REPORTS) {
            int slot_index = (int)(device - g_hid_devices);
            char hex[128];
            int pos = 0;
            int dump_len = (len > 32) ? 32 : (int)len;
            for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 3; i++) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]);
            }
            FMRB_LOGW(TAG, "DUMP slot=%d len=%d: %s", slot_index, (int)len, hex);
            device->dump_count++;
            if (device->dump_count == DUMP_MODE_MAX_REPORTS) {
                FMRB_LOGW(TAG, "DUMP slot=%d: reached %d reports, stopping dump",
                          slot_index, DUMP_MODE_MAX_REPORTS);
            }
        }
        return;
    }

    hid_mouse_report_layout_t *layout = &device->report_layout;
    if (!layout->valid) {
        return;
    }

    if (!ensure_screen_size()) {
        return;
    }

    // Auto-detect format mismatch on first reports
    auto_detect_mouse_report_format(device, data, len);

    // Dump raw report for debugging
    {
        int slot_index = (int)(device - g_hid_devices);
        char hex[64];
        int pos = 0;
        int dump_len = (len > 16) ? 16 : (int)len;
        for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 3; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]);
        }
        FMRB_LOGD(TAG, "Mouse report slot=%d len=%d: %s", slot_index, (int)len, hex);
    }

    // Skip Report ID byte if present
    const uint8_t *report_data = data;
    size_t report_len = len;
    if (layout->has_report_id) {
        if (len < 1) return;
        if (data[0] != layout->report_id) return;  // Not our report
        report_data = data + 1;
        report_len = len - 1;
    }

    // Extract fields using parsed layout
    uint8_t buttons = 0;
    if (layout->buttons.found) {
        buttons = (uint8_t)hid_report_extract_field(report_data, report_len, &layout->buttons);
    }
    int32_t raw_x = hid_report_extract_field(report_data, report_len, &layout->x);
    int32_t raw_y = hid_report_extract_field(report_data, report_len, &layout->y);

    FMRB_LOGD(TAG, "  extracted: buttons=0x%02X raw_x=%"PRId32" raw_y=%"PRId32" %s",
              buttons, raw_x, raw_y, layout->x.is_relative ? "rel" : "abs");

    if (layout->x.is_relative) {
        // Relative coordinate device
        int dx = (int)raw_x;
        int dy = (int)raw_y;

        // Initialize cursor to center on first movement when screen size is known
        if (!device->mouse_state.initialized && g_screen_width > 0 && g_screen_height > 0) {
            device->mouse_state.cursor_x = g_screen_width / 2;
            device->mouse_state.cursor_y = g_screen_height / 2;
            device->mouse_state.initialized = true;
            FMRB_LOGI(TAG, "Mouse initialized at center (%d, %d)",
                     device->mouse_state.cursor_x, device->mouse_state.cursor_y);
        }

        // Apply mouse sensitivity with fractional accumulation
        device->mouse_state.accum_x += dx * g_mouse_scale_x;
        device->mouse_state.accum_y += dy * g_mouse_scale_y;

        // Split into integer (pixel movement) and fractional (remainder) parts
        double ix, iy;
        device->mouse_state.accum_x = modf(device->mouse_state.accum_x, &ix);
        device->mouse_state.accum_y = modf(device->mouse_state.accum_y, &iy);
        dx = (int)ix;
        dy = (int)iy;

        // Update absolute cursor position from relative displacement
        if (dx != 0 || dy != 0) {
            device->mouse_state.cursor_x += dx;
            device->mouse_state.cursor_y += dy;

            // Clamp to screen bounds
            if (device->mouse_state.cursor_x < 0) device->mouse_state.cursor_x = 0;
            if (device->mouse_state.cursor_y < 0) device->mouse_state.cursor_y = 0;
            if (device->mouse_state.cursor_x >= g_screen_width) device->mouse_state.cursor_x = g_screen_width - 1;
            if (device->mouse_state.cursor_y >= g_screen_height) device->mouse_state.cursor_y = g_screen_height - 1;

            fmrb_host_send_mouse_move(device->mouse_state.cursor_x, device->mouse_state.cursor_y);
        }
    } else {
        // Absolute coordinate device: scale from [logical_min..logical_max] to screen
        int32_t range_x = layout->x.logical_max - layout->x.logical_min;
        int32_t range_y = layout->y.logical_max - layout->y.logical_min;
        if (range_x <= 0) range_x = 1;
        if (range_y <= 0) range_y = 1;

        int new_x = (int)((int64_t)(raw_x - layout->x.logical_min) * g_screen_width / range_x);
        int new_y = (int)((int64_t)(raw_y - layout->y.logical_min) * g_screen_height / range_y);

        // Clamp to screen bounds
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        if (new_x >= g_screen_width) new_x = g_screen_width - 1;
        if (new_y >= g_screen_height) new_y = g_screen_height - 1;

        if (new_x != device->mouse_state.cursor_x || new_y != device->mouse_state.cursor_y) {
            device->mouse_state.cursor_x = new_x;
            device->mouse_state.cursor_y = new_y;
            fmrb_host_send_mouse_move(new_x, new_y);
        }
        device->mouse_state.initialized = true;
    }

    // Check button changes
    uint8_t changed = buttons ^ device->mouse_state.prev_buttons;

    if (changed) {
        for (int btn = 0; btn < 3; btn++) {
            if (changed & (1 << btn)) {
                int state = (buttons & (1 << btn)) ? 1 : 0;
                FMRB_LOGD(TAG, "Mouse button %d %s at (%d,%d)",
                         btn + 1, state ? "pressed" : "released",
                         device->mouse_state.cursor_x, device->mouse_state.cursor_y);
                fmrb_host_send_mouse_click(device->mouse_state.cursor_x, device->mouse_state.cursor_y, btn + 1, state);
            }
        }
        device->mouse_state.prev_buttons = buttons;
    }
}

static void process_gamepad_report(hid_device_info_t *device, const uint8_t *data, size_t len)
{
    if (len < 8) {
        return;  // Too short for any gamepad report
    }

    int gamepad_id = device->gamepad_state.gamepad_id;
    if (gamepad_id < 0 || gamepad_id >= MAX_GAMEPAD_DEVICES) {
        return;
    }

    // Parse report based on VID/PID
    uint16_t buttons = 0;
    int16_t axes[MAX_GAMEPAD_AXES] = {0};

    if (device->vid == VID_SONY && device->pid == PID_PS5_DUALSENSE) {
        // PS5 DualSense report format (64 bytes, USB mode)
        // Byte 0: Report ID (0x01)
        // Byte 1: Left stick X (0=left, 128=center, 255=right)
        // Byte 2: Left stick Y (0=up, 128=center, 255=down)
        // Byte 3: Right stick X
        // Byte 4: Right stick Y
        // Byte 5: L2 trigger (0-255)
        // Byte 6: R2 trigger (0-255)
        // Byte 7: Button bitfield byte 1
        // Byte 8: Button bitfield byte 2

        if (len >= 10) {
            // Convert analog sticks from 0-255 to -128..127
            axes[GAMEPAD_AXIS_LEFT_X] = (int16_t)((int)data[1] - 128);
            axes[GAMEPAD_AXIS_LEFT_Y] = (int16_t)((int)data[2] - 128);
            axes[GAMEPAD_AXIS_RIGHT_X] = (int16_t)((int)data[3] - 128);
            axes[GAMEPAD_AXIS_RIGHT_Y] = (int16_t)((int)data[4] - 128);
            axes[GAMEPAD_AXIS_L2] = data[5];
            axes[GAMEPAD_AXIS_R2] = data[6];

            // Parse buttons (simplified - actual PS5 has complex button mapping)
            buttons = ((uint16_t)data[8]) | (((uint16_t)data[9]) << 8);
        }
    } else if (device->vid == VID_LOGITECH) {
        // Logitech gamepad report format (standard HID, 8 bytes)
        // Byte 0: Left stick X (signed)
        // Byte 1: Left stick Y (signed)
        // Byte 2: Right stick X (signed)
        // Byte 3: Right stick Y (signed)
        // Byte 4: Button bits (lower 8 bits)
        // Byte 5: Button bits (upper 8 bits)
        if (len >= 6) {
            axes[GAMEPAD_AXIS_LEFT_X] = (int8_t)data[0];
            axes[GAMEPAD_AXIS_LEFT_Y] = (int8_t)data[1];
            axes[GAMEPAD_AXIS_RIGHT_X] = (int8_t)data[2];
            axes[GAMEPAD_AXIS_RIGHT_Y] = (int8_t)data[3];
            buttons = ((uint16_t)data[4]) | (((uint16_t)data[5]) << 8);

            // Triggers (if available)
            if (len >= 8) {
                axes[GAMEPAD_AXIS_L2] = data[6];
                axes[GAMEPAD_AXIS_R2] = data[7];
            }
        }
    }

    // Detect button changes
    uint16_t changed_buttons = buttons ^ device->gamepad_state.prev_buttons;
    if (changed_buttons) {
        for (int btn = 0; btn < MAX_GAMEPAD_BUTTONS; btn++) {
            if (changed_buttons & (1 << btn)) {
                int state = (buttons & (1 << btn)) ? 1 : 0;
                FMRB_LOGD(TAG, "Gamepad %d button %d %s", gamepad_id, btn, state ? "pressed" : "released");
                fmrb_host_send_gamepad_button(gamepad_id, btn, state);
            }
        }
        device->gamepad_state.prev_buttons = buttons;
    }
    // Detect axis changes (with deadzone of 5)
    const int deadzone = 5;
    for (int axis = 0; axis < MAX_GAMEPAD_AXES; axis++) {
        int16_t delta = axes[axis] - device->gamepad_state.prev_axes[axis];
        if (delta < 0) delta = -delta;
        if (delta > deadzone) {
            FMRB_LOGD(TAG, "Gamepad %d axis %d: %d", gamepad_id, axis, axes[axis]);
            fmrb_host_send_gamepad_axis(gamepad_id, axis, axes[axis]);
            device->gamepad_state.prev_axes[axis] = axes[axis];
        }
    }
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                         const hid_host_interface_event_t event,
                                         void *arg)
{
    // Guard against late callbacks after stop/uninstall (race condition safety)
    if (!g_usb_running || g_hid_devices_mutex == NULL) {
        return;  // USB stack is stopping or already stopped - ignore callback
    }

    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
            // IMPORTANT: No logging, short mutex only in this high-frequency callback
            // Strategy: Take mutex briefly to get slot_index/copy_len/generation, then release before USB call

            int8_t slot_idx = -1;
            uint8_t copy_len = 0;
            uint32_t generation = 0;

            // Short mutex acquisition to find slot index (timeout=1ms, fail fast)
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MS_TO_TICKS(1)) == FMRB_TRUE) {
                // Linear search for device with matching handle
                for (int i = 0; i < MAX_HID_DEVICES; i++) {
                    if (g_hid_devices[i].handle == hid_device_handle && g_hid_devices[i].connected) {
                        slot_idx = (int8_t)i;
                        copy_len = g_hid_devices[i].report_copy_len;
                        generation = g_hid_devices[i].generation;
                        break;
                    }
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
            }

            // If mutex failed or device not found, drop this report (no logging)
            if (slot_idx < 0 || copy_len == 0) {
                break;
            }

            // Now get report data outside mutex (USB call can be slow)
            uint8_t report_data[64];
            size_t report_len = 0;
            esp_err_t ret = hid_host_device_get_raw_input_report_data(
                hid_device_handle, report_data, sizeof(report_data), &report_len);

            if (ret == ESP_OK && report_len > 0) {
                // Push only the minimum needed (copy_len or actual len, whichever is smaller)
                uint8_t actual_copy = (report_len < copy_len) ? report_len : copy_len;
                input_report_push(slot_idx, generation, report_data, actual_copy);
            }
            break;
        }

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            // Don't log in callback - too frequent during errors
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED: {
            // DISCONNECTED cleanup - always defer to task context for proper stop/close
            // IMPORTANT: No logging in callback (avoid heavy operations)
            // This ensures hid_host_device_stop/close are called consistently

            // Find slot_index+generation with short mutex (1ms timeout)
            int8_t slot_idx = -1;
            uint32_t generation = 0;
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MS_TO_TICKS(1)) == FMRB_TRUE) {
                for (int i = 0; i < MAX_HID_DEVICES; i++) {
                    if (g_hid_devices[i].handle == hid_device_handle &&
                        g_hid_devices[i].connected) {
                        slot_idx = (int8_t)i;
                        generation = g_hid_devices[i].generation;
                        break;
                    }
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
            }

            // Push slot_index+generation to deferred queue if found, otherwise request full scan
            if (slot_idx >= 0) {
                pending_disconnect_push(slot_idx, generation);
            } else {
                // Mutex timeout or device not found - request full cleanup scan as safety net
                pending_disconnect_request_cleanup();
            }
            break;
        }

        default:
            // Don't log unknown events in callback
            break;
    }
}

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                      const hid_host_driver_event_t event,
                                      void *arg)
{
    switch (event) {
        case HID_HOST_DRIVER_EVENT_CONNECTED: {
            hid_host_dev_info_t dev_info;
            uint16_t vid = 0, pid = 0;
            if (hid_host_get_device_info(hid_device_handle, &dev_info) == ESP_OK) {
                vid = dev_info.VID;
                pid = dev_info.PID;
                FMRB_LOGI(TAG, "========================================");
                FMRB_LOGI(TAG, "HID Device Connected");
                FMRB_LOGI(TAG, "  VID: 0x%04X  PID: 0x%04X", vid, pid);
                // Print manufacturer string (wchar_t -> ASCII for logging)
                char str_buf[64];
                if (dev_info.iManufacturer[0] != 0) {
                    int j = 0;
                    for (int i = 0; i < HID_STR_DESC_MAX_LENGTH && dev_info.iManufacturer[i] != 0 && j < 62; i++) {
                        str_buf[j++] = (char)(dev_info.iManufacturer[i] & 0x7F);
                    }
                    str_buf[j] = '\0';
                    FMRB_LOGI(TAG, "  Manufacturer: %s", str_buf);
                }
                if (dev_info.iProduct[0] != 0) {
                    int j = 0;
                    for (int i = 0; i < HID_STR_DESC_MAX_LENGTH && dev_info.iProduct[i] != 0 && j < 62; i++) {
                        str_buf[j++] = (char)(dev_info.iProduct[i] & 0x7F);
                    }
                    str_buf[j] = '\0';
                    FMRB_LOGI(TAG, "  Product:      %s", str_buf);
                }
                if (dev_info.iSerialNumber[0] != 0) {
                    int j = 0;
                    for (int i = 0; i < HID_STR_DESC_MAX_LENGTH && dev_info.iSerialNumber[i] != 0 && j < 62; i++) {
                        str_buf[j++] = (char)(dev_info.iSerialNumber[i] & 0x7F);
                    }
                    str_buf[j] = '\0';
                    FMRB_LOGI(TAG, "  Serial:       %s", str_buf);
                }
            }

            hid_host_dev_params_t dev_params;
            uint8_t proto = 0;
            bool is_boot_device = false;
            bool is_gamepad = is_gamepad_device(vid, pid);
            bool is_nonboot_candidate = false;

            if (hid_host_device_get_params(hid_device_handle, &dev_params) == ESP_OK) {
                proto = dev_params.proto;
                const char* proto_name = "Unknown";
                const char* sub_class_name = "None";
                if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                    is_boot_device = true;
                    sub_class_name = "Boot Interface";
                    if (proto == HID_PROTOCOL_KEYBOARD) {
                        proto_name = "Keyboard";
                    } else if (proto == HID_PROTOCOL_MOUSE) {
                        proto_name = "Mouse";
                    }
                } else if (is_gamepad) {
                    proto_name = "Gamepad";
                    sub_class_name = "Vendor Specific";
                    proto = HID_PROTOCOL_GAMEPAD;
                } else {
                    proto_name = "Non-Boot HID";
                    sub_class_name = (dev_params.sub_class == 0) ? "No SubClass" : "Unknown";
                    is_nonboot_candidate = true;
                }
                FMRB_LOGI(TAG, "  USB Addr:     %d", dev_params.addr);
                FMRB_LOGI(TAG, "  Interface:    %d", dev_params.iface_num);
                FMRB_LOGI(TAG, "  SubClass:     %s (%d)", sub_class_name, dev_params.sub_class);
                FMRB_LOGI(TAG, "  Protocol:     %s (%d)", proto_name, proto);
                FMRB_LOGI(TAG, "  Boot Device:  %s", is_boot_device ? "YES" : "NO");
                FMRB_LOGI(TAG, "========================================");
            }

            // Non-Boot, non-Gamepad devices: accept tentatively as mouse
            // Descriptor parsing in task context will verify and disconnect if not a mouse
            if (is_nonboot_candidate) {
                if (is_control_transfer_broken(vid, pid)) {
                    // Known broken device: skip non-boot interfaces (can't get descriptor)
                    FMRB_LOGI(TAG, "Ignoring non-boot interface on known device (VID=0x%04X)", vid);
                    return;
                }
                proto = HID_PROTOCOL_MOUSE;
                FMRB_LOGI(TAG, "Non-boot device accepted for descriptor parse");
            }

            // Protect device array access with mutex
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) != FMRB_TRUE) {
                FMRB_LOGE(TAG, "Failed to acquire mutex for device connection");
                return;
            }

            hid_device_info_t* slot = find_empty_slot();
            if (slot == NULL) {
                FMRB_LOGW(TAG, "No empty slot for HID device");
                fmrb_semaphore_give(g_hid_devices_mutex);
                return;
            }

            slot->handle = hid_device_handle;
            slot->proto = proto;
            slot->connected = true;
            slot->vid = vid;
            slot->pid = pid;
            slot->dev_addr = dev_params.addr;
            slot->generation++;

            // Set report copy length based on device type
            if (proto == HID_PROTOCOL_KEYBOARD) {
                slot->report_copy_len = 8;
            } else if (proto == HID_PROTOCOL_MOUSE) {
                slot->report_copy_len = 16;  // Default, may be overridden by descriptor parse
            } else if (proto == HID_PROTOCOL_GAMEPAD) {
                if (vid == VID_SONY && pid == PID_PS5_DUALSENSE) {
                    slot->report_copy_len = 64;
                } else if (vid == VID_LOGITECH) {
                    slot->report_copy_len = 8;
                } else {
                    slot->report_copy_len = 64;
                }
            } else {
                slot->report_copy_len = 0;
            }

            memset(&slot->prev_kbd_report, 0, sizeof(hid_keyboard_input_report_boot_t));
            slot->mouse_state.cursor_x = 0;
            slot->mouse_state.cursor_y = 0;
            slot->mouse_state.prev_buttons = 0;
            slot->mouse_state.initialized = false;

            // Boot mouse: layout stays invalid until SET_PROTOCOL(Boot) succeeds in task context
            // This prevents misinterpreting Report Protocol data as Boot format

            // Initialize gamepad state if it's a gamepad
            if (is_gamepad) {
                int free_id = -1;
                bool id_used[MAX_GAMEPAD_DEVICES] = {false, false};
                for (int i = 0; i < MAX_HID_DEVICES; i++) {
                    if (g_hid_devices[i].connected && i != (slot - g_hid_devices) &&
                        g_hid_devices[i].proto == HID_PROTOCOL_GAMEPAD) {
                        int gid = g_hid_devices[i].gamepad_state.gamepad_id;
                        if (gid >= 0 && gid < MAX_GAMEPAD_DEVICES) {
                            id_used[gid] = true;
                        }
                    }
                }
                for (int i = 0; i < MAX_GAMEPAD_DEVICES; i++) {
                    if (!id_used[i]) {
                        free_id = i;
                        break;
                    }
                }

                if (free_id >= 0) {
                    slot->gamepad_state.gamepad_id = free_id;
                    slot->gamepad_state.prev_buttons = 0;
                    memset(slot->gamepad_state.prev_axes, 0, sizeof(slot->gamepad_state.prev_axes));
                    FMRB_LOGI(TAG, "Gamepad assigned ID: %d", free_id);
                } else {
                    FMRB_LOGW(TAG, "No free gamepad ID available (max %d)", MAX_GAMEPAD_DEVICES);
                    memset(slot, 0, sizeof(*slot));
                    slot->gamepad_state.gamepad_id = -1;
                    fmrb_semaphore_give(g_hid_devices_mutex);
                    return;
                }
            }

            int slot_index = (int)(slot - g_hid_devices);
            uint32_t slot_generation = slot->generation;

            fmrb_semaphore_give(g_hid_devices_mutex);

            // Open and start device
            const hid_host_device_config_t dev_config = {
                .callback = hid_host_interface_callback,
                .callback_arg = NULL
            };

            esp_err_t ret = hid_host_device_open(hid_device_handle, &dev_config);
            if (ret != ESP_OK) {
                FMRB_LOGE(TAG, "hid_host_device_open failed: 0x%x", ret);
                if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                    if (g_hid_devices[slot_index].generation == slot_generation &&
                        g_hid_devices[slot_index].handle == hid_device_handle) {
                        clear_slot(&g_hid_devices[slot_index]);
                    }
                    fmrb_semaphore_give(g_hid_devices_mutex);
                }
                break;
            }

            ret = hid_host_device_start(hid_device_handle);
            if (ret != ESP_OK) {
                FMRB_LOGE(TAG, "hid_host_device_start failed: 0x%x", ret);
                hid_host_device_close(hid_device_handle);
                if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                    if (g_hid_devices[slot_index].generation == slot_generation &&
                        g_hid_devices[slot_index].handle == hid_device_handle) {
                        clear_slot(&g_hid_devices[slot_index]);
                    }
                    fmrb_semaphore_give(g_hid_devices_mutex);
                }
                break;
            }

            // Mouse layout setup priority:
            // 1. TOML config file (hid_devices.toml) - user-defined per VID/PID
            // 2. Boot mouse: boot protocol fallback + auto-detect
            // 3. Non-boot mouse: parse HID report descriptor
            if (proto == HID_PROTOCOL_MOUSE) {
                hid_mouse_report_layout_t cfg_layout;
                uint8_t cfg_copy_len;
                if (hid_device_config_find_mouse(vid, pid, &cfg_layout, &cfg_copy_len)) {
                    // TOML config matched - apply directly
                    if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                        hid_device_info_t* device = &g_hid_devices[slot_index];
                        if (device->connected && device->generation == slot_generation) {
                            device->report_layout = cfg_layout;
                            device->report_copy_len = cfg_copy_len;
                            FMRB_LOGI(TAG, "TOML config layout applied for slot %d", slot_index);
                        }
                        fmrb_semaphore_give(g_hid_devices_mutex);
                    }
                } else if (is_boot_device) {
                    // No config: apply boot mouse layout + auto-detect on first report
                    if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                        hid_device_info_t* device = &g_hid_devices[slot_index];
                        if (device->connected && device->generation == slot_generation) {
                            hid_mouse_report_layout_t *layout = &device->report_layout;
                            layout->valid = true;
                            layout->has_report_id = false;
                            layout->report_id = 0;
                            layout->buttons.bit_offset = 0;
                            layout->buttons.bit_size = 8;
                            layout->buttons.logical_min = 0;
                            layout->buttons.logical_max = 1;
                            layout->buttons.is_relative = false;
                            layout->buttons.found = true;
                            layout->x.bit_offset = 8;
                            layout->x.bit_size = 8;
                            layout->x.logical_min = -127;
                            layout->x.logical_max = 127;
                            layout->x.is_relative = true;
                            layout->x.found = true;
                            layout->y.bit_offset = 16;
                            layout->y.bit_size = 8;
                            layout->y.logical_min = -127;
                            layout->y.logical_max = 127;
                            layout->y.is_relative = true;
                            layout->y.found = true;
                            layout->report_byte_len = 3;
                            device->report_copy_len = 8;
                            FMRB_LOGI(TAG, "Boot mouse layout applied for slot %d (auto-detect enabled)", slot_index);
                        }
                        fmrb_semaphore_give(g_hid_devices_mutex);
                    }
                } else if (!hid_device_config_skip_control_transfer(vid, pid)) {
                    // Non-boot, no config: enter dump mode directly
                    // Control transfer (descriptor fetch) can corrupt HID library state
                    // and break the IN endpoint, so skip it and capture raw reports instead
                    if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                        hid_device_info_t* device = &g_hid_devices[slot_index];
                        if (device->connected && device->generation == slot_generation) {
                            device->dump_mode = true;
                            device->dump_count = 0;
                            device->report_copy_len = 32;
                        }
                        fmrb_semaphore_give(g_hid_devices_mutex);
                    }
                    FMRB_LOGW(TAG, "Non-boot mouse slot %d: no config, entering dump mode (VID=0x%04X PID=0x%04X)",
                              slot_index, vid, pid);
                    FMRB_LOGW(TAG, "  Add this device to /etc/hid_devices.toml to configure it");
                }
            } else if (is_boot_device && proto == HID_PROTOCOL_KEYBOARD) {
                FMRB_LOGI(TAG, "Boot keyboard ready for slot %d", slot_index);
            }

            FMRB_LOGI(TAG, "HID Device Connected (proto=%d, VID=0x%04X, PID=0x%04X)", proto, vid, pid);
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
        esp_err_t ret = usb_host_lib_handle_events(FMRB_MS_TO_TICKS(100), &event_flags);
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
        esp_err_t ret = hid_host_handle_events(FMRB_MS_TO_TICKS(100));
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            FMRB_LOGW(TAG, "HID host handle events error: %d", ret);
        }

        // Process input reports (deferred from callback)
        // This runs in task context, so we can use portMAX_DELAY and call process_* safely
        // Limit to 8 reports per loop to prevent starvation of other tasks
        const int max_reports_per_loop = 8;
        int reports_processed = 0;
        hid_input_report_t report;

        while (reports_processed < max_reports_per_loop && input_report_pop(&report)) {
            reports_processed++;

            // Validate slot_index range
            if (report.slot_index < 0 || report.slot_index >= MAX_HID_DEVICES) {
                continue;  // Invalid slot, skip
            }

            // Take mutex with FMRB_MAX_DELAY (safe in task context)
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                hid_device_info_t* device = &g_hid_devices[report.slot_index];

                // Validate generation to detect stale reports (from disconnected devices)
                // This prevents race condition where disconnect happens after callback queued report
                if (device->connected && device->generation == report.generation) {
                    // Call process_* functions (which call fmrb_host_send_*) in task context
                    if (device->proto == HID_PROTOCOL_KEYBOARD) {
                        process_keyboard_report(device, report.report_data, report.report_len);
                    } else if (device->proto == HID_PROTOCOL_MOUSE) {
                        process_mouse_report(device, report.report_data, report.report_len);
                    } else if (device->proto == HID_PROTOCOL_GAMEPAD) {
                        process_gamepad_report(device, report.report_data, report.report_len);
                    }
                }
                // If generation mismatch or device disconnected, silently drop the report

                fmrb_semaphore_give(g_hid_devices_mutex);
            }
        }

        // Flush pending mouse moves that were rate-limited
        // Check for input report overflow and perform recovery
        if (input_report_check_overflow()) {
            FMRB_LOGW(TAG, "Input report queue overflow - performing device state reset");

            // Send "all release" events to prevent stuck keys/buttons
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                for (int i = 0; i < MAX_HID_DEVICES; i++) {
                    if (!g_hid_devices[i].connected) {
                        continue;
                    }

                    if (g_hid_devices[i].proto == HID_PROTOCOL_KEYBOARD) {
                        // Send key-up for all previously pressed keys
                        for (int j = 0; j < HID_KEYBOARD_KEY_MAX; j++) {
                            uint8_t key = g_hid_devices[i].prev_kbd_report.key[j];
                            if (key != HID_KEY_NO_PRESS) {
                                fmrb_host_send_key_up(key, key, 0);
                            }
                        }
                        // Send modifier release
                        uint8_t mod = g_hid_devices[i].prev_kbd_report.modifier.val;
                        if (mod != 0) {
                            static const uint8_t mod_scancodes[] = {
                                HID_KEY_LEFT_CONTROL, HID_KEY_LEFT_SHIFT, HID_KEY_LEFT_ALT, HID_KEY_LEFT_GUI,
                                HID_KEY_RIGHT_CONTROL, HID_KEY_RIGHT_SHIFT, HID_KEY_RIGHT_ALT, HID_KEY_RIGHT_GUI,
                            };
                            for (int j = 0; j < 8; j++) {
                                if (mod & (1 << j)) {
                                    fmrb_host_send_key_up(mod_scancodes[j], mod_scancodes[j], 0);
                                }
                            }
                        }
                        memset(&g_hid_devices[i].prev_kbd_report, 0, sizeof(hid_keyboard_input_report_boot_t));

                    } else if (g_hid_devices[i].proto == HID_PROTOCOL_MOUSE) {
                        // Send mouse button release for all pressed buttons
                        uint8_t buttons = g_hid_devices[i].mouse_state.prev_buttons;
                        for (int btn = 0; btn < 3; btn++) {
                            if (buttons & (1 << btn)) {
                                fmrb_host_send_mouse_click(
                                    g_hid_devices[i].mouse_state.cursor_x,
                                    g_hid_devices[i].mouse_state.cursor_y,
                                    btn + 1, 0);  // 0 = released
                            }
                        }
                        g_hid_devices[i].mouse_state.prev_buttons = 0;

                    } else if (g_hid_devices[i].proto == HID_PROTOCOL_GAMEPAD) {
                        // Send gamepad button release for all pressed buttons
                        int gamepad_id = g_hid_devices[i].gamepad_state.gamepad_id;
                        if (gamepad_id >= 0) {
                            uint16_t buttons = g_hid_devices[i].gamepad_state.prev_buttons;
                            for (int btn = 0; btn < MAX_GAMEPAD_BUTTONS; btn++) {
                                if (buttons & (1 << btn)) {
                                    fmrb_host_send_gamepad_button(gamepad_id, btn, 0);  // 0 = released
                                }
                            }
                            g_hid_devices[i].gamepad_state.prev_buttons = 0;

                            // Reset axes to center (0)
                            for (int axis = 0; axis < MAX_GAMEPAD_AXES; axis++) {
                                if (g_hid_devices[i].gamepad_state.prev_axes[axis] != 0) {
                                    fmrb_host_send_gamepad_axis(gamepad_id, axis, 0);
                                    g_hid_devices[i].gamepad_state.prev_axes[axis] = 0;
                                }
                            }
                        }
                    }
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
            }
        }

        // Process pending mouse descriptor parse (deferred from callback)
        {
            pending_protocol_setup_item_t setup;
            while (g_usb_running && pending_protocol_setup_pop(&setup)) {
                if (setup.slot_index < 0 || setup.slot_index >= MAX_HID_DEVICES) continue;

                hid_host_device_handle_t handle = NULL;
                if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                    hid_device_info_t* device = &g_hid_devices[setup.slot_index];
                    if (device->connected && device->generation == setup.generation &&
                        device->handle != NULL) {
                        handle = device->handle;
                    }
                    fmrb_semaphore_give(g_hid_devices_mutex);
                }
                if (handle == NULL) continue;

                FMRB_LOGI(TAG, "Parsing report descriptor for slot %d (proto=%d)",
                          setup.slot_index, setup.proto);

                // Retry descriptor fetch (composite devices may need time to settle)
                #define DESC_FETCH_MAX_RETRIES 3
                #define DESC_FETCH_RETRY_DELAY_MS 500
                size_t desc_len = 0;
                const uint8_t *desc = NULL;
                for (int retry = 0; retry < DESC_FETCH_MAX_RETRIES; retry++) {
                    if (retry > 0) {
                        FMRB_LOGI(TAG, "Retrying descriptor fetch for slot %d (attempt %d/%d)",
                                  setup.slot_index, retry + 1, DESC_FETCH_MAX_RETRIES);
                        fmrb_task_delay_ms(DESC_FETCH_RETRY_DELAY_MS);
                    }
                    desc = hid_host_get_report_descriptor(handle, &desc_len);
                    if (desc != NULL && desc_len > 0) break;
                }
                if (desc != NULL && desc_len > 0) {
                    FMRB_LOGI(TAG, "Report descriptor: %d bytes", (int)desc_len);
                    hid_mouse_report_layout_t parsed;
                    memset(&parsed, 0, sizeof(parsed));
                    if (hid_report_parse_mouse(desc, desc_len, &parsed)) {
                        if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                            hid_device_info_t* device = &g_hid_devices[setup.slot_index];
                            if (device->connected && device->generation == setup.generation) {
                                device->report_layout = parsed;
                                uint8_t calc_len = (uint8_t)(parsed.report_byte_len +
                                                   (parsed.has_report_id ? 1 : 0));
                                if (calc_len > 64) calc_len = 64;
                                if (calc_len > device->report_copy_len) {
                                    device->report_copy_len = calc_len;
                                }
                                FMRB_LOGI(TAG, "Descriptor parse OK: slot=%d report_id=%s(%d) copy_len=%d %s",
                                          setup.slot_index,
                                          parsed.has_report_id ? "yes" : "no",
                                          parsed.report_id,
                                          device->report_copy_len,
                                          parsed.x.is_relative ? "rel" : "abs");
                                FMRB_LOGI(TAG, "  buttons: offset=%d size=%d",
                                          parsed.buttons.bit_offset, parsed.buttons.bit_size);
                                FMRB_LOGI(TAG, "  X: offset=%d size=%d range=[%d..%d]",
                                          parsed.x.bit_offset, parsed.x.bit_size,
                                          parsed.x.logical_min, parsed.x.logical_max);
                                FMRB_LOGI(TAG, "  Y: offset=%d size=%d range=[%d..%d]",
                                          parsed.y.bit_offset, parsed.y.bit_size,
                                          parsed.y.logical_min, parsed.y.logical_max);
                            }
                            fmrb_semaphore_give(g_hid_devices_mutex);
                        }
                    } else {
                        // Descriptor parse did not find mouse fields
                        if (setup.is_boot) {
                            // Boot device: fall back to standard boot mouse layout
                            FMRB_LOGW(TAG, "Descriptor parse failed for boot mouse slot=%d, using boot layout fallback",
                                      setup.slot_index);
                            goto apply_boot_fallback;
                        }
                        FMRB_LOGI(TAG, "Descriptor parse: not a mouse (slot=%d), disconnecting", setup.slot_index);
                        if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                            hid_device_info_t* device = &g_hid_devices[setup.slot_index];
                            if (device->connected && device->generation == setup.generation) {
                                hid_host_device_handle_t dh = device->handle;
                                bool sibling = has_sibling_interface(setup.slot_index, device->dev_addr);
                                clear_slot(device);
                                fmrb_semaphore_give(g_hid_devices_mutex);
                                if (sibling) {
                                    FMRB_LOGI(TAG, "Sibling interface active, skipping device close for slot %d", setup.slot_index);
                                } else {
                                    hid_host_device_stop(dh);
                                    hid_host_device_close(dh);
                                }
                            } else {
                                fmrb_semaphore_give(g_hid_devices_mutex);
                            }
                        }
                    }
                } else {
                    // Failed to get descriptor
                    if (setup.is_boot) {
                        FMRB_LOGW(TAG, "No descriptor for boot mouse slot=%d, using boot layout fallback",
                                  setup.slot_index);
                        goto apply_boot_fallback;
                    }
                    FMRB_LOGW(TAG, "Failed to get descriptor for slot %d after %d attempts",
                              setup.slot_index, DESC_FETCH_MAX_RETRIES);
                    // Keep slot alive in dump mode to capture raw reports for manual config
                    if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                        hid_device_info_t* device = &g_hid_devices[setup.slot_index];
                        if (device->connected && device->generation == setup.generation) {
                            device->dump_mode = true;
                            device->dump_count = 0;
                            device->report_copy_len = 32;  // Capture enough bytes for analysis
                            hid_host_device_handle_t dh = device->handle;
                            FMRB_LOGW(TAG, "Slot %d entering dump mode (VID=0x%04X PID=0x%04X) - send HID input to see raw reports",
                                      setup.slot_index, device->vid, device->pid);
                            fmrb_semaphore_give(g_hid_devices_mutex);
                            // Restart IN endpoint polling (control transfer failures may have disrupted it)
                            hid_host_device_stop(dh);
                            fmrb_task_delay_ms(50);
                            esp_err_t restart_ret = hid_host_device_start(dh);
                            if (restart_ret != ESP_OK) {
                                FMRB_LOGW(TAG, "Failed to restart IN endpoint for slot %d: 0x%x",
                                          setup.slot_index, restart_ret);
                            } else {
                                FMRB_LOGI(TAG, "IN endpoint restarted for dump mode slot %d", setup.slot_index);
                            }
                        } else {
                            fmrb_semaphore_give(g_hid_devices_mutex);
                        }
                    }
                }

                // Skip boot fallback if we already succeeded
                if (false) {
                apply_boot_fallback:
                    if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                        hid_device_info_t* device = &g_hid_devices[setup.slot_index];
                        if (device->connected && device->generation == setup.generation) {
                            hid_mouse_report_layout_t *layout = &device->report_layout;
                            layout->valid = true;
                            layout->has_report_id = false;
                            layout->report_id = 0;
                            layout->buttons.bit_offset = 0;
                            layout->buttons.bit_size = 8;
                            layout->buttons.logical_min = 0;
                            layout->buttons.logical_max = 1;
                            layout->buttons.is_relative = false;
                            layout->buttons.found = true;
                            layout->x.bit_offset = 8;
                            layout->x.bit_size = 8;
                            layout->x.logical_min = -127;
                            layout->x.logical_max = 127;
                            layout->x.is_relative = true;
                            layout->x.found = true;
                            layout->y.bit_offset = 16;
                            layout->y.bit_size = 8;
                            layout->y.logical_min = -127;
                            layout->y.logical_max = 127;
                            layout->y.is_relative = true;
                            layout->y.found = true;
                            layout->report_byte_len = 3;
                            device->report_copy_len = 8;
                            FMRB_LOGI(TAG, "Boot mouse fallback layout applied for slot %d", setup.slot_index);
                        }
                        fmrb_semaphore_give(g_hid_devices_mutex);
                    }
                }
            }
        }

        // Process pending disconnects (deferred from callback)
        // This runs in task context, so we can use portMAX_DELAY and call stop/close safely
        // Skip if USB is stopping (usb_task_stop will handle cleanup)
        pending_disconnect_item_t item;
        while (g_usb_running && pending_disconnect_pop(&item)) {
            // Validate slot_index
            if (item.slot_index < 0 || item.slot_index >= MAX_HID_DEVICES) {
                continue;  // Invalid slot, skip
            }

            // Get handle from slot with mutex, validate generation to prevent slot reuse issues
            hid_host_device_handle_t handle = NULL;
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                hid_device_info_t* device = &g_hid_devices[item.slot_index];
                // Only process if generation matches (prevents processing wrong device after slot reuse)
                if (device->connected && device->generation == item.generation && device->handle != NULL) {
                    handle = device->handle;
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
            }

            // If we got a valid handle, stop and close outside mutex (can be slow)
            if (handle != NULL) {
                FMRB_LOGI(TAG, "Processing deferred disconnect for slot %d gen %u", item.slot_index, item.generation);

                // Stop device with error checking
                esp_err_t ret = hid_host_device_stop(handle);
                if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND && ret != ESP_ERR_INVALID_STATE) {
                    FMRB_LOGW(TAG, "hid_host_device_stop failed: 0x%x", ret);
                }

                // Close device with error checking
                ret = hid_host_device_close(handle);
                if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND && ret != ESP_ERR_INVALID_STATE) {
                    FMRB_LOGW(TAG, "hid_host_device_close failed: 0x%x", ret);
                }

                // Then, clean up the device slot (inside mutex)
                if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                    hid_device_info_t* device = &g_hid_devices[item.slot_index];
                    // Triple-check: generation + handle match (prevent slot reuse issues)
                    if (device->connected && device->generation == item.generation && device->handle == handle) {
                        FMRB_LOGI(TAG, "HID Device Disconnected (deferred, proto=%d)", device->proto);
                        clear_slot(device);  // This increments generation, invalidating queued reports
                    }
                    fmrb_semaphore_give(g_hid_devices_mutex);
                }
            }
        }

        // Check for cleanup needed flag (set when DISCONNECTED callback fails to get mutex)
        if (pending_disconnect_check_cleanup_needed()) {
            FMRB_LOGW(TAG, "Full cleanup scan requested (callback mutex timeout)");

            // 2-stage scan to avoid calling stop() inside mutex (prevents deadlock)
            // Stage 1: Collect candidates with mutex
            typedef struct {
                hid_host_device_handle_t handle;
                int8_t slot_idx;
                uint32_t generation;
            } cleanup_candidate_t;
            cleanup_candidate_t candidates[MAX_HID_DEVICES];
            int candidate_count = 0;

            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                for (int i = 0; i < MAX_HID_DEVICES; i++) {
                    if (g_hid_devices[i].connected && g_hid_devices[i].handle != NULL) {
                        candidates[candidate_count].handle = g_hid_devices[i].handle;
                        candidates[candidate_count].slot_idx = (int8_t)i;
                        candidates[candidate_count].generation = g_hid_devices[i].generation;
                        candidate_count++;
                    }
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
            }

            // Stage 2: Call stop() and close() outside mutex (may block or trigger callbacks)
            // Stage 3: Verify generation match and clear slot inside mutex
            for (int i = 0; i < candidate_count; i++) {
                esp_err_t ret_stop = hid_host_device_stop(candidates[i].handle);
                esp_err_t ret_close = hid_host_device_close(candidates[i].handle);

                // If both stop and close indicate device is gone (NOT_FOUND or INVALID_STATE),
                // it's an orphaned device - clean up the slot
                bool is_orphan = (ret_stop == ESP_ERR_NOT_FOUND || ret_stop == ESP_ERR_INVALID_STATE) &&
                                 (ret_close == ESP_ERR_NOT_FOUND || ret_close == ESP_ERR_INVALID_STATE);

                if (is_orphan) {
                    // Device already gone - verify and clean up the slot
                    if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                        hid_device_info_t* device = &g_hid_devices[candidates[i].slot_idx];
                        // Verify generation + handle match (prevent slot reuse issues)
                        if (device->connected &&
                            device->generation == candidates[i].generation &&
                            device->handle == candidates[i].handle) {
                            FMRB_LOGI(TAG, "Cleanup scan: slot %d orphaned, clearing", candidates[i].slot_idx);
                            clear_slot(device);
                        }
                        fmrb_semaphore_give(g_hid_devices_mutex);
                    }
                }
            }
        }

        // Check for ring overflow and do cleanup if needed
        if (pending_disconnect_check_overflow()) {
            FMRB_LOGW(TAG, "Pending disconnect overflow - cleaning up gamepad devices");

            // Clear the pending ring (we're doing cleanup, so discard all queued items)
            fmrb_enter_critical(&g_pending_disconnect_spinlock);
            g_pending_disconnect_head = 0;
            g_pending_disconnect_tail = 0;
            fmrb_exit_critical(&g_pending_disconnect_spinlock);

            // 3-stage cleanup: collect handles, stop/close outside mutex, clear slots
            typedef struct {
                hid_host_device_handle_t handle;
                int8_t slot_idx;
                uint32_t generation;
            } overflow_cleanup_t;
            overflow_cleanup_t oc_items[MAX_HID_DEVICES];
            int oc_count = 0;

            // Stage 1: collect gamepad handles with mutex
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                for (int i = 0; i < MAX_HID_DEVICES; i++) {
                    if (g_hid_devices[i].connected &&
                        g_hid_devices[i].proto == HID_PROTOCOL_GAMEPAD &&
                        g_hid_devices[i].handle != NULL) {
                        oc_items[oc_count].handle = g_hid_devices[i].handle;
                        oc_items[oc_count].slot_idx = (int8_t)i;
                        oc_items[oc_count].generation = g_hid_devices[i].generation;
                        oc_count++;
                    }
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
            }

            // Stage 2: stop/close outside mutex
            for (int i = 0; i < oc_count; i++) {
                hid_host_device_stop(oc_items[i].handle);
                hid_host_device_close(oc_items[i].handle);
            }

            // Stage 3: clear slots with mutex
            if (fmrb_semaphore_take(g_hid_devices_mutex, FMRB_MAX_DELAY) == FMRB_TRUE) {
                int cleared_count = 0;
                for (int i = 0; i < oc_count; i++) {
                    hid_device_info_t* device = &g_hid_devices[oc_items[i].slot_idx];
                    if (device->connected &&
                        device->generation == oc_items[i].generation &&
                        device->handle == oc_items[i].handle) {
                        clear_slot(device);
                        cleared_count++;
                    }
                }
                fmrb_semaphore_give(g_hid_devices_mutex);
                FMRB_LOGI(TAG, "Overflow cleanup: cleared %d gamepad slots", cleared_count);
            }
        }
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

    // Create mutex for device array protection
    g_hid_devices_mutex = fmrb_semaphore_create_mutex();
    if (g_hid_devices_mutex == NULL) {
        FMRB_LOGE(TAG, "Failed to create mutex");
        return FMRB_ERR_FAILED;
    }

    init_device_slots();
    g_screen_width = 0;
    g_screen_height = 0;
    g_mouse_scale_x = 1.0;
    g_mouse_scale_y = 1.0;

    // Initialize input report ring buffer
    g_input_report_head = 0;
    g_input_report_tail = 0;
    g_input_report_overflow = false;

    // Initialize pending disconnect ring buffer
    g_pending_disconnect_head = 0;
    g_pending_disconnect_tail = 0;
    g_pending_disconnect_overflow = false;
    g_pending_disconnect_cleanup_needed = false;

    FMRB_LOGI(TAG, "Initializing USB Host...");

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t ret = usb_host_install(&host_config);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "USB Host install failed: %d", ret);
        fmrb_semaphore_delete(g_hid_devices_mutex);
        g_hid_devices_mutex = NULL;
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
        fmrb_semaphore_delete(g_hid_devices_mutex);
        g_hid_devices_mutex = NULL;
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

    fmrb_base_type_t ret = fmrb_task_create_ex(
        usb_host_lib_task,
        "usb_host_lib",
        FMRB_USB_HOST_TASK_STACK_SIZE,
        NULL,
        FMRB_USB_HOST_TASK_PRIORITY,
        &g_usb_lib_task_handle,
        FMRB_USB_HOST_TASK_FLAGS
    );
    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create USB host lib task");
        g_usb_running = 0;
        // No cleanup needed - USB stack not affected yet
        return;
    }

    ret = fmrb_task_create_ex(
        hid_host_task,
        "hid_host",
        FMRB_USB_HID_TASK_STACK_SIZE,
        NULL,
        FMRB_USB_HID_TASK_PRIORITY,
        &g_hid_task_handle,
        FMRB_USB_HID_TASK_FLAGS
    );
    if (ret != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create HID host task");
        g_usb_running = 0;

        // Clean up: usb_host_lib_task is already running, need to stop it
        // Wait for usb_host_lib_task to exit (it checks g_usb_running)
        int timeout_count = 0;
        const int max_timeout = 30;
        while (!g_usb_lib_task_exited && timeout_count < max_timeout) {
            fmrb_task_delay_ms(100);
            timeout_count++;
        }

        if (timeout_count >= max_timeout) {
            FMRB_LOGE(TAG, "Timeout waiting for USB lib task to exit during cleanup");
        }

        // Now safe to uninstall USB stack
        hid_host_uninstall();
        usb_host_uninstall();

        g_usb_lib_task_handle = NULL;
        g_hid_task_handle = NULL;
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
        FMRB_LOGE(TAG, "FATAL: Timeout waiting for tasks to exit (hid:%d, usb:%d)",
                  g_hid_task_exited, g_usb_lib_task_exited);
        FMRB_LOGE(TAG, "Cannot safely uninstall USB stack - tasks still running");
        FMRB_LOGE(TAG, "System is in inconsistent state - manual reset recommended");

        // Set LED to FATAL error state (blinking) to indicate critical failure
        status_led_set_error(FMRB_LED_STATUS_FATAL);

        // Do NOT proceed with uninstall if tasks are still running (Use-After-Free risk)
        // Do NOT delete mutex either - tasks are still using it (would cause NULL deref crash)
        // Just set FATAL LED and return - system will need manual reset
        return;
    }

    // Tasks have exited - safe to cleanup and uninstall
    // 2-stage cleanup to avoid calling stop/close inside mutex (prevents deadlock)
    // Stage 1: Collect devices to close with mutex
    typedef struct {
        hid_host_device_handle_t handle;
        int slot_idx;
    } cleanup_item_t;
    cleanup_item_t cleanup_items[MAX_HID_DEVICES];
    int cleanup_count = 0;

    if (g_hid_devices_mutex != NULL && fmrb_semaphore_take(g_hid_devices_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < MAX_HID_DEVICES; i++) {
            if (g_hid_devices[i].connected && g_hid_devices[i].handle != NULL) {
                cleanup_items[cleanup_count].handle = g_hid_devices[i].handle;
                cleanup_items[cleanup_count].slot_idx = i;
                cleanup_count++;
            }
        }
        fmrb_semaphore_give(g_hid_devices_mutex);
    }

    // Stage 2: Call stop/close outside mutex (may trigger callbacks)
    for (int i = 0; i < cleanup_count; i++) {
        hid_host_device_stop(cleanup_items[i].handle);
        hid_host_device_close(cleanup_items[i].handle);
    }

    // Stage 3: Clear slots with mutex
    if (g_hid_devices_mutex != NULL && fmrb_semaphore_take(g_hid_devices_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < cleanup_count; i++) {
            int idx = cleanup_items[i].slot_idx;
            if (g_hid_devices[idx].handle == cleanup_items[i].handle) {
                g_hid_devices[idx].handle = NULL;
                g_hid_devices[idx].connected = false;
            }
        }
        fmrb_semaphore_give(g_hid_devices_mutex);
    }

    hid_host_uninstall();
    usb_host_uninstall();

    // Clean up mutex
    if (g_hid_devices_mutex != NULL) {
        fmrb_semaphore_delete(g_hid_devices_mutex);
        g_hid_devices_mutex = NULL;
    }

    g_usb_lib_task_handle = NULL;
    g_hid_task_handle = NULL;
    FMRB_LOGI(TAG, "USB HID Host stopped");
}
