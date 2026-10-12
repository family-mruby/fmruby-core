// The board display interface (board/board_display.h) for the M5Stack Tab5.
//
// The panel varies by production batch (lgfx_tab5.hpp), so bring-up powers
// the panel and touch through the PI4IO expanders, probes the touch
// controller to tell the variant, and only then configures the device. The
// internal I2C bus is LovyanGFX's (touch), so every other transaction --
// the codec, the PI4IO headphone detect, mruby apps -- goes through lgfx's
// I2C helpers under the display's I2C lock.

#include "board_display.h"
#include "lgfx_tab5.hpp"

#include "fmrb_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

static const char *TAG = "display_local";

// Tab5 board pins / I2C (see fmrb_pin_assign.h; kept local to the driver).
#define TAB5_I2C_PORT   I2C_NUM_1
#define TAB5_I2C_SDA    GPIO_NUM_31
#define TAB5_I2C_SCL    GPIO_NUM_32
#define TAB5_TP_INT     GPIO_NUM_23
#define ST_TOUCH_ADDR   0x55   // ST7121/ST7123 integrated touch controller
#define GT911_TOUCH_ADDR 0x14  // GT911 touch (ILI9881C units), TP INT high at reset
#define PI4IO1_ADDR     0x43   // PI4IO GPIO expander #1 (LCD/touch reset)
#define PI4IO2_ADDR     0x44   // PI4IO GPIO expander #2 (power rails)

// PI4IO #1 runtime bits: P1 = speaker amp enable, P7 = headphone detect
#define PI4IO_REG_OUT_SET   0x05
#define PI4IO_REG_IN_STA    0x0F
#define PI4IO_BIT_SPK_EN    (1 << 1)
#define PI4IO_BIT_HP_DETECT (1 << 7)
#define PI4IO_I2C_FREQ      400000

static LGFX_Tab5 g_lcd;

lgfx::LGFX_Device *board_display_lcd(void) { return &g_lcd; }

// ============================================================
// PI4IO I2C helper
// ============================================================

static inline void pi4io_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

// ============================================================
// Power/reset sequence for the Tab5 panel + touch.
// ============================================================

// Determine which panel/touch combination this Tab5 carries, following
// M5GFX's Tab5 detection (managed_components/m5stack__m5gfx/src/M5GFX.cpp).
// The touch controller answers before the DSI link is up, so the variant is
// decided over I2C: the ST7123 touch block (0x55) reports a firmware version
// that tells ST7121 (1) from ST7123 (3), and a GT911 at 0x14 means this is an
// ILI9881C unit. ST7121 and ST7123 differ in DSI lane rate, so the DSI ID
// cannot safely tell them apart -- the touch probe has to come first. The
// model number on the rear label does not decide this either: a unit labelled
// ST7123 was measured reporting touch firmware version 1, i.e. ST7121.
//
// The ST touch block needs a few tens of ms after reset before it answers
// (~50 ms measured by M5GFX), so keep polling until one of the two chips
// responds rather than probing once.
//
// Called with the IDF i2c_master bus still owned by tab5_power_on(), after
// the LCD/touch resets have been released.
static Tab5PanelVariant tab5_probe_panel(i2c_master_bus_handle_t bus) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    dev_cfg.device_address = ST_TOUCH_ADDR;
    i2c_master_dev_handle_t st = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &st) != ESP_OK) {
        FMRB_LOGW(TAG, "panel probe: i2c device add failed, assuming ILI9881C");
        return Tab5PanelVariant::ILI9881C;
    }

    // A NACK is the normal answer here (only one of the two chips exists),
    // and the IDF driver logs every one of them at error level. Quiet it for
    // the duration of the probe.
    fmrb_log_level_set("i2c.master", FMRB_LOG_NONE);

    Tab5PanelVariant variant = Tab5PanelVariant::ILI9881C;
    bool decided = false;
    bool st_answered = false;
    int last_logged_fw = -1;

    for (int i = 0; i < 60 && !decided; ++i) {
        // ST7123 touch firmware version register (0x0000), 16-bit register
        // address, one byte back.
        const uint8_t fw_reg[2] = { 0x00, 0x00 };
        uint8_t fw = 0;
        if (i2c_master_transmit_receive(st, fw_reg, sizeof(fw_reg), &fw, 1, 100) == ESP_OK) {
            st_answered = true;
            if (fw != last_logged_fw) {  // do not repeat the log while retrying
                last_logged_fw = fw;
                FMRB_LOGI(TAG, "panel probe: ST touch FW version %02x", fw);
            }
            if (fw == 1) {
                variant = Tab5PanelVariant::ST7121;
                decided = true;
            } else if (fw == 3) {
                variant = Tab5PanelVariant::ST7123;
                decided = true;
            }
        } else if (i2c_master_probe(bus, GT911_TOUCH_ADDR, 50) == ESP_OK) {
            // Address ACK only: reading a GT911 here would advance its
            // internal pointer.
            variant = Tab5PanelVariant::ILI9881C;
            decided = true;
        }
        if (!decided) vTaskDelay(pdMS_TO_TICKS(10));
    }

    i2c_master_bus_rm_device(st);
    fmrb_log_level_set("i2c.master", FMRB_LOG_ERROR);

    if (!decided) {
        if (st_answered) {
            // An ST touch block with a firmware version we do not know about.
            // ST7123 is the current production part, so prefer it.
            FMRB_LOGW(TAG, "panel probe: unknown ST touch FW %02x, assuming ST7123",
                      last_logged_fw);
            variant = Tab5PanelVariant::ST7123;
        } else {
            // Neither chip answered. ILI9881C is the configuration this
            // firmware has always shipped with, so fall back to it.
            FMRB_LOGW(TAG, "panel probe: no touch controller answered, assuming ILI9881C");
        }
    }

    FMRB_LOGI(TAG, "panel probe: %s",
              variant == Tab5PanelVariant::ST7123 ? "ST7123 (display+touch)"
            : variant == Tab5PanelVariant::ST7121 ? "ST7121 + ST7123 touch"
                                                  : "ILI9881C + GT911 touch");
    return variant;
}

