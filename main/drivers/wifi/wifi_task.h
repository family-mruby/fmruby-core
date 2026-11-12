#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi STA. On Modern (ESP32-P4) the radio lives on the ESP32-C6
// coprocessor and is reached through esp_hosted / esp_wifi_remote over the
// same SDIO transport as BLE; call after the transport is up (i.e. after
// ble_task_init, or wifi_task_init itself waits for it). On Retro (Narya
// v3, ESP32-S3) the radio is native and mutually exclusive with BLE.
// Settings come from /home/wifi.toml, else /etc/wifi.toml: up to five saved
// networks (wifi_conf.h). A manager task joins the strongest one in reach
// and takes the Wi-Fi page's requests (doc/wifi_settings).

/**
 * @brief Read /home/wifi.toml (else /etc/wifi.toml) and start the WiFi station.
 *
 * Returns FMRB_ERR_NOT_FOUND when WiFi is disabled ([wifi] without
 * enable = true); callers treat that as a non-error "feature off" state.
 * A missing file or one with no network starts the station all the same,
 * so the Wi-Fi page can add one.
 */
fmrb_err_t wifi_task_init(void);

/**
 * @brief True once wifi_task_init has started the station (radio claimed).
 *
 * A weak false-returning default exists in ble_task.c so targets that do
 * not compile wifi_task.c (ATOM) still link.
 */
bool wifi_task_is_started(void);

/**
 * @brief Block until an IP address is acquired.
 * @param timeout_ms Max wait in milliseconds.
 * @return true if connected with an IP.
 */
bool wifi_wait_for_ip(uint32_t timeout_ms);

/**
 * @brief True while the station is connected and has an IP.
 */
bool wifi_is_connected(void);

/**
 * @brief Copy the current IPv4 address as a dotted string ("0.0.0.0" if none).
 */
void wifi_get_ip_str(char *buf, size_t buf_len);

/**
 * @brief Copy the configured SSID (empty string when WiFi is unconfigured).
 */
void wifi_get_ssid(char *buf, size_t buf_len);

/**
 * @brief Copy the mDNS hostname (defaults to "fmruby").
 */
void wifi_get_hostname(char *buf, size_t buf_len);

/**
 * @brief The Wi-Fi page's text (fmrb_platform_sys.h wifi_list): the state
 *        line and one "ap" line per network in reach, then the saved ones
 *        out of reach. Never a password.
 *
 * "wifi\t<status>\t<active>\t<busy>\t<code>\t<arg>\tforget" where status is
 * ok, or disabled / ble / failed when the station did not start; then
 * "ap\t<ssid>\t<strength 0-100>\t<secured>\t<active>\t<saved>\t<in reach>".
 * @param scan also ask for a scan (answered later; the list then changes)
 * @return the length (more than cap - 1: buf holds only the beginning)
 */
int32_t wifi_list_text(bool scan, char *buf, size_t cap);

/** @brief Queue joining a network (password "" : the saved one, or open). */
fmrb_err_t wifi_request_connect(const char *ssid, const char *password);

/** @brief Queue leaving the network; no automatic joining until the next connect. */
fmrb_err_t wifi_request_disconnect(void);

/** @brief Queue forgetting a saved network (removed from /home/wifi.toml). */
fmrb_err_t wifi_request_forget(const char *ssid);

/**
 * @brief Save the networks in a wifi.toml text in /home/wifi.toml, beside
 *        the ones saved there (wifi_conf_merge: the first sent is the newest,
 *        a saved SSID gets the new password, five at most).
 *
 * The BLE console's Wi-Fi settings (BLE command 0x19). Works whether the
 * station runs or not; while it runs the manager task does the write and
 * this waits for it. One caller at a time (the BLE file service).
 * @param saved the networks in the file afterwards
 */
fmrb_err_t wifi_save_networks(const char *text, size_t len, int *saved);

#ifdef __cplusplus
}
#endif
