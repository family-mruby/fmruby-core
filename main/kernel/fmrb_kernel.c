#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <picoruby.h>
#include "fmrb_log.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_mem.h"
#include "fmrb_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_app.h"
#include "fmrb_kernel.h"
#include "fmrb_rtc.h"
#include "boot.h"
#include "fmrb_transport.h"
#include "host/host_task.h"
#include "fmrb_toml.h"
#include "fmrb_file_transfer_msg.h"
#include "fmrb_link_cobs.h"
#include "picoruby_fmrb_const.h"
#include "fmrb_keymap.h"
#include "fmrb_debug.h"
#include "fmrb_mp.h"

// Generated from kernel.rb (will be compiled by picorbc)
extern const uint8_t fmrb_kernel_irep[];

static const char *TAG = "kernel";

// Forward declarations
static bool init_hid_routing(void);

// System configuration (global, initialized once)
static fmrb_system_config_t g_system_config = {
    .system_name = "Family mruby OS",
    .display_width = 360,
    .display_height = 240,
    .display_margin_x = 0,
    .display_margin_y = 0,
    .default_user_app_width = 280,
    .default_user_app_height = 180,
    .display_mode = FMRB_DISPLAY_MODE_NTSC_IPC,
    .debug_mode = true,
    .ble_auto_start = true,
    .wifi_auto_start = false,
    .mouse_scale_x = 1.0,
    .mouse_scale_y = 1.0,
    .language = "en"
};

// Parse keyboard_layout string to enum
static fmrb_keymap_layout_t parse_keyboard_layout(const char* layout_str)
{
    if (strcmp(layout_str, "us") == 0) {
        return FMRB_KEYMAP_LAYOUT_US;
    }
    return FMRB_KEYMAP_LAYOUT_JP;  // Default
}

// Parse display_mode string to enum
static fmrb_display_mode_t parse_display_mode(const char* mode_str)
{
    if (strcmp(mode_str, "ntsc_ipc") == 0) {
        return FMRB_DISPLAY_MODE_NTSC_IPC;
    } else if (strcmp(mode_str, "spi_direct") == 0) {
        return FMRB_DISPLAY_MODE_SPI_DIRECT;
    } else if (strcmp(mode_str, "headless") == 0) {
        return FMRB_DISPLAY_MODE_HEADLESS;
    } else if (strcmp(mode_str, "tab5_dsi") == 0) {
        return FMRB_DISPLAY_MODE_TAB5_DSI;
    } else if (strcmp(mode_str, "sdl2") == 0) {
        return FMRB_DISPLAY_MODE_SDL2;
    } else if (strcmp(mode_str, "atom_display") == 0) {
        return FMRB_DISPLAY_MODE_ATOM_DISPLAY;
    }
    return FMRB_DISPLAY_MODE_NTSC_IPC;  // Default
}

// Name for the log line: the enum number alone tells the reader nothing, and an
// unrecognised mode_str silently becomes NTSC_IPC (see above), which is how a
// Modern board came to report NTSC in its boot log.
static const char *display_mode_name(fmrb_display_mode_t mode)
{
    switch (mode) {
    case FMRB_DISPLAY_MODE_NTSC_IPC:   return "ntsc_ipc";
    case FMRB_DISPLAY_MODE_SPI_DIRECT: return "spi_direct";
    case FMRB_DISPLAY_MODE_HEADLESS:   return "headless";
    case FMRB_DISPLAY_MODE_TAB5_DSI:   return "tab5_dsi";
    case FMRB_DISPLAY_MODE_SDL2:       return "sdl2";
    case FMRB_DISPLAY_MODE_ATOM_DISPLAY: return "atom_display";
    default:                           return "unknown";
    }
}

