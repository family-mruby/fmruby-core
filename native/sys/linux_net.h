/*
 * linux_net.h - Wi-Fi through NetworkManager (doc/linux_native, L14): the
 * networks in reach, connecting (with a password) and disconnecting, for the
 * settings window of the PC (native/apps/linux_settings.app.rb).
 *
 * A thread of its own on the system bus (sd-bus, linux_bus.h), outside
 * FreeRTOS; the calls below only copy under a lock and wake it, never wait.
 *
 *   the list     the first Wi-Fi device's access points (GetAllAccessPoints:
 *                the SSID, the strength, whether secured), one per SSID (the
 *                strongest), the active one first; whether NetworkManager
 *                has a saved connection for each (Settings, read again at
 *                most every 30 s and after a connection). Read again when
 *                asked for and older than 3 s; a scan (RequestScan) when
 *                asked.
 *   connecting   a saved network: ActivateConnection on its connection. A
 *                new one: AddAndActivateConnection with the password
 *                (wpa-psk, or sae for WPA3-only), which NetworkManager keeps
 *                as it keeps the ones made in its own tools. The program
 *                also is a secret agent for this connection attempt only:
 *                when NetworkManager asks for a saved network's password
 *                (one kept by the desktop's own wallet, or one that changed),
 *                the password the user typed is the answer, else "none".
 *                A new connection that failed is deleted again (it is ours).
 *                The password is wiped from our memory when the attempt
 *                ends; it is never logged and never written by us.
 *   disconnect   the device's Disconnect (the connection stays saved).
 *
 * Nothing here runs unless linux_net_start was called (the machine's
 * system.settings); DBUS_SYSTEM_BUS_ADDRESS moves the bus (the checks' own
 * NetworkManager stand-in).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the thread (once). */
void linux_net_start(void);

/* The state and the list as text, more than cap: nothing written. scan true
 * asks for a scan too. First line:
 *   "wifi\t<status>\t<active ssid>\t<busy>\t<code>\t<arg>"
 *     status  none (no NetworkManager), nodevice, off (radio off), ok,
 *             starting (not asked yet)
 *     busy    connecting, scanning or ""
 *     code    the last outcome for the user: connected, disconnected,
 *             need_password, auth_failed, no_network, enterprise, failed,
 *             refused (arg: the reason), or ""; arg the SSID (refused: the
 *             reason)
 * then per network "ap\t<ssid>\t<strength 0-100>\t<secured 0|1>\t<active
 * 0|1>\t<saved 0|1>". */
int32_t linux_net_list(bool scan, char *buf, size_t cap);

/* Queue a connection (password "" for an open or a saved network) or the
 * disconnection. FMRB_ERR_BUSY while a connection is under way. */
fmrb_err_t linux_net_connect(const char *ssid, const char *password);
fmrb_err_t linux_net_disconnect(void);

/* The pure parts, for the host tests. */
typedef struct {
    char ssid[33];
    uint8_t strength;
    bool secured;
    bool active;
    bool saved;
} linux_net_ap_t;

/* Keeps the strongest entry of each SSID (an active one always), drops the
 * hidden ones (no SSID), and orders them: active first, then by strength,
 * then by name. Returns the new count. */
int linux_net_ap_tidy(linux_net_ap_t *ap, int n);

/* The security a new connection needs from the access point's flags:
 * "" open, "wpa-psk", "sae", or NULL when it is one we do not set up
 * (802.1X, WEP). */
const char *linux_net_key_mgmt(uint32_t flags, uint32_t wpa_flags, uint32_t rsn_flags);

/* The outcome code for a failed attempt from the device's state reason. */
const char *linux_net_failure_code(uint32_t reason, bool had_password);

#ifdef __cplusplus
}
#endif
