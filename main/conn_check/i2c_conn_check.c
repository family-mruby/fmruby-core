#include "i2c_conn_check.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/i2c_master.h"
#endif

static const char *TAG = "i2c_conn_check";

#ifndef CONFIG_IDF_TARGET_LINUX

// I2C master bus handles
static i2c_master_bus_handle_t i2c1_bus_handle = NULL;
static i2c_master_bus_handle_t i2c2_bus_handle = NULL;

// I2C configuration
#define I2C_MASTER_FREQ_HZ     100000  // 100kHz standard mode

int i2c_conn_check_init(void)
{
    esp_err_t ret;

    // Initialize I2C1
    i2c_master_bus_config_t i2c1_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = FMRB_PIN_I2C1_SDA,
        .scl_io_num = FMRB_PIN_I2C1_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ret = i2c_new_master_bus(&i2c1_config, &i2c1_bus_handle);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to initialize I2C1: %s", esp_err_to_name(ret));
        return -1;
    }
    FMRB_LOGI(TAG, "I2C1 initialized - SDA:%d SCL:%d", FMRB_PIN_I2C1_SDA, FMRB_PIN_I2C1_SCL);

    // Initialize I2C2
    i2c_master_bus_config_t i2c2_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = FMRB_PIN_I2C2_SDA,
        .scl_io_num = FMRB_PIN_I2C2_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ret = i2c_new_master_bus(&i2c2_config, &i2c2_bus_handle);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to initialize I2C2: %s", esp_err_to_name(ret));
        // Continue with I2C1 only
    } else {
        FMRB_LOGI(TAG, "I2C2 initialized - SDA:%d SCL:%d", FMRB_PIN_I2C2_SDA, FMRB_PIN_I2C2_SCL);
    }

    return 0;
}

int i2c_conn_check_scan(int bus_num)
{
    i2c_master_bus_handle_t bus_handle = NULL;

    if (bus_num == 1) {
        bus_handle = i2c1_bus_handle;
    } else if (bus_num == 2) {
        bus_handle = i2c2_bus_handle;
    }

    if (bus_handle == NULL) {
        FMRB_LOGE(TAG, "I2C%d not initialized", bus_num);
        return -1;
    }

    FMRB_LOGI(TAG, "Scanning I2C%d bus...", bus_num);

    int devices_found = 0;

    // Scan addresses 0x08 to 0x77 (valid 7-bit addresses)
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t ret = i2c_master_probe(bus_handle, addr, 50);  // 50ms timeout
        if (ret == ESP_OK) {
            FMRB_LOGI(TAG, "I2C%d: Device found at address 0x%02X", bus_num, addr);
            devices_found++;
        }
    }

    if (devices_found == 0) {
        FMRB_LOGI(TAG, "I2C%d: No devices found", bus_num);
    } else {
        FMRB_LOGI(TAG, "I2C%d: %d device(s) found", bus_num, devices_found);
    }

    return devices_found;
}

void i2c_conn_check_deinit(void)
{
    if (i2c1_bus_handle != NULL) {
        i2c_del_master_bus(i2c1_bus_handle);
        i2c1_bus_handle = NULL;
        FMRB_LOGI(TAG, "I2C1 deinitialized");
    }

    if (i2c2_bus_handle != NULL) {
        i2c_del_master_bus(i2c2_bus_handle);
        i2c2_bus_handle = NULL;
        FMRB_LOGI(TAG, "I2C2 deinitialized");
    }
}

#else
// Linux stubs

int i2c_conn_check_init(void)
{
    FMRB_LOGI(TAG, "I2C not available on Linux target");
    return 0;
}

int i2c_conn_check_scan(int bus_num)
{
    (void)bus_num;
    return 0;
}

void i2c_conn_check_deinit(void)
{
}

#endif