static bool read_system_config(void)
{
    const char *config_path = "/etc/system_conf.toml";
    const char *factory_path = "/etc/system_conf.factory.toml";

    FMRB_LOGI(TAG, "Loading system configuration from %s", config_path);

    // Load TOML file using helper function
    char errbuf[200];
    toml_table_t *conf = fmrb_toml_load_file(config_path, errbuf, sizeof(errbuf));

    if (!conf) {
        // Booting without a config leaves the machine unusable -- no desktop,
        // no radio, so no way in except USB. The live file can legitimately be
        // damaged (the desktop rewrites it, and power can go at any moment), so
        // fall back to the factory copy the firmware ships instead of refusing
        // to start. It is written once at build time and never at runtime.
        FMRB_LOGE(TAG, "Config load failed: %s", errbuf);
        FMRB_LOGW(TAG, "Falling back to factory settings (%s)", factory_path);
        conf = fmrb_toml_load_file(factory_path, errbuf, sizeof(errbuf));
        if (!conf) {
            FMRB_LOGE(TAG, "Factory config load failed too: %s", errbuf);
            return false;
        }
    }

    // Read system_name
    const char* system_name = fmrb_toml_get_string(conf, "system_name", g_system_config.system_name);
    if (system_name != g_system_config.system_name) {
        strncpy(g_system_config.system_name, system_name, sizeof(g_system_config.system_name) - 1);
        g_system_config.system_name[sizeof(g_system_config.system_name) - 1] = '\0';
        fmrb_sys_free((void*)system_name);
    }

    // Read display dimensions and margin
    g_system_config.display_width = (uint16_t)fmrb_toml_get_int(conf, "display_width",
                                                                 g_system_config.display_width);
    g_system_config.display_height = (uint16_t)fmrb_toml_get_int(conf, "display_height",
                                                                  g_system_config.display_height);
    g_system_config.display_margin_x = (uint8_t)fmrb_toml_get_int(conf, "display_margin_x",
                                                                    g_system_config.display_margin_x);
    g_system_config.display_margin_y = (uint8_t)fmrb_toml_get_int(conf, "display_margin_y",
                                                                    g_system_config.display_margin_y);

    // Read default user app window size
    g_system_config.default_user_app_width = (uint16_t)fmrb_toml_get_int(conf, "default_user_app_width",
                                                                          g_system_config.default_user_app_width);
    g_system_config.default_user_app_height = (uint16_t)fmrb_toml_get_int(conf, "default_user_app_height",
                                                                           g_system_config.default_user_app_height);

    // Read display mode
    const char* display_mode_str = fmrb_toml_get_string(conf, "display_mode", "ntsc_ipc");
    g_system_config.display_mode = parse_display_mode(display_mode_str);
    if (display_mode_str != NULL && strcmp(display_mode_str, "ntsc_ipc") != 0) {
        fmrb_sys_free((void*)display_mode_str);
    }

    // Read debug mode
    g_system_config.debug_mode = fmrb_toml_get_bool(conf, "debug_mode", g_system_config.debug_mode);
    fmrb_debug_mode_set(g_system_config.debug_mode);

    // BLE boot policy: true (default) keeps today's behavior; false leaves the
    // ~75 KB of internal RAM unclaimed until the desktop menu starts BLE by
    // hand (doc/reference/internal_ram_budget.md, D axis).
    g_system_config.ble_auto_start = fmrb_toml_get_bool(conf, "ble_auto_start", g_system_config.ble_auto_start);

    // WiFi boot policy: default OFF on every target. Turning it on also needs
    // credentials in /etc/wifi.toml. On retro (S3) WiFi and BLE are mutually
    // exclusive; boot resolves a both-on misconfiguration in favor of BLE.
    g_system_config.wifi_auto_start = fmrb_toml_get_bool(conf, "wifi_auto_start", g_system_config.wifi_auto_start);

    // Read mouse sensitivity
    g_system_config.mouse_scale_x = fmrb_toml_get_double(conf, "mouse_scale_x", g_system_config.mouse_scale_x);
    g_system_config.mouse_scale_y = fmrb_toml_get_double(conf, "mouse_scale_y", g_system_config.mouse_scale_y);

    // Read timezone and apply
    const char *tz = fmrb_toml_get_string(conf, "timezone", NULL);
    if (tz) {
        setenv("TZ", tz, 1);
        tzset();
        FMRB_LOGI(TAG, "Timezone set to: %s", tz);
        fmrb_sys_free((void *)tz);
    } else {
        FMRB_LOGW(TAG, "Timezone not configured, logs will use UTC");
    }

    // Read UI language ("ja" / "en"); used by FmrbI18n in launcher and prebuilt apps
    const char *language_str = fmrb_toml_get_string(conf, "language", NULL);
    if (language_str) {
        strncpy(g_system_config.language, language_str, sizeof(g_system_config.language) - 1);
        g_system_config.language[sizeof(g_system_config.language) - 1] = '\0';
        FMRB_LOGI(TAG, "Language set to: %s", g_system_config.language);
        fmrb_sys_free((void *)language_str);
    } else {
        FMRB_LOGI(TAG, "Language not configured, using default: %s", g_system_config.language);
    }

    // Read keyboard layout and apply
    const char *kbd_layout_str = fmrb_toml_get_string(conf, "keyboard_layout", NULL);
    if (kbd_layout_str) {
        fmrb_keymap_layout_t layout = parse_keyboard_layout(kbd_layout_str);
        fmrb_keymap_set_layout(layout);
        FMRB_LOGI(TAG, "Keyboard layout set to: %s", kbd_layout_str);
        fmrb_sys_free((void *)kbd_layout_str);
    } else {
        FMRB_LOGI(TAG, "Keyboard layout not configured, using default (JP)");
    }

    // Log loaded configuration
    FMRB_LOGI(TAG, "System Name: %s", g_system_config.system_name);
    FMRB_LOGI(TAG, "Display: %dx%d", g_system_config.display_width, g_system_config.display_height);
    FMRB_LOGI(TAG, "Default User App Window: %dx%d",
              g_system_config.default_user_app_width, g_system_config.default_user_app_height);
    FMRB_LOGI(TAG, "Display Mode: %s (%d)",
              display_mode_name(g_system_config.display_mode),
              g_system_config.display_mode);
    FMRB_LOGI(TAG, "Debug Mode: %s", g_system_config.debug_mode ? "enabled" : "disabled");
    FMRB_LOGI(TAG, "Mouse Scale: x=%.2f, y=%.2f", g_system_config.mouse_scale_x, g_system_config.mouse_scale_y);

    // Read [theme] section
    toml_table_t *theme_tab = toml_table_in(conf, "theme");
    if (theme_tab) {
        fmrb_theme_t theme = *fmrb_theme_get();  // Start from defaults
        theme.desktop_bg  = (uint8_t)fmrb_toml_get_int(theme_tab, "desktop_bg",  theme.desktop_bg);
        theme.menu_bg     = (uint8_t)fmrb_toml_get_int(theme_tab, "menu_bg",     theme.menu_bg);
        theme.window_bg   = (uint8_t)fmrb_toml_get_int(theme_tab, "window_bg",   theme.window_bg);
        theme.text        = (uint8_t)fmrb_toml_get_int(theme_tab, "text",        theme.text);
        theme.text_light  = (uint8_t)fmrb_toml_get_int(theme_tab, "text_light",  theme.text_light);
        theme.highlight   = (uint8_t)fmrb_toml_get_int(theme_tab, "highlight",   theme.highlight);
        theme.border      = (uint8_t)fmrb_toml_get_int(theme_tab, "border",      theme.border);
        theme.button      = (uint8_t)fmrb_toml_get_int(theme_tab, "button",      theme.button);
        theme.dir_color   = (uint8_t)fmrb_toml_get_int(theme_tab, "dir_color",   theme.dir_color);
        fmrb_theme_set(&theme);
        FMRB_LOGI(TAG, "Theme loaded from config");
    }

    // Dump full configuration for debugging
    FMRB_LOGI(TAG, "Full configuration:");
    dump_toml_table(conf, 0);

    // Clean up
    toml_free(conf);
    FMRB_LOGI(TAG, "System configuration loaded successfully");
    return true;
}


