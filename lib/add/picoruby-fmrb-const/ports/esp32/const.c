#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <string.h>
#include <stdio.h>

#include "sdkconfig.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#endif

#include "fmrb.h"
#include "fmrb_app.h"
#include "fmrb_task_config.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_pin_assign.h"
#include "status_led.h"
#include "../../include/picoruby_fmrb_const.h"

/* Default theme (can be overridden by system_conf.toml before VMs start) */
static fmrb_theme_t g_theme = {
    .desktop_bg  = 0xF6,
    .menu_bg     = 0xC5,
    .window_bg   = 0xFF,
    .text        = 0x00,
    .text_light  = 0xFF,
    .highlight   = 0xEE,
    .border      = 0x60,
    .button      = 0x60,
    .dir_color   = 0x03,
};

void fmrb_theme_set(const fmrb_theme_t *theme)
{
    if (theme) {
        memcpy(&g_theme, theme, sizeof(fmrb_theme_t));
    }
}

const fmrb_theme_t* fmrb_theme_get(void)
{
    return &g_theme;
}

// FmrbHw.pin_status(pin) -> Integer (usage type)
static mrb_value mrb_fmrb_hw_pin_status(mrb_state *mrb, mrb_value klass)
{
    mrb_int pin;
    mrb_get_args(mrb, "i", &pin);
    fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
    return mrb_fixnum_value(st.usage);
}

// FmrbHw.pin_available?(pin) -> true/false
static mrb_value mrb_fmrb_hw_pin_available_p(mrb_state *mrb, mrb_value klass)
{
    mrb_int pin;
    mrb_get_args(mrb, "i", &pin);
    return mrb_bool_value(fmrb_pin_manager_is_available(pin));
}

// FmrbHw.pin_status_all -> Array of Integer (index=pin, value=usage)
static mrb_value mrb_fmrb_hw_pin_status_all(mrb_state *mrb, mrb_value klass)
{
    mrb_value ary = mrb_ary_new_capa(mrb, FMRB_PIN_MAX);
    for (int i = 0; i < FMRB_PIN_MAX; i++) {
        fmrb_pin_status_t st = fmrb_pin_manager_get_status(i);
        mrb_ary_push(mrb, ary, mrb_fixnum_value(st.usage));
    }
    return ary;
}

// FmrbHw.pin_count -> Integer
static mrb_value mrb_fmrb_hw_pin_count(mrb_state *mrb, mrb_value klass)
{
    return mrb_fixnum_value(FMRB_PIN_MAX);
}

#ifndef CONFIG_IDF_TARGET_LINUX
static const char* reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_USB:       return "USB";
        case ESP_RST_JTAG:      return "JTAG";
        case ESP_RST_EFUSE:     return "EFUSE";
        case ESP_RST_PWR_GLITCH:return "PWR_GLITCH";
        case ESP_RST_CPU_LOCKUP:return "CPU_LOCKUP";
        case ESP_RST_UNKNOWN:
        default:                return "UNKNOWN";
    }
}

static const char* chip_model_str(esp_chip_model_t m)
{
    switch (m) {
        case CHIP_ESP32:    return "ESP32";
        case CHIP_ESP32S2:  return "ESP32-S2";
        case CHIP_ESP32S3:  return "ESP32-S3";
        case CHIP_ESP32C3:  return "ESP32-C3";
        case CHIP_ESP32H2:  return "ESP32-H2";
        case CHIP_ESP32C2:  return "ESP32-C2";
        case CHIP_ESP32C6:  return "ESP32-C6";
        default:            return "UNKNOWN";
    }
}
#endif

