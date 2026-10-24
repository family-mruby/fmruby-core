// LovyanGFX device for the M5Stack StickS3's 1.14" 135x240 IPS panel
// (ST7789P3), and the board's power manager chores that come before it.
//
// As with LGFX_AtomS3R, the LovyanGFX classes are configured here directly
// from fmrb_pin_assign.h rather than through M5GFX's board autodetect.
//
// Power. The StickS3 has a power manager, the M5PM1 (a PY32 MCU at 0x6E on
// the system I2C bus, SDA=G47 / SCL=G48, I2C_NUM_1). It is always powered and
// switches the rails itself: a click of the power button turns the S3's 3.3 V
// rail on, a double click turns it off, and it keeps the rail on from the
// battery when USB goes away. So there is no hold pin to drive. What the
// firmware has to do is what M5GFX / M5Unified do for this board:
//   - turn off the M5PM1's I2C idle sleep and its watchdog (both survive a
//     reset of the S3 on battery, so a value left by another firmware stays),
//   - drive the M5PM1's G2 high: it enables the rail of the LCD, the
//     backlight's FET and the audio codec (L3B in the schematic).
// The power state (source, rails, battery voltage) is logged once, so a boot
// log tells whether the board runs from USB or the battery.
//
// Panel: ST7789 in 3-wire SPI (no MISO), 135x240 in the controller's 240x320
// memory at offset (52, 40), inverted. Values from M5GFX's StickS3 entry.
// The backlight is a PWM on G38 (through a FET, active high).
//
// Every other user of the system bus (the BMI270 through hw_proxy, for one)
// goes through the display task's serialized I2C service, as on the AtomS3R.
#pragma once

#include <M5GFX.h>  // LovyanGFX library headers (the M5GFX device object is not used)
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

// The system I2C bus the power manager sits on.
#define STICKS3_I2C_PORT   I2C_NUM_1
#define STICKS3_PM1        0x6E
#define STICKS3_PM1_FREQ   100000

// M5PM1 registers (M5Unified's M5PM1_Class).
#define PM1_REG_DEVICE_ID  0x00
#define PM1_REG_PWR_SRC    0x04   // bits 2:0, which supply is present
#define PM1_REG_PWR_CFG    0x06   // b0 charge, b1 DCDC 3.3 V, b2 LDO 3.3 V, b3 5 V boost
#define PM1_REG_I2C_CFG    0x09
#define PM1_REG_WDT_CNT    0x0A
#define PM1_REG_GPIO_MODE  0x10   // 1 = output
#define PM1_REG_GPIO_OUT   0x11
#define PM1_REG_GPIO_IN    0x12
#define PM1_REG_GPIO_DRV   0x13   // 1 = open drain
#define PM1_REG_GPIO_FUNC0 0x16   // 2 bits per pin (G0-G3), 00 = GPIO
#define PM1_REG_VBAT_L     0x22   // mV, little endian
#define PM1_REG_VIN_L      0x24   // mV, little endian
#define PM1_LCD_RAIL_GPIO  2      // PM1 G2: L3B enable (LCD, backlight, codec)

class LGFX_StickS3 : public lgfx::LGFX_Device
{
public:
    // Bring the power manager into a known state, power the panel and attach
    // it. false when the power manager did not answer: the panel's rail is
    // then unknown and init() must not be called.
    bool configure(void)
    {
        static const char *TAG = "lgfx_sticks3";

        lgfx::i2c::init(STICKS3_I2C_PORT, FMRB_PIN_I2C1_SDA, FMRB_PIN_I2C1_SCL);
        if (!power_up(TAG)) return false;

        reset_panel();

        auto cfg = _bus.config();
        cfg.spi_host   = SPI3_HOST;
        cfg.spi_mode   = 0;
        cfg.spi_3wire  = true;
        cfg.pin_sclk   = FMRB_PIN_LCD_SPI_SCLK;
        cfg.pin_mosi   = FMRB_PIN_LCD_SPI_MOSI;
        cfg.pin_miso   = -1;
        cfg.pin_dc     = FMRB_PIN_LCD_SPI_DC;
        cfg.freq_write = 40000000;
        cfg.freq_read  = 16000000;
        _bus.config(cfg);
        _st7789.bus(&_bus);

        auto pc = _st7789.config();
        pc.pin_cs          = FMRB_PIN_LCD_SPI_CS;
        pc.pin_rst         = FMRB_PIN_LCD_SPI_RST;
        pc.panel_width     = 135;
        pc.panel_height    = 240;
        pc.offset_x        = 52;
        pc.offset_y        = 40;
        pc.offset_rotation = 0;
        pc.readable        = true;
        pc.invert          = true;
        pc.bus_shared      = false;
        _st7789.config(pc);

        // M5GFX's backlight values for this board: LEDC channel 7, 256 Hz,
        // the lowest 16 steps cut off (the panel is dark below them).
        auto lc = _light.config();
        lc.pin_bl      = FMRB_PIN_LCD_BACKLIGHT;
        lc.freq        = 256;
        lc.pwm_channel = 7;
        lc.offset      = 16;
        lc.invert      = false;
        _light.config(lc);
        _st7789.setLight(&_light);

        setPanel(&_st7789);
        FMRB_LOGI(TAG, "ST7789 135x240 attached");
        return true;
    }