// Send file command to host_task and wait for completion
static fmrb_err_t send_file_cmd(file_cmd_t *cmd, file_cmd_result_t *result, uint32_t timeout_ms)
{
    result->done_sem = fmrb_semaphore_create_binary();
    if (!result->done_sem) {
        return FMRB_ERR_NO_MEMORY;
    }
    result->result = -99;
    cmd->result = result;

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_FILE_TRANSFER,
        .src_pid = 0,
        .size = sizeof(file_cmd_t)
    };
    memcpy(msg.data, cmd, sizeof(file_cmd_t));

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
    if (ret != FMRB_OK) {
        fmrb_semaphore_delete(result->done_sem);
        return ret;
    }

    fmrb_base_type_t sem_ret = fmrb_semaphore_take(result->done_sem, FMRB_MS_TO_TICKS(timeout_ms));
    fmrb_semaphore_delete(result->done_sem);

    if (sem_ret != FMRB_PASS) {
        return FMRB_ERR_TIMEOUT;
    }
    return (result->result == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Calculate CRC32 of a local file
static uint32_t calc_file_crc32(const char *path, uint32_t *out_size)
{
    fmrb_file_t fh;
    uint32_t crc = 0;
    uint32_t total = 0;

    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &fh) != FMRB_OK) {
        if (out_size) *out_size = 0;
        return 0;
    }

    uint8_t buf[256];
    size_t bytes_read;
    while (fmrb_hal_file_read(fh, buf, sizeof(buf), &bytes_read) == FMRB_OK && bytes_read > 0) {
        crc = fmrb_link_crc32_update(crc, buf, bytes_read);
        total += bytes_read;
    }

    fmrb_hal_file_close(fh);
    if (out_size) *out_size = total;
    return crc;
}

