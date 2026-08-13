// M5Stack Tab5 Keyboard accessory driver (I2C, STM32F030 @ 0x6D).
// Uses HID mode: polls INT_STA register, reads HID_EVENT (modifier + keycode),
// and sends key events to host_task via the _us variants of
// fmrb_host_send_key_down/up: HID mode resolves the keyboard's legends (Sym
// layer included) to US HID codes, so the host must decode them with the US
// table, not the configured keyboard_layout (which is for external keyboards).

#include "tab5_keyboard.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"
#include "fmrb_keymap.h"
#include "host/host_task.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include "fmrb_task_config.h"

static const char *TAG = "tab5_kbd";

// STM32F030 keyboard controller registers
#define KBD_ADDR            0x6D
#define KBD_REG_INT_CFG     0x00
#define KBD_REG_INT_STA     0x01
#define KBD_REG_EVENT_NUM   0x02
#define KBD_REG_BRIGHTNESS  0x03
#define KBD_REG_KBD_MODE    0x10
#define KBD_REG_HID_EVENT   0x30
#define KBD_MODE_HID        1
#define KBD_POLL_MS         50
#define KBD_I2C_TIMEOUT_MS  100

// HID modifier byte → FMRB modifier bitmap conversion
// USB HID: bit0=LCtrl, bit1=LShift, bit2=LAlt, bit4=RCtrl, bit5=RShift, bit6=RAlt
// FMRB:    bit0=LShift, bit1=RShift, bit2=LCtrl, bit3=RCtrl, bit4=LAlt, bit5=RAlt
static uint8_t hid_mod_to_fmrb(uint8_t hid_mod) {
    uint8_t f = 0;
    if (hid_mod & 0x01) f |= FMRB_KEYMAP_MOD_LCTRL;
    if (hid_mod & 0x02) f |= FMRB_KEYMAP_MOD_LSHIFT;
    if (hid_mod & 0x04) f |= FMRB_KEYMAP_MOD_LALT;
    if (hid_mod & 0x10) f |= FMRB_KEYMAP_MOD_RCTRL;
    if (hid_mod & 0x20) f |= FMRB_KEYMAP_MOD_RSHIFT;
    if (hid_mod & 0x40) f |= FMRB_KEYMAP_MOD_RALT;
    return f;
}

static i2c_master_bus_handle_t g_bus = NULL;
static i2c_master_dev_handle_t g_dev = NULL;

// Previous HID state for detecting key up / modifier changes
static uint8_t g_prev_modifier = 0;
static uint8_t g_prev_keycode  = 0;

static bool reg_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(g_dev, buf, 2, KBD_I2C_TIMEOUT_MS) == ESP_OK;
}

static bool reg_read(uint8_t reg, uint8_t *val) {
    esp_err_t err = i2c_master_transmit_receive(g_dev, &reg, 1, val, 1, KBD_I2C_TIMEOUT_MS);
    return err == ESP_OK;
}

static bool reg_read_bytes(uint8_t reg, uint8_t *buf, uint8_t len) {
    esp_err_t err = i2c_master_transmit_receive(g_dev, &reg, 1, buf, len, KBD_I2C_TIMEOUT_MS);
    return err == ESP_OK;
}

