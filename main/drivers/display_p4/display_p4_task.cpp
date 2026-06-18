// Family mruby Modern (ESP32-P4 / M5Stack Tab5) display task.
//
// Phase 1: bring up the Tab5 MIPI-DSI panel via our own LovyanGFX device
// (lgfx_tab5.hpp) and draw a boot test pattern, to validate the panel, DSI
// bus, LDO and backlight on real hardware. The gfx-command compositor and
// touch->mouse handling are added in later phases.

#include "display_p4_task.h"
#include "lgfx_tab5.hpp"

#include "fmrb_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

static const char *TAG = "display_p4";

// Tab5 board pins / I2C (see fmrb_pin_assign.h; kept local to the driver).
#define TAB5_I2C_PORT   I2C_NUM_1
#define TAB5_I2C_SDA    GPIO_NUM_31
#define TAB5_I2C_SCL    GPIO_NUM_32
#define TAB5_TP_INT     GPIO_NUM_23
#define TAB5_BACKLIGHT  GPIO_NUM_22
#define PI4IO1_ADDR     0x43   // PI4IO GPIO expander #1 (LCD/touch reset)
#define PI4IO2_ADDR     0x44   // PI4IO GPIO expander #2 (power rails)

static LGFX_Tab5 g_lcd;

static inline void pi4io_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

// Power/reset sequence for the Tab5 panel + touch, mirroring M5GFX's Tab5
// bring-up. Uses a temporary IDF i2c_master bus on port 1 that is deleted
// before LovyanGFX initializes its own I2C (port 1) for the GT911 touch, so
// the two drivers never own the peripheral at the same time.
static void tab5_power_on(void)
{
    // TP INT high selects the GT911 I2C address (0x14).
    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = 1ULL << TAB5_TP_INT;
    int_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&int_cfg);
    gpio_set_level(TAB5_TP_INT, 1);

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = TAB5_I2C_PORT;
    bus_cfg.sda_io_num = TAB5_I2C_SDA;
    bus_cfg.scl_io_num = TAB5_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        FMRB_LOGE(TAG, "PI4IO i2c bus init failed");
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t io1 = NULL, io2 = NULL;
    dev_cfg.device_address = PI4IO1_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io1);
    dev_cfg.device_address = PI4IO2_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io2);

    // PI4IO #1: directions/pulls, resets driven LOW (OUT_SET bit4=LCD, bit5=touch)
    pi4io_write(io1, 0x03, 0x7F);
    pi4io_write(io1, 0x05, 0x46);
    pi4io_write(io1, 0x07, 0x00);
    pi4io_write(io1, 0x0D, 0x7F);
    pi4io_write(io1, 0x0B, 0x7F);
    // PI4IO #2: power rails / IO config
    pi4io_write(io2, 0x03, 0xB9);
    pi4io_write(io2, 0x07, 0x06);
    pi4io_write(io2, 0x0D, 0xB9);
    pi4io_write(io2, 0x0B, 0xF9);
    pi4io_write(io2, 0x09, 0x40);
    pi4io_write(io2, 0x11, 0xBF);
    pi4io_write(io2, 0x05, 0x89);

    vTaskDelay(pdMS_TO_TICKS(10));
    // PI4IO #1: release LCD + touch resets (OUT_SET resets HIGH)
    pi4io_write(io1, 0x05, 0x76);

    // Hand the INT pin back as input for the touch controller.
    gpio_set_direction(TAB5_TP_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_master_bus_rm_device(io1);
    i2c_master_bus_rm_device(io2);
    i2c_del_master_bus(bus);

    // Backlight full-on (PWM brightness control can be added later).
    gpio_config_t bl_cfg = {};
    bl_cfg.pin_bit_mask = 1ULL << TAB5_BACKLIGHT;
    bl_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl_cfg);
    gpio_set_level(TAB5_BACKLIGHT, 1);
}

static void draw_test_pattern(void)
{
    const int w = g_lcd.width();
    const int h = g_lcd.height();

    // Vertical colour bars to confirm the panel + DSI are alive.
    static const uint32_t bars[] = {
        0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00,
        0xFF00FF, 0x00FFFF, 0xFFFFFF, 0x808080,
    };
    const int n = (int)(sizeof(bars) / sizeof(bars[0]));
    const int bw = w / n;
    for (int i = 0; i < n; ++i) {
        g_lcd.fillRect(i * bw, 0, (i == n - 1) ? (w - i * bw) : bw, h,
                       g_lcd.color888((bars[i] >> 16) & 0xFF,
                                      (bars[i] >> 8) & 0xFF,
                                      bars[i] & 0xFF));
    }

    g_lcd.setTextColor(0xFFFFFF, 0x000000);
    g_lcd.setTextSize(4);
    g_lcd.setCursor(20, 20);
    g_lcd.print("Family mruby Modern");
    g_lcd.setCursor(20, 70);
    g_lcd.printf("ESP32-P4 / Tab5  %dx%d", w, h);
}

static void display_p4_task(void *arg)
{
    (void)arg;
    FMRB_LOGI(TAG, "Tab5 display: power on");
    tab5_power_on();

    FMRB_LOGI(TAG, "Tab5 display: LGFX init");
    if (!g_lcd.init()) {
        FMRB_LOGE(TAG, "LGFX init failed");
    } else {
        FMRB_LOGI(TAG, "LGFX init OK (%dx%d)", g_lcd.width(), g_lcd.height());
        draw_test_pattern();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" fmrb_err_t display_p4_task_init(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(display_p4_task, "display_p4",
                                            8192, NULL, 5, NULL, 1);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create display_p4 task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

extern "C" fmrb_err_t display_p4_task_deinit(void)
{
    return FMRB_OK;
}
