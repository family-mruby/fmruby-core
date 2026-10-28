/*
 * linux_bt.h - Bluetooth through BlueZ (doc/linux_native, L14): the
 * adapter's power, the devices paired before (by Linux's own tools), and
 * connecting and disconnecting them, for the PC's settings window. New
 * devices are not paired here (the window names Linux's tool for that).
 *
 * A thread of its own on the system bus (sd-bus, linux_bus.h); the calls
 * below only copy under a lock and wake it. The thread reads the objects
 * (ObjectManager.GetManagedObjects on org.bluez) when asked and older than
 * 2 s, sets Adapter1.Powered, and calls Device1.Connect / Disconnect (which
 * take seconds: the thread waits, nobody else does).
 *
 * Nothing here runs unless linux_bt_start was called (the machine's
 * system.settings); DBUS_SYSTEM_BUS_ADDRESS moves the bus (the checks' own
 * BlueZ stand-in).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void linux_bt_start(void);

/* As text, more than cap: nothing written. First line:
 *   "bt\t<status>\t<powered 0|1>\t<busy address>\t<code>\t<arg>"
 *     status  none (no BlueZ or no adapter), ok, starting
 *     code    connected, disconnected, failed (arg: the reason),
 *             powered_on, powered_off, power_failed (arg: the reason), ""
 * then per known device "dev\t<address>\t<name>\t<connected 0|1>\t<icon>",
 * connected ones first, then by name. */
int32_t linux_bt_list(char *buf, size_t cap);

fmrb_err_t linux_bt_power(bool on);
/* on: connect, else disconnect. FMRB_ERR_BUSY while one is under way. */
fmrb_err_t linux_bt_connect(const char *address, bool on);

/* The pure part, for the host tests: is a device one to list (paired,
 * bonded or trusted). */
bool linux_bt_known(bool paired, bool bonded, bool trusted);

#ifdef __cplusplus
}
#endif