// Serializes fmrb_kernel_sync_file. The boot-time [[sync_files]] pass runs on
// the kernel task and apps reach the same function through FmrbGfx#sync_file,
// so the shared result below needs one caller at a time. Syncing is rare and
// slow, so a plain mutex is enough - and it keeps the large read buffer from
// being allocated by several tasks at once.
static fmrb_semaphore_t g_sync_file_mutex = NULL;

static bool init_file_sync(void)
{
    g_sync_file_mutex = fmrb_semaphore_create_mutex();
    if (!g_sync_file_mutex) {
        FMRB_LOGE(TAG, "Failed to create file sync mutex");
        return false;
    }
    return true;
}

// Body of fmrb_kernel_sync_file, called with g_sync_file_mutex held.
static fmrb_err_t sync_file_locked(const char *src, const char *dest)
{
    // Calculate local file CRC32 and size
    // fmrb_hal_file_open's build_path handles platform-specific prefix
    uint32_t local_size = 0;
    uint32_t local_crc = calc_file_crc32(src, &local_size);
    if (local_size == 0) {
        FMRB_LOGW(TAG, "File sync: source %s not found or empty", src);
        return FMRB_ERR_FAILED;
    }

    // Strip /flash/ prefix: WROVER's build_full_path prepends it
    const char *rel_dest = dest;
    if (strncmp(rel_dest, "/flash/", 7) == 0) {
        rel_dest += 7;
    } else if (rel_dest[0] == '/') {
        rel_dest += 1;
    }

    // Static result so a late host_task response cannot write through a
    // pointer into a dead stack frame. The mutex above keeps it to one user.
    static file_cmd_result_t s_sync_result;

    // Query remote file status
    file_cmd_t cmd = {0};
    memset(&s_sync_result, 0, sizeof(s_sync_result));
    cmd.cmd_type = FILE_CMD_STATUS;
    cmd.path_len = (uint16_t)strlen(rel_dest);
    memcpy(cmd.path, rel_dest, cmd.path_len);

    fmrb_err_t ret = send_file_cmd(&cmd, &s_sync_result, 5000);
    if (ret == FMRB_OK &&
        s_sync_result.data.status.exists &&
        s_sync_result.data.status.file_size == local_size &&
        s_sync_result.data.status.checksum == local_crc) {
        FMRB_LOGI(TAG, "File sync: %s up-to-date (size=%lu, crc=0x%08lx)",
                   rel_dest, (unsigned long)local_size, (unsigned long)local_crc);
        return FMRB_OK;
    }

    // Transfer needed
    FMRB_LOGI(TAG, "File sync: transferring %s -> %s (%lu bytes)",
               src, rel_dest, (unsigned long)local_size);

    // Read entire file into memory
    uint8_t *file_data = (uint8_t *)fmrb_sys_malloc(local_size);
    if (!file_data) {
        FMRB_LOGE(TAG, "File sync: malloc failed for %lu bytes",
                   (unsigned long)local_size);
        return FMRB_ERR_NO_MEMORY;
    }

    fmrb_file_t fh;
    if (fmrb_hal_file_open(src, FMRB_O_RDONLY, &fh) != FMRB_OK) {
        fmrb_sys_free(file_data);
        return FMRB_ERR_FAILED;
    }
    size_t bytes_read;
    size_t offset = 0;
    while (fmrb_hal_file_read(fh, file_data + offset, 512, &bytes_read) == FMRB_OK && bytes_read > 0) {
        offset += bytes_read;
    }
    fmrb_hal_file_close(fh);

    // Send transfer command (host_task will chunk and send to WROVER)
    memset(&cmd, 0, sizeof(cmd));
    memset(&s_sync_result, 0, sizeof(s_sync_result));
    cmd.cmd_type = FILE_CMD_TRANSFER;
    cmd.path_len = (uint16_t)strlen(rel_dest);
    memcpy(cmd.path, rel_dest, cmd.path_len);
    cmd.params.transfer.data = file_data;  // Ownership transfers to host_task
    cmd.params.transfer.data_len = local_size;

    ret = send_file_cmd(&cmd, &s_sync_result, 30000);  // 30s timeout for large files
    if (ret == FMRB_OK) {
        FMRB_LOGI(TAG, "File sync: transfer complete");
    } else {
        FMRB_LOGE(TAG, "File sync: transfer failed: %d", ret);
    }
    return ret;
}