    // After init(): the panel's ID (RDDID), for the log. 0 or all ones means
    // nothing answered on the bus.
    uint32_t panelId(void) { return _st7789.readCommand(0x04, 1, 3); }

private:
    lgfx::Bus_SPI      _bus;
    lgfx::Panel_ST7789 _st7789;
    lgfx::Light_PWM    _light;

    static bool pm1_read(uint8_t reg, uint8_t *data, size_t len)
    {
        auto r = lgfx::i2c::readRegister(STICKS3_I2C_PORT, STICKS3_PM1, reg,
                                         data, len, STICKS3_PM1_FREQ);
        return !r.has_error();
    }

    static bool pm1_write(uint8_t reg, uint8_t value, uint8_t mask = 0)
    {
        auto r = lgfx::i2c::writeRegister8(STICKS3_I2C_PORT, STICKS3_PM1, reg,
                                           value, mask, STICKS3_PM1_FREQ);
        return !r.has_error();
    }

    static bool power_up(const char *TAG)
    {
        uint8_t id = 0;
        if (!pm1_read(PM1_REG_DEVICE_ID, &id, 1)) {
            FMRB_LOGE(TAG, "M5PM1 (0x%02x) did not answer: no panel", STICKS3_PM1);
            return false;
        }

        // Before any other access: a sleeping PM1 interface drops transfers,
        // and its watchdog would cut the power of a firmware that does not
        // feed it.
        bool ok = pm1_write(PM1_REG_I2C_CFG, 0x00);
        ok &= pm1_write(PM1_REG_WDT_CNT, 0x00);

        uint8_t func = 0, mode = 0, out = 0;
        pm1_read(PM1_REG_GPIO_FUNC0, &func, 1);
        pm1_read(PM1_REG_GPIO_MODE, &mode, 1);
        pm1_read(PM1_REG_GPIO_OUT, &out, 1);
        const uint8_t bit = 1u << PM1_LCD_RAIL_GPIO;
        const bool was_on = (func & (0x03u << (PM1_LCD_RAIL_GPIO * 2))) == 0
                         && (mode & bit) && (out & bit);

        // G2: GPIO function, push-pull output, high.
        ok &= pm1_write(PM1_REG_GPIO_FUNC0, 0x00,
                        (uint8_t)~(0x03u << (PM1_LCD_RAIL_GPIO * 2)));
        ok &= pm1_write(PM1_REG_GPIO_DRV, 0x00, (uint8_t)~bit);
        ok &= pm1_write(PM1_REG_GPIO_MODE, bit, (uint8_t)~bit);
        ok &= pm1_write(PM1_REG_GPIO_OUT, bit, (uint8_t)~bit);
        // The rail and the panel's own power-on reset need time before the
        // panel takes commands (M5GFX waits 100 ms too).
        if (!was_on) lgfx::delay(100);

        uint8_t src = 0, pwr = 0, gin = 0, vb[2] = {0, 0}, vi[2] = {0, 0};
        pm1_read(PM1_REG_PWR_SRC, &src, 1);
        pm1_read(PM1_REG_PWR_CFG, &pwr, 1);
        pm1_read(PM1_REG_GPIO_IN, &gin, 1);
        pm1_read(PM1_REG_VBAT_L, vb, 2);
        pm1_read(PM1_REG_VIN_L, vi, 2);
        FMRB_LOGI(TAG, "M5PM1 id=0x%02x: LCD rail %s (G2, func 0x%02x mode 0x%02x out 0x%02x)%s",
                  id, was_on ? "was on" : "turned on", func, mode, out,
                  ok ? "" : ", a write FAILED");
        FMRB_LOGI(TAG, "M5PM1 power: src=0x%02x cfg=0x%02x (dcdc %d ldo %d boost %d chg %d) "
                  "vbat=%u mV vin=%u mV charging=%d",
                  src, pwr, (pwr >> 1) & 1, (pwr >> 2) & 1, (pwr >> 3) & 1, pwr & 1,
                  (unsigned)(vb[0] | (vb[1] << 8)), (unsigned)(vi[0] | (vi[1] << 8)),
                  (gin & 0x01) ? 0 : 1);
        return true;
    }

    static void reset_panel(void)
    {
        lgfx::pinMode(FMRB_PIN_LCD_SPI_RST, lgfx::pin_mode_t::output);
        lgfx::gpio_lo(FMRB_PIN_LCD_SPI_RST);
        lgfx::delay(2);
        lgfx::gpio_hi(FMRB_PIN_LCD_SPI_RST);
        lgfx::delay(10);
    }
};
