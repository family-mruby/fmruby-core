#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "toml.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The saved Wi-Fi networks (doc/wifi_settings/plan.md), as read from and
 * written to /home/wifi.toml (else /etc/wifi.toml). Pure C over a parsed
 * TOML table and a text buffer: no file system, no radio, so the rules are
 * tested on the host (rake wificonf:test).
 *
 * The file:
 *
 *   [wifi]
 *   enable = true
 *   hostname = "fmruby"          # optional
 *   ssid = "home"                # optional: the one network of the old form
 *   password = "..."
 *
 *   [[networks]]                 # up to WIFI_CONF_MAX, newest first
 *   ssid = "cafe"
 *   password = "..."             # "" or left out: an open network
 *   hidden = true                # optional: not announced, tried by name
 *
 * The old form (one ssid/password in [wifi]) is still read; it becomes the
 * first network. Writing always produces the new form.
 */

#define WIFI_CONF_MAX       5
#define WIFI_CONF_SSID_MAX  32
#define WIFI_CONF_PASS_MAX  64
#define WIFI_CONF_HOST_MAX  31

typedef struct {
    char ssid[WIFI_CONF_SSID_MAX + 1];
    char password[WIFI_CONF_PASS_MAX + 1];
    bool hidden;
} wifi_conf_net_t;

typedef struct {
    bool enable;
    char hostname[WIFI_CONF_HOST_MAX + 1];
    int count;
    wifi_conf_net_t nets[WIFI_CONF_MAX];
} wifi_conf_t;

/** An empty configuration: enabled, no hostname, no networks. */
void wifi_conf_init(wifi_conf_t *conf);

/**
 * @brief Fill conf from a parsed wifi.toml (NULL root: the empty one).
 *
 * A [wifi] table is enabled only by enable = true (as it always was); a
 * file without one (only [[networks]]) and no file at all are enabled. Networks beyond WIFI_CONF_MAX, and
 * a repeated SSID, are dropped; an SSID or password too long is dropped
 * with its entry. xfree releases the strings tomlc99 hands out (the
 * allocator it was given: fmrb_toml_xfree on the devices). Returns the
 * number of networks.
 */
int wifi_conf_from_toml(const toml_table_t *root, wifi_conf_t *conf, void (*xfree)(void *));

/** Index of the network with this SSID, -1 when not saved. */
int wifi_conf_find(const wifi_conf_t *conf, const char *ssid);

/**
 * @brief Save a network as the newest: put first, an older entry of the
 *        same name removed, the oldest dropped when there are WIFI_CONF_MAX.
 *
 * Its hidden flag is kept when it was saved before.
 * @return true when the file would change (new, moved or a new password).
 */
bool wifi_conf_add(wifi_conf_t *conf, const char *ssid, const char *password);

/** Forget a network. @return true when it was saved. */
bool wifi_conf_forget(wifi_conf_t *conf, const char *ssid);

/**
 * @brief The file's text for conf (always the new form).
 * @return the length written, or the length needed (> cap - 1: nothing
 *         usable written).
 */
size_t wifi_conf_to_text(const wifi_conf_t *conf, char *out, size_t cap);

/** Overwrite the passwords (before the struct is freed or reused). */
void wifi_conf_wipe(wifi_conf_t *conf);

#ifdef __cplusplus
}
#endif