/**
 * Public API: bring one file on the graphics side up to date.
 *
 * Skips the transfer when the remote copy already has the same size and
 * CRC32, which is what makes it safe to call on every boot and every app
 * start. Callers should use this rather than checking existence themselves:
 * an existence check leaves an edited asset stale forever.
 */
fmrb_err_t fmrb_kernel_sync_file(const char *src, const char *dest)
{
    if (!src || !dest) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (!g_sync_file_mutex) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (fmrb_semaphore_take(g_sync_file_mutex, FMRB_TICK_MAX) != FMRB_PASS) {
        return FMRB_ERR_TIMEOUT;
    }
    fmrb_err_t ret = sync_file_locked(src, dest);
    fmrb_semaphore_give(g_sync_file_mutex);
    return ret;
}

// Public API: Get sync_files entries from system_conf.toml
int fmrb_kernel_get_sync_files(fmrb_sync_file_entry_t *entries, int max_entries)
{
    if (!entries || max_entries <= 0) {
        return 0;
    }

    const char *config_path = "/etc/system_conf.toml";
    char errbuf[200];

    toml_table_t *conf = fmrb_toml_load_file(config_path, errbuf, sizeof(errbuf));
    if (!conf) {
        return 0;
    }

    toml_array_t *sync_arr = toml_array_in(conf, "sync_files");
    if (!sync_arr) {
        toml_free(conf);
        return 0;
    }

    int n = toml_array_nelem(sync_arr);
    int count = 0;

    for (int i = 0; i < n && count < max_entries; i++) {
        const toml_table_t *entry = toml_table_at(sync_arr, i);
        if (!entry) continue;

        const char *src = fmrb_toml_get_string(entry, "src", NULL);
        const char *dest = fmrb_toml_get_string(entry, "dest", NULL);
        if (!src || !dest) continue;

        strncpy(entries[count].src, src, sizeof(entries[count].src) - 1);
        entries[count].src[sizeof(entries[count].src) - 1] = '\0';
        strncpy(entries[count].dest, dest, sizeof(entries[count].dest) - 1);
        entries[count].dest[sizeof(entries[count].dest) - 1] = '\0';
        count++;
    }

    toml_free(conf);
    return count;
}

