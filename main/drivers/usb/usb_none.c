// usb_none.c - the USB host API for a board without a USB host port
//
// Compiled instead of usb_task.c where there is no host port to drive
// (ATOM_DISPLAY, the AtomS3R: main/CMakeLists.txt). The apps that list USB
// devices or read raw HID reports keep working and are told there is nothing
// attached; boot does not start a USB task at all (FMRB_HAS_USB_HOST).

#include "usb_task.h"

int usb_task_get_device_info(fmrb_usb_device_info_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

fmrb_err_t usb_task_subscribe_raw_reports(int8_t slot_index, uint16_t subscriber_pid)
{
    (void)slot_index;
    (void)subscriber_pid;
    return FMRB_ERR_NOT_FOUND;
}

fmrb_err_t usb_task_unsubscribe_raw_reports(int8_t slot_index)
{
    (void)slot_index;
    return FMRB_ERR_NOT_FOUND;
}
