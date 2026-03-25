// I2C keyboard driver for ATOM_DISPLAY mode
// Polls an I2C slave keyboard (address 0x5F) and sends key events
// to the host task via fmrb_host_send_key_down/up.

#include <string.h>
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_err.h"
#include "fmrb_task_config.h"
#include "fmrb_pin_assign.h"
#include "i2c_keyboard.h"
#include "m5gfx_task.h"
#include "host_task.h"

#include "driver/i2c_master.h"

static const char *TAG = "i2c_kbd";

#define I2C_KBD_SLAVE_ADDR  0x5F
#define I2C_KBD_POLL_MS     50
#define I2C_KBD_TIMEOUT_MS  100

// Special keycodes from keyboard.ino
#define KBD_KEY_LEFT   180
#define KBD_KEY_UP     181
#define KBD_KEY_DOWN   182
#define KBD_KEY_RIGHT  183

static i2c_master_bus_handle_t g_bus_handle = NULL;
static i2c_master_dev_handle_t g_dev_handle = NULL;
static fmrb_task_handle_t g_task_handle = NULL;
static volatile bool g_running = false;

// Previous key state for detecting key-up
static uint8_t g_prev_key = 0;

// Mouse cursor position (tracked locally for arrow key movement)
static int g_mouse_x = 240;
static int g_mouse_y = 160;
#define MOUSE_STEP 8

static bool is_arrow_key(uint8_t keycode) {
    return keycode >= KBD_KEY_LEFT && keycode <= KBD_KEY_RIGHT;
}

static void handle_arrow_key(uint8_t keycode) {
    switch (keycode) {
    case KBD_KEY_LEFT:  g_mouse_x -= MOUSE_STEP; break;
    case KBD_KEY_UP:    g_mouse_y -= MOUSE_STEP; break;
    case KBD_KEY_DOWN:  g_mouse_y += MOUSE_STEP; break;
    case KBD_KEY_RIGHT: g_mouse_x += MOUSE_STEP; break;
    }
    // Clamp to screen bounds
    if (g_mouse_x < 0) g_mouse_x = 0;
    if (g_mouse_y < 0) g_mouse_y = 0;
    if (g_mouse_x >= M5GFX_SPRITE_WIDTH) g_mouse_x = M5GFX_SPRITE_WIDTH - 1;
    if (g_mouse_y >= M5GFX_SPRITE_HEIGHT) g_mouse_y = M5GFX_SPRITE_HEIGHT - 1;

    fmrb_host_send_mouse_move(g_mouse_x, g_mouse_y);
}

static void i2c_keyboard_task(void *arg)
{
    FMRB_LOGI(TAG, "I2C keyboard task started (addr=0x%02X, poll=%dms)",
              I2C_KBD_SLAVE_ADDR, I2C_KBD_POLL_MS);

    while (g_running) {
        uint8_t keycode = 0;
        esp_err_t err = i2c_master_receive(g_dev_handle, &keycode, 1, I2C_KBD_TIMEOUT_MS);

        if (err != ESP_OK) {
            fmrb_task_delay_ms(I2C_KBD_POLL_MS);
            continue;
        }

        if (keycode != 0) {
            if (is_arrow_key(keycode)) {
                // Arrow keys → mouse movement (repeats while held)
                handle_arrow_key(keycode);
            } else if (g_prev_key == 0) {
                // Regular key pressed (new press only)
                FMRB_LOGI(TAG, "Key down: %d (0x%02X) '%c'",
                           keycode, keycode,
                           (keycode >= 0x20 && keycode < 0x7F) ? keycode : '.');
                fmrb_host_send_key_down(keycode, keycode, 0);
            }
        } else if (g_prev_key != 0 && !is_arrow_key(g_prev_key)) {
            // Regular key released
            FMRB_LOGI(TAG, "Key up: %d", g_prev_key);
            fmrb_host_send_key_up(g_prev_key, g_prev_key, 0);
        }

        g_prev_key = keycode;
        fmrb_task_delay_ms(I2C_KBD_POLL_MS);
    }

    FMRB_LOGI(TAG, "I2C keyboard task stopped");
    fmrb_task_delete(NULL);
}

fmrb_err_t i2c_keyboard_init(void)
{
    if (g_task_handle) {
        FMRB_LOGW(TAG, "Already initialized");
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Initializing I2C keyboard (SDA=%d, SCL=%d)...",
              FMRB_PIN_I2C1_SDA, FMRB_PIN_I2C1_SCL);

    // Initialize I2C master bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = FMRB_PIN_I2C1_SDA,
        .scl_io_num = FMRB_PIN_I2C1_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &g_bus_handle);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to create I2C master bus: %d", err);
        return FMRB_ERR_FAILED;
    }

    // Add keyboard device
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_KBD_SLAVE_ADDR,
        .scl_speed_hz = 100000,
    };

    err = i2c_master_bus_add_device(g_bus_handle, &dev_config, &g_dev_handle);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to add I2C keyboard device: %d", err);
        i2c_del_master_bus(g_bus_handle);
        g_bus_handle = NULL;
        return FMRB_ERR_FAILED;
    }

    // Probe for keyboard
    err = i2c_master_probe(g_bus_handle, I2C_KBD_SLAVE_ADDR, 100);
    if (err != ESP_OK) {
        FMRB_LOGW(TAG, "I2C keyboard not detected at 0x%02X (will retry in task)",
                   I2C_KBD_SLAVE_ADDR);
    } else {
        FMRB_LOGI(TAG, "I2C keyboard detected at 0x%02X", I2C_KBD_SLAVE_ADDR);
    }

    // Start polling task
    g_running = true;

    fmrb_base_type_t result = fmrb_task_create_ex(
        i2c_keyboard_task,
        "i2c_kbd",
        FMRB_I2C_KBD_TASK_STACK_SIZE,
        NULL,
        FMRB_I2C_KBD_TASK_PRIORITY,
        &g_task_handle,
        FMRB_I2C_KBD_TASK_FLAGS
    );

    if (result != FMRB_PASS) {
        FMRB_LOGE(TAG, "Failed to create I2C keyboard task");
        g_running = false;
        i2c_master_bus_rm_device(g_dev_handle);
        i2c_del_master_bus(g_bus_handle);
        g_dev_handle = NULL;
        g_bus_handle = NULL;
        return FMRB_ERR_FAILED;
    }

    FMRB_LOGI(TAG, "I2C keyboard initialized");
    return FMRB_OK;
}

fmrb_err_t i2c_keyboard_deinit(void)
{
    if (!g_task_handle) {
        return FMRB_OK;
    }

    FMRB_LOGI(TAG, "Deinitializing I2C keyboard...");
    g_running = false;
    fmrb_task_delay_ms(I2C_KBD_POLL_MS * 3);
    g_task_handle = NULL;

    if (g_dev_handle) {
        i2c_master_bus_rm_device(g_dev_handle);
        g_dev_handle = NULL;
    }
    if (g_bus_handle) {
        i2c_del_master_bus(g_bus_handle);
        g_bus_handle = NULL;
    }

    FMRB_LOGI(TAG, "I2C keyboard deinitialized");
    return FMRB_OK;
}