void mrb_picoruby_fmrb_const_init_impl(mrb_state *mrb)
{
    // Define FmrbConst module
    struct RClass *const_module = mrb_define_module(mrb, "FmrbConst");

    // Platform constants
#ifdef CONFIG_IDF_TARGET_LINUX
    mrb_define_const(mrb, const_module, "PLATFORM", mrb_str_new_cstr(mrb, "linux"));
#else
    mrb_define_const(mrb, const_module, "PLATFORM", mrb_str_new_cstr(mrb, "esp32"));
#endif

    // Process ID constants
    mrb_define_const(mrb, const_module, "PROC_ID_KERNEL", mrb_fixnum_value(PROC_ID_KERNEL));
    mrb_define_const(mrb, const_module, "PROC_ID_HOST", mrb_fixnum_value(PROC_ID_HOST));
    mrb_define_const(mrb, const_module, "PROC_ID_SYSTEM_APP", mrb_fixnum_value(PROC_ID_SYSTEM_APP));
    mrb_define_const(mrb, const_module, "PROC_ID_USER_APP0", mrb_fixnum_value(PROC_ID_USER_APP0));
    mrb_define_const(mrb, const_module, "PROC_ID_USER_APP1", mrb_fixnum_value(PROC_ID_USER_APP1));
    mrb_define_const(mrb, const_module, "PROC_ID_USER_APP2", mrb_fixnum_value(PROC_ID_USER_APP2));

    // Process state constants
    mrb_define_const(mrb, const_module, "PROC_STATE_FREE", mrb_fixnum_value(PROC_STATE_FREE));
    mrb_define_const(mrb, const_module, "PROC_STATE_INIT", mrb_fixnum_value(PROC_STATE_INIT));
    mrb_define_const(mrb, const_module, "PROC_STATE_RUNNING", mrb_fixnum_value(PROC_STATE_RUNNING));
    mrb_define_const(mrb, const_module, "PROC_STATE_SUSPENDED", mrb_fixnum_value(PROC_STATE_SUSPENDED));
    mrb_define_const(mrb, const_module, "PROC_STATE_STOPPING", mrb_fixnum_value(PROC_STATE_STOPPING));

    // Message type constants
    mrb_define_const(mrb, const_module, "MSG_TYPE_APP_CONTROL", mrb_fixnum_value(FMRB_MSG_TYPE_APP_CONTROL));
    mrb_define_const(mrb, const_module, "MSG_TYPE_APP_GFX", mrb_fixnum_value(FMRB_MSG_TYPE_APP_GFX));
    mrb_define_const(mrb, const_module, "MSG_TYPE_APP_AUDIO", mrb_fixnum_value(FMRB_MSG_TYPE_APP_AUDIO));
    mrb_define_const(mrb, const_module, "MSG_TYPE_HID_EVENT", mrb_fixnum_value(FMRB_MSG_TYPE_HID_EVENT));

    // App control message subtypes
    mrb_define_const(mrb, const_module, "APP_CTRL_SPAWN", mrb_fixnum_value(FMRB_APP_CTRL_SPAWN));
    mrb_define_const(mrb, const_module, "APP_CTRL_KILL", mrb_fixnum_value(FMRB_APP_CTRL_KILL));
    mrb_define_const(mrb, const_module, "APP_CTRL_SUSPEND", mrb_fixnum_value(FMRB_APP_CTRL_SUSPEND));
    mrb_define_const(mrb, const_module, "APP_CTRL_RESUME", mrb_fixnum_value(FMRB_APP_CTRL_RESUME));

    // Path length constant
    mrb_define_const(mrb, const_module, "MAX_PATH_LEN", mrb_fixnum_value(FMRB_MAX_PATH_LEN));

    // Version constants (from fmrb.h)
    mrb_define_const(mrb, const_module, "OS_VERSION",   mrb_str_new_cstr(mrb, FMRB_OS_VERSION));
    mrb_define_const(mrb, const_module, "GA_VERSION",   mrb_str_new_cstr(mrb, FMRB_GA_VERSION));
    mrb_define_const(mrb, const_module, "LINK_VERSION", mrb_fixnum_value(FMRB_LINK_VERSION));

    // System info constants (frozen at init)
#ifdef CONFIG_IDF_TARGET_LINUX
    mrb_define_const(mrb, const_module, "IDF_VERSION",   mrb_str_new_cstr(mrb, "-"));
    mrb_define_const(mrb, const_module, "MAC_ADDRESS",   mrb_str_new_cstr(mrb, "-"));
    mrb_define_const(mrb, const_module, "CHIP_MODEL",    mrb_str_new_cstr(mrb, "linux"));
    mrb_define_const(mrb, const_module, "CHIP_REVISION", mrb_str_new_cstr(mrb, "-"));
    mrb_define_const(mrb, const_module, "CHIP_CORES",    mrb_fixnum_value(0));
    mrb_define_const(mrb, const_module, "FLASH_SIZE_MB", mrb_fixnum_value(0));
    mrb_define_const(mrb, const_module, "PSRAM_SIZE_MB", mrb_fixnum_value(0));
    mrb_define_const(mrb, const_module, "RESET_REASON",  mrb_str_new_cstr(mrb, "-"));
#else
    // IDF version (build-time string, e.g. "v5.5.4")
    mrb_define_const(mrb, const_module, "IDF_VERSION",
                     mrb_str_new_cstr(mrb, IDF_VER));

    // MAC address (BLE, formatted "AA:BB:CC:DD:EE:FF").
    // ESP32-S3 derives per-interface MACs from a single base MAC
    // (WiFi STA = base, SoftAP = base+1, BLE = base+2, Eth = base+3),
    // so reading WIFI_STA would show a value that differs from the BLE
    // device-name suffix by 2 in the last byte. WiFi is disabled in this
    // build, so the BLE MAC is the relevant one to display in About.
    {
        uint8_t mac[6] = {0};
        char mac_str[18] = {0};
        if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            snprintf(mac_str, sizeof(mac_str), "??:??:??:??:??:??");
        }
        mrb_define_const(mrb, const_module, "MAC_ADDRESS",
                         mrb_str_new_cstr(mrb, mac_str));
    }

    // Chip info: model / revision / cores
    {
        esp_chip_info_t info;
        esp_chip_info(&info);
        char rev_str[16];
        // info.revision is encoded as major*100 + minor (IDF v5.x)
        snprintf(rev_str, sizeof(rev_str), "v%d.%d",
                 info.revision / 100, info.revision % 100);
        mrb_define_const(mrb, const_module, "CHIP_MODEL",
                         mrb_str_new_cstr(mrb, chip_model_str(info.model)));
        mrb_define_const(mrb, const_module, "CHIP_REVISION",
                         mrb_str_new_cstr(mrb, rev_str));
        mrb_define_const(mrb, const_module, "CHIP_CORES",
                         mrb_fixnum_value(info.cores));
    }

    // Flash size (MB, rounded down)
    {
        uint32_t flash_bytes = 0;
        if (esp_flash_get_size(NULL, &flash_bytes) != ESP_OK) {
            flash_bytes = 0;
        }
        mrb_define_const(mrb, const_module, "FLASH_SIZE_MB",
                         mrb_fixnum_value(flash_bytes / (1024 * 1024)));
    }

    // PSRAM size (MB)
    {
        size_t psram_bytes = 0;
#if CONFIG_SPIRAM
        psram_bytes = esp_psram_get_size();
#endif
        mrb_define_const(mrb, const_module, "PSRAM_SIZE_MB",
                         mrb_fixnum_value((mrb_int)(psram_bytes / (1024 * 1024))));
    }

    // Reset reason
    mrb_define_const(mrb, const_module, "RESET_REASON",
                     mrb_str_new_cstr(mrb, reset_reason_str(esp_reset_reason())));