static void tab5_keyboard_task(void *arg) {
    (void)arg;
    FMRB_LOGI(TAG, "Tab5 keyboard task started (addr=0x%02X, poll=%dms)", KBD_ADDR, KBD_POLL_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(KBD_POLL_MS));

        // Check interrupt status
        uint8_t status = 0;
        if (!reg_read(KBD_REG_INT_STA, &status)) continue;
        if (!(status & 0x02)) {
            // No HID event this poll (bit1 = HID mode trigger). The keyboard
            // is event-driven: a held key produces no events between press
            // and release, so "no event" must NOT be treated as a release
            // (that used to synthesize key_up 50 ms after every press and
            // broke hold-to-move in games). The release arrives as an
            // explicit event with keycode == 0, handled below.
            continue;
        }

        // Get event count
        uint8_t count = 0;
        if (!reg_read(KBD_REG_EVENT_NUM, &count) || count == 0) {
            reg_write(KBD_REG_INT_STA, 0);  // Clear interrupt
            continue;
        }

        // Process all events in queue
        for (uint8_t i = 0; i < count && i < 32; i++) {
            uint8_t buf[2] = {0xFF, 0xFF};
            if (!reg_read_bytes(KBD_REG_HID_EVENT, buf, 2)) break;
            if (buf[0] == 0xFF && buf[1] == 0xFF) break;  // Queue empty

            uint8_t modifier = buf[0];
            uint8_t keycode  = buf[1];
            uint8_t mod_fmrb = hid_mod_to_fmrb(modifier);

            // Handle modifier changes
            uint8_t mod_changed = modifier ^ g_prev_modifier;
            if (mod_changed) {
                // HID modifier key scancodes
                static const uint8_t mod_scancodes[] = {
                    0xE0, 0xE1, 0xE2, 0xE3,  // LCtrl, LShift, LAlt, LGUI
                    0xE4, 0xE5, 0xE6, 0xE7,  // RCtrl, RShift, RAlt, RGUI
                };
                for (int j = 0; j < 8; j++) {
                    if (mod_changed & (1 << j)) {
                        if (modifier & (1 << j)) {
                            fmrb_host_send_key_down_us(mod_scancodes[j], mod_scancodes[j], mod_fmrb);
                        } else {
                            fmrb_host_send_key_up_us(mod_scancodes[j], mod_scancodes[j], mod_fmrb);
                        }
                    }
                }
            }

            // Handle key press/release
            if (keycode != 0 && keycode != g_prev_keycode) {
                // New key or key changed
                if (g_prev_keycode != 0) {
                    fmrb_host_send_key_up_us(g_prev_keycode, g_prev_keycode, mod_fmrb);
                }
                FMRB_LOGD(TAG, "Key DOWN: hid=0x%02X mod=0x%02X", keycode, modifier);
                fmrb_host_send_key_down_us(keycode, keycode, mod_fmrb);
            } else if (keycode == 0 && g_prev_keycode != 0) {
                // Key released
                FMRB_LOGD(TAG, "Key UP: hid=0x%02X", g_prev_keycode);
                fmrb_host_send_key_up_us(g_prev_keycode, g_prev_keycode, mod_fmrb);
            }

            g_prev_modifier = modifier;
            g_prev_keycode  = keycode;
        }

        // Clear interrupt status
        reg_write(KBD_REG_INT_STA, 0);
    }
}

fmrb_err_t tab5_keyboard_init(void) {
    // Create I2C0 master bus for keyboard
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = FMRB_PIN_KBD_I2C_SDA,
        .scl_io_num = FMRB_PIN_KBD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &g_bus);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "I2C0 bus init failed: %d (keyboard not connected?)", err);
        return FMRB_OK;  // Non-fatal: keyboard is optional
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = KBD_ADDR,
        .scl_speed_hz = 100000,
    };

    err = i2c_master_bus_add_device(g_bus, &dev_cfg, &g_dev);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to add keyboard device: %d", err);
        i2c_del_master_bus(g_bus);
        g_bus = NULL;
        return FMRB_OK;
    }

    // Probe for keyboard presence
    err = i2c_master_probe(g_bus, KBD_ADDR, KBD_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        FMRB_LOGI(TAG, "Tab5 Keyboard not detected at 0x%02X", KBD_ADDR);
        i2c_master_bus_rm_device(g_dev);
        i2c_del_master_bus(g_bus);
        g_dev = NULL;
        g_bus = NULL;
        return FMRB_OK;
    }

    // Read firmware version
    uint8_t ver = 0;
    if (reg_read(0xFE, &ver)) {
        FMRB_LOGI(TAG, "Tab5 Keyboard detected: fw=0x%02X", ver);
    }

    // Set HID mode
    if (!reg_write(KBD_REG_KBD_MODE, KBD_MODE_HID)) {
        FMRB_LOGE(TAG, "Failed to set HID mode");
    }

    // Clear event queue and interrupt status
    reg_write(KBD_REG_EVENT_NUM, 0);
    reg_write(KBD_REG_INT_STA, 0);

    // Start polling task
    BaseType_t ok = xTaskCreatePinnedToCore(
        tab5_keyboard_task, "tab5_kbd", FMRB_TAB5_KBD_TASK_STACK_SIZE, NULL,
        FMRB_TAB5_KBD_TASK_PRIORITY, NULL, FMRB_TAB5_KBD_TASK_CORE);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create keyboard task");
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "Tab5 Keyboard initialized (HID mode)");
    return FMRB_OK;
}

fmrb_err_t tab5_keyboard_deinit(void) {
    if (g_dev) {
        i2c_master_bus_rm_device(g_dev);
        g_dev = NULL;
    }
    if (g_bus) {
        i2c_del_master_bus(g_bus);
        g_bus = NULL;
    }
    return FMRB_OK;
}
