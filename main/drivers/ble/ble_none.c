// ble_none.c - the BLE API for a firmware built without Bluetooth
//
// Compiled instead of ble_task.c when the sdkconfig has no CONFIG_BT_ENABLED
// (main/CMakeLists.txt). Any board can be built that way; the first to do so
// is the AtomS3R, which is reached over WiFi. Everything that calls into BLE
// (boot's radio policy, the desktop menu, the remote debugger's BLE transport)
// keeps compiling and is told "not here" at run time, the same answer a
// failed start gives.

#include "ble_task.h"
#include "ble_debug_link.h"

fmrb_err_t ble_task_init(void)
{
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t ble_service_start(void)
{
    return FMRB_ERR_NOT_SUPPORTED;
}

bool ble_service_is_started(void)
{
    return false;
}

int ble_ui_state(void)
{
    return 0;
}

fmrb_err_t ble_settings_window_set(uint32_t seconds)
{
    (void)seconds;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t ble_setup_mode_start(int button_gpio)
{
    (void)button_gpio;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t ble_release_memory(void)
{
    return FMRB_ERR_NOT_SUPPORTED;
}

uint32_t ble_settings_window_left(void)
{
    return 0;
}

fmrb_err_t ble_task_deinit(void)
{
    return FMRB_OK;
}

// Accepted, so the debug daemon keeps running for the on-device debugger; no
// peer ever connects, and the BLE transport simply stays down.
fmrb_err_t ble_debug_register(fmrb_queue_t free_q, fmrb_queue_t ready_q)
{
    (void)free_q;
    (void)ready_q;
    return FMRB_OK;
}

fmrb_err_t ble_debug_send(const uint8_t *encoded, size_t len)
{
    (void)encoded;
    (void)len;
    return FMRB_ERR_INVALID_STATE;
}

bool ble_debug_link_ready(void)
{
    return false;
}

const uint8_t *ble_debug_rx_buf(uint8_t idx)
{
    (void)idx;
    return NULL;
}
