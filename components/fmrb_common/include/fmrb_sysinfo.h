#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Remember the Bluetooth address this machine is actually using.
 *
 * Called by the BLE task once the host has synced with the controller. On a
 * machine whose radio is a separate chip (the Modern board reaches WiFi and
 * BLE through an ESP32-C6 over SDIO) this is the only place the address can be
 * had: the efuse of the chip running this firmware has no BT MAC in it, and
 * esp_read_mac(ESP_MAC_BT) fails there by design.
 *
 * @param mac six bytes, least significant first (NimBLE's own order).
 */
void fmrb_sysinfo_set_bt_mac(const uint8_t mac[6]);

/**
 * @brief Write the Bluetooth address as "AA:BB:CC:DD:EE:FF".
 *
 * Prefers the address the BLE host reported; falls back to the efuse BT MAC,
 * which is what a machine with an on-chip radio has. Writes "-" and returns
 * false when neither is available (BLE not built in, or not yet synced on a
 * machine with no BT MAC of its own).
 *
 * @param buf destination, 18 bytes or more.
 */
bool fmrb_sysinfo_bt_mac_str(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
