#pragma once

#include <stdint.h>
#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display mode enumeration
 */
typedef enum {
    FMRB_DISPLAY_MODE_NTSC_IPC = 0,  // NTSC via IPC (SPI to WROVER)
    FMRB_DISPLAY_MODE_SPI_DIRECT,    // Direct SPI control
    FMRB_DISPLAY_MODE_HEADLESS,      // No display output
    FMRB_DISPLAY_MODE_TAB5_DSI,      // Tab5 MIPI-DSI panel, driven locally (Modern)
    FMRB_DISPLAY_MODE_SDL2,          // SDL2 window (Linux simulation)
    FMRB_DISPLAY_MODE_ATOM_DISPLAY,  // M5Stack ATOM Display (HDMI)
    FMRB_DISPLAY_MODE_MAX
} fmrb_display_mode_t;

/**
 * @brief System configuration structure loaded from /etc/system_conf.toml
 */
typedef struct {
    char system_name[64];                   // System name string
    uint16_t display_width;                 // Physical display width in pixels
    uint16_t display_height;                // Physical display height in pixels
    uint8_t display_margin_x;              // Horizontal margin (left+right) in pixels
    uint8_t display_margin_y;              // Vertical margin (top+bottom) in pixels
    uint16_t default_user_app_width;        // Default user app window width
    uint16_t default_user_app_height;       // Default user app window height
    fmrb_display_mode_t display_mode;       // Display output mode
    bool debug_mode;                        // Debug mode enabled/disabled
    bool ble_auto_start;                    // Start BLE at boot (false = manual via menu)
    double mouse_scale_x;                   // Mouse X sensitivity multiplier
    double mouse_scale_y;                   // Mouse Y sensitivity multiplier
    char language[8];                       // UI language code, e.g. "ja" or "en"
} fmrb_system_config_t;


/**
 * Start the Family mruby OS kernel task
 * @return FMRB_OK on success, FMRB_ERR_* on failure
 */
fmrb_err_t fmrb_kernel_start(void);

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void);


const fmrb_system_config_t* fmrb_kernel_get_config(void);

/**
 * @brief HID routing table structure
 */
typedef struct {
    uint8_t target_pid;      // Target app PID for HID events (0xFF = none)
    uint8_t focused_window;  // Focused window ID (for future use)
    bool routing_enabled;    // Global enable/disable
} fmrb_hid_routing_t;

/**
 * @brief Get HID routing table (for Host Task)
 * @param routing Pointer to routing structure to fill
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_get_hid_routing(fmrb_hid_routing_t *routing);

/**
 * @brief Set HID target PID (for Kernel Ruby)
 * @param target_pid Target app PID (0xFF = none)
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_set_hid_target(uint8_t target_pid);

/**
 * @brief Set focused window ID (for future use)
 * @param window_id Window ID
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_set_focused_window(uint8_t window_id);

/**
 * @brief Enable/disable HID routing
 * @param enable true to enable, false to disable
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_kernel_enable_hid_routing(bool enable);

/**
 * @brief Sync file entry from system_conf.toml
 */
#define FMRB_SYNC_FILE_PATH_MAX 128

typedef struct {
    char src[FMRB_SYNC_FILE_PATH_MAX];
    char dest[FMRB_SYNC_FILE_PATH_MAX];
} fmrb_sync_file_entry_t;

/**
 * @brief Get sync_files configuration from system_conf.toml
 * @param entries Array to fill with sync file entries
 * @param max_entries Maximum number of entries to read
 * @return Number of entries read
 */
int fmrb_kernel_get_sync_files(fmrb_sync_file_entry_t *entries, int max_entries);

/**
 * @brief Bring one file on the graphics side up to date
 *
 * Compares the local size and CRC32 against the remote copy and transfers
 * only when they differ, so this is the call to use rather than checking
 * existence: an existence check leaves an edited asset stale forever.
 * Safe to call from any task; syncs are serialized.
 *
 * @param src Source path (local, e.g. "/usr/share/sounds/test.nsf")
 * @param dest Destination path (remote, e.g. "/flash/data/test.nsf")
 * @return FMRB_OK on success or if already up-to-date
 */
fmrb_err_t fmrb_kernel_sync_file(const char *src, const char *dest);

/**
 * @brief Config section key-value pair
 */
#define FMRB_CONFIG_MAX_ENTRIES 16
#define FMRB_CONFIG_KEY_MAX 32
#define FMRB_CONFIG_VAL_MAX 128

typedef struct {
    char key[FMRB_CONFIG_KEY_MAX];
    char value[FMRB_CONFIG_VAL_MAX];
} fmrb_config_kv_t;

typedef struct {
    fmrb_config_kv_t kv[FMRB_CONFIG_MAX_ENTRIES];
    int count;
} fmrb_config_table_t;

/**
 * @brief Read a TOML section from system_conf.toml
 * For [[array-of-tables]], each table fills one entry in tables_out (returns table count).
 * For [table], tables_out[0] is filled (returns 1).
 * @return Number of tables read, 0 if section not found
 */
int fmrb_kernel_get_config_section(const char *section,
                                   fmrb_config_table_t *tables_out,
                                   int max_tables);

#ifdef __cplusplus
}
#endif