// Helper: extract key-value pairs from a TOML table into fmrb_config_table_t
static void extract_table_kv(const toml_table_t *tab, fmrb_config_table_t *out)
{
    out->count = 0;
    const char *key;
    for (int i = 0; (key = toml_key_in(tab, i)) && out->count < FMRB_CONFIG_MAX_ENTRIES; i++) {
        fmrb_config_kv_t *kv = &out->kv[out->count];
        strncpy(kv->key, key, FMRB_CONFIG_KEY_MAX - 1);
        kv->key[FMRB_CONFIG_KEY_MAX - 1] = '\0';

        // Try string first, then int, then bool, then double
        toml_datum_t vs = toml_string_in(tab, key);
        if (vs.ok) {
            strncpy(kv->value, vs.u.s, FMRB_CONFIG_VAL_MAX - 1);
            kv->value[FMRB_CONFIG_VAL_MAX - 1] = '\0';
            fmrb_sys_free(vs.u.s);
            out->count++;
            continue;
        }
        toml_datum_t vi = toml_int_in(tab, key);
        if (vi.ok) {
            snprintf(kv->value, FMRB_CONFIG_VAL_MAX, "%lld", (long long)vi.u.i);
            out->count++;
            continue;
        }
        toml_datum_t vb = toml_bool_in(tab, key);
        if (vb.ok) {
            strncpy(kv->value, vb.u.b ? "true" : "false", FMRB_CONFIG_VAL_MAX - 1);
            out->count++;
            continue;
        }
        toml_datum_t vd = toml_double_in(tab, key);
        if (vd.ok) {
            snprintf(kv->value, FMRB_CONFIG_VAL_MAX, "%g", vd.u.d);
            out->count++;
            continue;
        }
        // Skip nested tables/arrays (not supported in flat kv output)
    }
}

int fmrb_kernel_get_config_section(const char *section,
                                   fmrb_config_table_t *tables_out,
                                   int max_tables)
{
    if (!section || !tables_out || max_tables <= 0) {
        return 0;
    }

    const char *config_path = "/etc/system_conf.toml";
    char errbuf[200];
    toml_table_t *conf = fmrb_toml_load_file(config_path, errbuf, sizeof(errbuf));
    if (!conf) {
        return 0;
    }

    int result = 0;

    // Try as array-of-tables first ([[section]])
    toml_array_t *arr = toml_array_in(conf, section);
    if (arr) {
        int n = toml_array_nelem(arr);
        for (int i = 0; i < n && result < max_tables; i++) {
            const toml_table_t *entry = toml_table_at(arr, i);
            if (entry) {
                extract_table_kv(entry, &tables_out[result]);
                result++;
            }
        }
        toml_free(conf);
        return result;
    }

    // Try as single table ([section])
    toml_table_t *tab = toml_table_in(conf, section);
    if (tab) {
        extract_table_kv(tab, &tables_out[0]);
        result = 1;
    }

    toml_free(conf);
    return result;
}

/**
 * Initialize HAL layer and subsystems
 */
static bool init_hal(void)
{
    // Initialize HAL layer
    fmrb_err_t ret = fmrb_hal_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize HAL: %d", ret);
        return false;
    }
    FMRB_LOGI(TAG, "HAL initialized successfully");

    // Initialize message queue registry
    ret = fmrb_msg_init();
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize message queue: %d", ret);
        return false;
    }
    FMRB_LOGI(TAG, "Message queue initialized");

    // Initialize transport (singleton)
    fmrb_transport_config_t transport_config = {
        .timeout_ms = 1000, //default timeout for sync messages (can be overridden per message)
        .enable_retransmit = true,
        .max_retries = 3,
        .window_size = 8
    };

    ret = fmrb_transport_init(&transport_config);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to initialize Transport");
        return false;
    }

    // Note: Version check moved to host_task_init() after host_task starts

    return true;
}

/**
 * Start the kernel task
 */
#ifdef FMRB_KERNEL_ENGINE_SPINEL
/* Spinel engine (Phase 2 T2-5): the kernel VM is the Spinel-compiled combined
   kernel program (main/prebuild_scripts/spinel/, generated by rake spinel:gen).
   It is spawned through the normal app path as a NATIVE task so it gets the
   same PROC_ID_KERNEL context, message queue and lifecycle the mruby kernel
   does. fmrb_kernel_entry() runs the Ruby top-level bootstrap
   (FmrbKernelImpl.new.start -> main_loop); the "main_loop started" boot marker
   awaited by tools/dev_run_check.sh is emitted from Ruby (fmrb_kernel.rb). */
extern int fmrb_kernel_entry(void);
#include "fmrb_spinel_host.h"   /* fmrb_spinel_instance_begin/end (SP_MULTI_CTX) */