#endif

    // Status LED error pattern constants
    mrb_define_const(mrb, const_module, "LED_ERR_NONE",              mrb_fixnum_value(FMRB_LED_STATUS_NONE));
    mrb_define_const(mrb, const_module, "LED_ERR_FATAL",             mrb_fixnum_value(FMRB_LED_STATUS_FATAL));
    mrb_define_const(mrb, const_module, "LED_ERR_VERSION_MISMATCH",  mrb_fixnum_value(FMRB_LED_STATUS_VERSION_MISMATCH));

    // Theme color constants (from g_theme, possibly overridden by system_conf.toml)
    const fmrb_theme_t *t = fmrb_theme_get();
    mrb_define_const(mrb, const_module, "THEME_DESKTOP_BG", mrb_fixnum_value(t->desktop_bg));
    mrb_define_const(mrb, const_module, "THEME_MENU_BG", mrb_fixnum_value(t->menu_bg));
    mrb_define_const(mrb, const_module, "THEME_WINDOW_BG", mrb_fixnum_value(t->window_bg));
    mrb_define_const(mrb, const_module, "THEME_TEXT", mrb_fixnum_value(t->text));
    mrb_define_const(mrb, const_module, "THEME_TEXT_LIGHT", mrb_fixnum_value(t->text_light));
    mrb_define_const(mrb, const_module, "THEME_HIGHLIGHT", mrb_fixnum_value(t->highlight));
    mrb_define_const(mrb, const_module, "THEME_BORDER", mrb_fixnum_value(t->border));
    mrb_define_const(mrb, const_module, "THEME_BUTTON", mrb_fixnum_value(t->button));
    mrb_define_const(mrb, const_module, "THEME_DIR_COLOR", mrb_fixnum_value(t->dir_color));

    // Define FmrbHw module for hardware resource management
    struct RClass *hw_module = mrb_define_module(mrb, "FmrbHw");

    // Pin usage type constants
    mrb_define_const(mrb, hw_module, "PIN_UNUSED",    mrb_fixnum_value(FMRB_PIN_UNUSED));
    mrb_define_const(mrb, hw_module, "PIN_SYSTEM_EXCLUSIVE", mrb_fixnum_value(FMRB_PIN_SYSTEM_EXCLUSIVE));
    mrb_define_const(mrb, hw_module, "PIN_USER_GPIO", mrb_fixnum_value(FMRB_PIN_USER_GPIO));
    mrb_define_const(mrb, hw_module, "PIN_USER_I2C",  mrb_fixnum_value(FMRB_PIN_USER_I2C));
    mrb_define_const(mrb, hw_module, "PIN_USER_RMT",  mrb_fixnum_value(FMRB_PIN_USER_RMT));
    mrb_define_const(mrb, hw_module, "PIN_USER_SPI",  mrb_fixnum_value(FMRB_PIN_USER_SPI));
    mrb_define_const(mrb, hw_module, "PIN_USER_PWM",  mrb_fixnum_value(FMRB_PIN_USER_PWM));
    mrb_define_const(mrb, hw_module, "PIN_USER_UART", mrb_fixnum_value(FMRB_PIN_USER_UART));

#ifndef CONFIG_IDF_TARGET_LINUX
    // I2C bus pin assignments (ESP32 only)
    mrb_define_const(mrb, hw_module, "PIN_I2C1_SDA", mrb_fixnum_value(FMRB_PIN_I2C1_SDA));
    mrb_define_const(mrb, hw_module, "PIN_I2C1_SCL", mrb_fixnum_value(FMRB_PIN_I2C1_SCL));
    mrb_define_const(mrb, hw_module, "PIN_I2C2_SDA", mrb_fixnum_value(FMRB_PIN_I2C2_SDA));
    mrb_define_const(mrb, hw_module, "PIN_I2C2_SCL", mrb_fixnum_value(FMRB_PIN_I2C2_SCL));
#endif

    // Class methods
    mrb_define_class_method(mrb, hw_module, "pin_status", mrb_fmrb_hw_pin_status, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, hw_module, "pin_available?", mrb_fmrb_hw_pin_available_p, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, hw_module, "pin_status_all", mrb_fmrb_hw_pin_status_all, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, hw_module, "pin_count", mrb_fmrb_hw_pin_count, MRB_ARGS_NONE());
}
