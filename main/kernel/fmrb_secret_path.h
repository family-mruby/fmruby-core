#pragma once

/**
 * @file fmrb_secret_path.h
 * @brief Which files hold the board's connection secrets, and what the
 *        remote file services (BLE console, devctl) may do with them.
 *
 * Secrets are the Asterism credentials (/home/asterism/<name>.tar, and
 * anything else in that directory but config.toml), never read remotely, and
 * the WiFi settings (/home/wifi.toml, /etc/wifi.toml), read over BLE only
 * while the board's settings window is open (the owner asked for it on the
 * board) and never over devctl. All of them, and /home/asterism/config.toml,
 * are written only while the window is open (doc/ruby_asterism/report/cf2.md).
 *
 * Pure C over strings, no file system access: the rules are tested on the
 * host (test/secret_path).
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The WiFi settings, in the order wifi_task reads them: the first that
 *  exists is the only one used (doc/ruby_asterism/report/cf1.md 4.3). */
#define FMRB_WIFI_TOML_HOME_PATH "/home/wifi.toml"
#define FMRB_WIFI_TOML_ETC_PATH  "/etc/wifi.toml"

typedef enum {
    FMRB_PATH_PLAIN = 0,  /**< an ordinary file */
    FMRB_PATH_SETTINGS,   /**< readable, but written only while the window is open */
    FMRB_PATH_WIFI,       /**< the WiFi password: read only while the window is open */
    FMRB_PATH_SECRET,     /**< holds a key: never read remotely */
} fmrb_path_class_t;

typedef enum {
    FMRB_REMOTE_FS_GET = 0,
    FMRB_REMOTE_FS_PUT,
    FMRB_REMOTE_FS_RM,
    FMRB_REMOTE_FS_RENAME,
} fmrb_remote_fs_op_t;

typedef enum {
    FMRB_REMOTE_FS_ALLOW = 0,
    FMRB_REMOTE_FS_DENY_SECRET,  /**< it would hand out a secret */
    FMRB_REMOTE_FS_DENY_CLOSED,  /**< needs the settings window, and it is closed */
} fmrb_remote_fs_verdict_t;

/**
 * @brief Classify a path as the file HAL would resolve it.
 *
 * Empty and "." segments are skipped, a leading "/flash" (the HAL's alias of
 * the root) is dropped, and a path with a ".." segment counts as a secret
 * (no remote client needs one, and littlefs would resolve it).
 */
fmrb_path_class_t fmrb_path_classify(const char *path);

/**
 * @brief Whether a secret can live under this path: "/", "/home", "/etc",
 *        "/home/asterism". Renaming one of these would move secrets out.
 */
bool fmrb_path_contains_secret(const char *path);

/**
 * @brief The verdict for one remote file operation.
 * @param op          what is asked
 * @param path        the file (for RENAME, the source)
 * @param to          RENAME's destination, NULL otherwise
 * @param window_open whether the settings window is open
 */
fmrb_remote_fs_verdict_t fmrb_remote_fs_check(fmrb_remote_fs_op_t op, const char *path,
                                              const char *to, bool window_open);

/**
 * @brief The non-secret part of a wifi.toml, as a JSON object.
 *
 * Writes {"src":"<path>","on":<enable>,"ssid":"<ssid>","conn":<bool>,"ip":"<ip>"}
 * into out (always terminated, cut short when cap is too small). The
 * password is never looked at: only the "enable" and "ssid" keys are read.
 *
 * @param toml_text the file's text, NULL when there is no file
 * @param src       which file it is ("/home/wifi.toml", "/etc/wifi.toml"), "" for none
 * @param connected whether WiFi is connected now
 * @param ip        the current IPv4 address, "" or NULL when there is none
 * @return the length written
 */
size_t fmrb_wifi_summary_json(const char *toml_text, const char *src, bool connected,
                              const char *ip, char *out, size_t cap);

/** Why a board boots into the setup mode (doc/ruby_asterism/report/cf3.md). */
typedef enum {
    FMRB_SETUP_NONE = 0,     /**< the normal boot */
    FMRB_SETUP_NO_WIFI,      /**< neither WiFi settings file exists */
    FMRB_SETUP_BUTTON,       /**< the button was held at boot */
} fmrb_setup_reason_t;

/**
 * @brief Whether this boot is the setup mode (BLE only, settings accepted).
 * @param enabled            system_conf setup_mode (false: never)
 * @param have_wifi_settings one of the FMRB_WIFI_TOML_* files exists
 * @param button_held        the button was down at boot
 */
fmrb_setup_reason_t fmrb_setup_mode_reason(bool enabled, bool have_wifi_settings,
                                           bool button_held);

/** @brief A short English line for the log and the screen. */
const char *fmrb_setup_reason_text(fmrb_setup_reason_t reason);

#ifdef __cplusplus
}
#endif