static void spinel_kernel_native(void *arg)
{
    /* execute_native_function passes the task context. Back the Spinel runtime
       with this task's estalloc pool (Phase 4 memory wiring): every allocation
       the kernel VM makes then comes from POOL_ID_KERNEL, isolated from other
       Spinel instances, and `ps` can report it via ctx->est. */
    fmrb_app_task_context_t *ctx = (fmrb_app_task_context_t *)arg;
    void  *pool = fmrb_get_mempool_ptr(ctx->mempool_id);
    size_t pool_size = fmrb_get_mempool_size(ctx->mempool_id);
    if (!pool || pool_size == 0) {
        FMRB_LOGE(TAG, "kernel mempool %d unavailable (ptr=%p size=%zu)",
                  ctx->mempool_id, pool, pool_size);
        return;
    }
    /* GC / string-heap collection thresholds well below the pool size
       (sp_instance_config contract: threshold < pool).
       pool/32, matching the desktop instance. The kernel allocates per input
       event, and a real mouse drag delivers a continuous stream of them, so a
       loose threshold lets a burst reach the top of the pool between
       collections -- which is fatal, since exhaustion goes through sp_oom_die
       and takes the firmware with it. Collecting early also means the GC's own
       mark stack gets claimed while the pool is still unfragmented. */
    size_t threshold = pool_size / 32;
    void *est = fmrb_spinel_instance_begin(pool, pool_size, threshold, threshold);
    if (!est) {
        FMRB_LOGE(TAG, "failed to create Spinel kernel instance (pool %d, %zu bytes)",
                  ctx->mempool_id, pool_size);
        return;
    }
    ctx->est = est;

    /* Runs to completion of the Ruby bootstrap; only returns on a fatal error,
       after which app_task_main performs the normal task cleanup. */
    fmrb_kernel_entry();

    fmrb_spinel_instance_end(est);
    ctx->est = NULL;
}
#endif /* FMRB_KERNEL_ENGINE_SPINEL */

fmrb_err_t fmrb_kernel_start(void)
{
    FMRB_LOGI(TAG, "Starting Family mruby OS Kernel...");

    // Initialize app context management (first time only)
    static bool context_initialized = false;
    if (context_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if(!read_system_config()){
        return FMRB_ERR_FAILED;
    }
    fmrb_mem_log_boot_snapshot("system_config");
    if(!init_hal()){
        return FMRB_ERR_FAILED;
    }
    fmrb_mem_log_boot_snapshot("hal");
    if(!init_hid_routing()){
        return FMRB_ERR_FAILED;
    }
    if(!init_file_sync()){
        return FMRB_ERR_FAILED;
    }
    fmrb_mem_log_boot_snapshot("file_sync");
    if(!fmrb_app_init()){
        return FMRB_ERR_FAILED;
    }
    fmrb_mem_log_boot_snapshot("app_init");

    // MicroPython guest VM. Only the single-instance lock is set up here; the
    // runtime itself comes up per app.
    if (fmrb_mp_init() != FMRB_OK) {
        return FMRB_ERR_FAILED;
    }
    fmrb_mem_log_boot_snapshot("mp_init");

    // Create host task
    int32_t result = fmrb_host_task_init();
    if (result < 0) {
        FMRB_LOGE(TAG, "Failed to start host task");
        return FMRB_ERR_FAILED;
    }

    int cnt = 0;
    while(!fmrb_host_is_ready())
    {
        FMRB_LOGI(TAG, "Waiting for host to be ready...");
        fmrb_task_delay_ms(100);
        if(cnt >= 30){
            FMRB_LOGE(TAG, "waiting host timeout!");
            return FMRB_ERR_FAILED;
        }
        cnt++;
    }
    fmrb_mem_log_boot_snapshot("host_ready");

    // Set the clock before anything can report a time, i.e. before the kernel
    // task exists, but only once the host has answered: on Modern the RTC sits
    // on the display's I2C bus and is reached through the display task's I2C
    // service, which is not up until the display has initialized (calling
    // earlier failed every boot with "mediation unavailable"). A missing or
    // flat RTC is not fatal -- the system runs with whatever the clock says --
    // so the return value only decides whether to log.
    fmrb_err_t rtc_ret = fmrb_rtc_sync_system_clock();
    if (rtc_ret != FMRB_OK && rtc_ret != FMRB_ERR_NOT_SUPPORTED) {
        FMRB_LOGW(TAG, "Clock not set from RTC (err=%d); times will be wrong",
                  rtc_ret);
    }

    // Create kernel task using spawn API. The Spinel engine runs the same
    // PROC_ID_KERNEL slot as a NATIVE task (fmrb_kernel_entry); the mruby engine
    // runs the compiled kernel irep. Everything else (context, queue, window,
    // lifecycle) is identical so behavior stays in parity.
    fmrb_spawn_attr_t attr = {
        .app_id = PROC_ID_KERNEL,
        .type = APP_TYPE_KERNEL,
        .name = "fmrb_kernel",
#ifdef FMRB_KERNEL_ENGINE_SPINEL
        .vm_type = FMRB_VM_TYPE_NATIVE,
        .native_func = spinel_kernel_native,
#else
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = fmrb_kernel_irep,
#endif
        .stack_words = FMRB_KERNEL_TASK_STACK_SIZE,
        .priority = FMRB_KERNEL_TASK_PRIORITY,
        .flags = FMRB_KERNEL_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .rounded_corners = true
    };

    int32_t kernel_id;
    fmrb_err_t ret = fmrb_app_spawn(&attr, &kernel_id);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to spawn kernel task: %d", ret);
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "Kernel task spawned successfully (id=%ld)", kernel_id);

    context_initialized = true;
    return FMRB_OK;
}

