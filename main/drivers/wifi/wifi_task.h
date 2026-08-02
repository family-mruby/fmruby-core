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
// Credentials come from /etc/wifi.toml.

/**
 * @brief Read /etc/wifi.toml and start the WiFi station.
 *
 * Returns FMRB_ERR_NOT_FOUND when WiFi is disabled or unconfigured
 * (missing file, enable=false, or empty ssid); callers treat that as a
 * non-error "feature off" state.
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

#ifdef __cplusplus
}
#endif