static Tab5PanelVariant tab5_power_on(void) {
    // TP INT high during reset selects GT911 touch I2C address 0x14 (the
    // ST7123 touch block ignores it and always answers at 0x55).
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
        return Tab5PanelVariant::ILI9881C;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t io1 = NULL, io2 = NULL;
    dev_cfg.device_address = PI4IO1_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io1);
    dev_cfg.device_address = PI4IO2_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io2);

    // PI4IO #1: directions/pulls, resets driven LOW
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
    pi4io_write(io1, 0x05, 0x76);  // release LCD + touch resets

    gpio_set_direction(TAB5_TP_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Identify the panel while this bus is still ours; lgfx takes the port
    // over once the devices below are removed.
    Tab5PanelVariant variant = tab5_probe_panel(bus);

    i2c_master_bus_rm_device(io1);
    i2c_master_bus_rm_device(io2);
    i2c_del_master_bus(bus);
    // Backlight: LEDC PWM via Light_PWM in LGFX_Tab5 (GPIO22, ch7, 44100Hz).
    return variant;
}

bool board_display_configure(void) {
    FMRB_LOGI(TAG, "Tab5 display: power on");
    g_lcd.configure(tab5_power_on());
    return true;
}

void board_display_after_init(void) {
    g_lcd.setRotation(3); // landscape: 1280x720 (native portrait 720x1280 rotated 90deg CCW)
}

void *board_display_panel_framebuffer(void) { return g_lcd.getFrameBuffer(); }

// ============================================================
// I2C primitives: LovyanGFX drives the controller at register level (the
// touch panel), so every transaction goes through lgfx's I2C helpers.
// ============================================================

fmrb_err_t board_display_i2c_write(uint8_t addr, const uint8_t *data,
                                   size_t len, uint32_t freq) {
    auto res = lgfx::i2c::transactionWrite(TAB5_I2C_PORT, addr, data,
                                           (uint8_t)len, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t board_display_i2c_read(uint8_t addr, uint8_t *data,
                                  size_t len, uint32_t freq) {
    auto res = lgfx::i2c::transactionRead(TAB5_I2C_PORT, addr, data,
                                          (uint8_t)len, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t board_display_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                        uint8_t value, uint32_t freq) {
    auto res = lgfx::i2c::writeRegister8(TAB5_I2C_PORT, addr, reg, value,
                                         0, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

// ============================================================
// Headphone jack detect / speaker amp gating (PI4IO #1).
//
// board_display_poll_headphone() is called from the touch task loop; the
// PI4IO transactions take the display's I2C lock like every other bus user.
// ============================================================

static bool g_headphone_in = false;
static uint8_t g_hp_debounce = 0;  // 1 byte: packs with g_headphone_in

// Caller must hold the I2C lock (or be in the single-task init window).
static void hp_apply_speaker(bool headphone_in) {
    // RMW of OUT_SET through the lgfx path; other bits (LCD/TP/CAM
    // resets, EXT5V) keep their state.
    if (headphone_in) {
        lgfx::i2c::bitOff(TAB5_I2C_PORT, PI4IO1_ADDR, PI4IO_REG_OUT_SET,
                          PI4IO_BIT_SPK_EN, PI4IO_I2C_FREQ);
    } else {
        lgfx::i2c::bitOn(TAB5_I2C_PORT, PI4IO1_ADDR, PI4IO_REG_OUT_SET,
                         PI4IO_BIT_SPK_EN, PI4IO_I2C_FREQ);
    }
    FMRB_LOGI(TAG, "Headphone %s: speaker %s",
              headphone_in ? "plugged" : "removed",
              headphone_in ? "muted" : "enabled");
}

void board_display_after_audio(void) {
    // Apply the initial headphone state so booting with headphones
    // plugged does not play the boot sound on the speaker. If lgfx's
    // I2C is not up yet the read fails silently and the touch-task
    // poll corrects the state within ~0.5 s.
    auto hp_res = lgfx::i2c::readRegister8(TAB5_I2C_PORT, PI4IO1_ADDR,
                                           PI4IO_REG_IN_STA,
                                           PI4IO_I2C_FREQ);
    if (!hp_res.has_error() && (hp_res.value() & PI4IO_BIT_HP_DETECT)) {
        g_headphone_in = true;
        hp_apply_speaker(true);
    }
}

void board_display_poll_headphone(void) {
    if (!display_local_i2c_lock()) return;

    auto res = lgfx::i2c::readRegister8(TAB5_I2C_PORT, PI4IO1_ADDR,
                                        PI4IO_REG_IN_STA, PI4IO_I2C_FREQ);
    if (res.has_error()) {
        display_local_i2c_unlock();
        return;
    }
    bool hp = (res.value() & PI4IO_BIT_HP_DETECT) != 0;

    if (hp == g_headphone_in) {
        g_hp_debounce = 0;
    } else if (++g_hp_debounce >= 2) {
        g_hp_debounce = 0;
        g_headphone_in = hp;
        hp_apply_speaker(hp);
    }
    display_local_i2c_unlock();
}