/**
 * Stop the kernel task
 */
void fmrb_kernel_stop(void)
{
    FMRB_LOGI(TAG, "Stopping kernel task...");

    // fmrb_app_kill() asks first and forces only after its grace period. The
    // kernel does not handle a "stop" addressed to itself, so this call always
    // waits out the grace (~1s) and then takes the forced path -- which still
    // releases the resources. Shutdown is that much slower; if it ever matters,
    // handle the self-stop in the kernel message loop.
    fmrb_app_kill(PROC_ID_KERNEL);

    FMRB_LOGI(TAG, "Kernel task stopped");
}

const fmrb_system_config_t* fmrb_kernel_get_config(void)
{
    return &g_system_config;
}

// HID routing table (protected by mutex)
static fmrb_hid_routing_t g_hid_routing = {
    .target_pid = 0xFF,        // No target initially
    .focused_window = 0xFF,
    .routing_enabled = true
};

static fmrb_semaphore_t g_hid_routing_mutex = NULL;

// Initialize HID routing table
static bool init_hid_routing(void)
{
    g_hid_routing_mutex = fmrb_semaphore_create_mutex();
    if (!g_hid_routing_mutex) {
        FMRB_LOGE(TAG, "Failed to create HID routing mutex");
        return false;
    }
    FMRB_LOGI(TAG, "HID routing initialized");
    return true;
}

// Get HID routing table (for Host Task)
fmrb_err_t fmrb_kernel_get_hid_routing(fmrb_hid_routing_t *routing)
{
    if (!routing) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_semaphore_take(g_hid_routing_mutex, FMRB_TICK_MAX);
    *routing = g_hid_routing;
    fmrb_semaphore_give(g_hid_routing_mutex);

    return FMRB_OK;
}

// Set HID target PID (for Kernel Ruby)
fmrb_err_t fmrb_kernel_set_hid_target(uint8_t target_pid)
{
    fmrb_semaphore_take(g_hid_routing_mutex, FMRB_TICK_MAX);
    g_hid_routing.target_pid = target_pid;
    fmrb_semaphore_give(g_hid_routing_mutex);

    FMRB_LOGD(TAG, "HID target set to PID=%d", target_pid);
    return FMRB_OK;
}

// Set focused window ID (for future use)
fmrb_err_t fmrb_kernel_set_focused_window(uint8_t window_id)
{
    fmrb_semaphore_take(g_hid_routing_mutex, FMRB_TICK_MAX);
    g_hid_routing.focused_window = window_id;
    fmrb_semaphore_give(g_hid_routing_mutex);

    FMRB_LOGI(TAG, "Focused window set to ID=%d", window_id);
    return FMRB_OK;
}

// Enable/disable HID routing
fmrb_err_t fmrb_kernel_enable_hid_routing(bool enable)
{
    fmrb_semaphore_take(g_hid_routing_mutex, FMRB_TICK_MAX);
    g_hid_routing.routing_enabled = enable;
    fmrb_semaphore_give(g_hid_routing_mutex);

    FMRB_LOGI(TAG, "HID routing %s", enable ? "enabled" : "disabled");
    return FMRB_OK;
}
