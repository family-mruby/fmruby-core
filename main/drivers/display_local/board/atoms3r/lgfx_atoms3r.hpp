// LovyanGFX device for the M5Stack AtomS3R's 0.85" 128x128 IPS panel.
//
// As with LGFX_Tab5 / LGFX_Naryav4, the LovyanGFX classes are configured here
// directly from fmrb_pin_assign.h rather than through M5GFX's board
// autodetect, so the display task owns exactly what it uses.
//
// The panel controller depends on the production lot: the product documents
// say ST7735S, the unit checked in doc/headless_s3/report/h0.md is a GC9107.
// configure() reads the panel ID over the 3-wire SPI bus and picks the class
// and offsets the same way M5GFX's AtomS3R detection does:
//   ST7735S: ID 0x7683 / 0x897C (low 16 bits); 128x128 in 132 rows,
//            offset (2, 1), offset_rotation 2, inverted
//   GC9107 : ID 0x079100 (low 24 bits); 128x128, offset_y 32
// Some GC9107 lots only answer the ID read at a slow clock, so a failed read
// is retried at 100 kHz before giving up.
//
// The backlight is not a GPIO: it is the W channel of an LP5562 LED driver on
// the system I2C bus (SDA=G45, SCL=G0, I2C_NUM_1), which also carries the
// BMI270. LovyanGFX drives that controller at register level, so -- as on the
// Tab5 -- every other user of the bus goes through the display task's
// serialized I2C service (display_local_i2c_*, and hw_proxy_i2c.c for apps).
#pragma once

#include <M5GFX.h>  // LovyanGFX library headers (the M5GFX device object is not used)
#include <lgfx/v1/panel/Panel_ST7735.hpp>
#include <lgfx/v1/panel/Panel_GC9A01.hpp>

#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

// The system I2C bus the backlight driver sits on.
#define ATOMS3R_I2C_PORT  I2C_NUM_1
#define ATOMS3R_I2C_FREQ  400000
#define ATOMS3R_LP5562    0x30

// LP5562 W channel as an lgfx backlight. Register writes follow M5GFX's
// Light_M5StackAtomS3R: enable the chip, run it on its internal clock, map
// all four outputs to I2C control, then set the W PWM.
class Light_AtomS3R_LP5562 : public lgfx::ILight
{
public:
    bool init(uint8_t brightness) override
    {
        lgfx::i2c::init(ATOMS3R_I2C_PORT, FMRB_PIN_I2C1_SDA, FMRB_PIN_I2C1_SCL);
        lgfx::i2c::writeRegister8(ATOMS3R_I2C_PORT, ATOMS3R_LP5562, 0x00, 0x40, 0, ATOMS3R_I2C_FREQ);
        lgfx::delay(1);
        lgfx::i2c::writeRegister8(ATOMS3R_I2C_PORT, ATOMS3R_LP5562, 0x08, 0x01, 0, ATOMS3R_I2C_FREQ);
        lgfx::i2c::writeRegister8(ATOMS3R_I2C_PORT, ATOMS3R_LP5562, 0x70, 0x00, 0, ATOMS3R_I2C_FREQ);
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override
    {
        lgfx::i2c::writeRegister8(ATOMS3R_I2C_PORT, ATOMS3R_LP5562, 0x0E, brightness, 0, ATOMS3R_I2C_FREQ);
    }
};

class LGFX_AtomS3R : public lgfx::LGFX_Device
{
public:
    enum class Panel { None, ST7735S, GC9107 };

    // Bring the SPI bus up, identify the panel and attach it. false when
    // neither controller answered; init() must not be called then.
    bool configure(void)
    {
        static const char *TAG = "lgfx_atoms3r";

        auto cfg = _bus.config();
        cfg.spi_host   = SPI3_HOST;
        cfg.spi_mode   = 0;
        cfg.spi_3wire  = true;
        cfg.pin_sclk   = FMRB_PIN_LCD_SPI_SCLK;
        cfg.pin_mosi   = FMRB_PIN_LCD_SPI_MOSI;
        cfg.pin_miso   = -1;
        cfg.pin_dc     = FMRB_PIN_LCD_SPI_DC;
        cfg.freq_write = 8000000;
        cfg.freq_read  = 8000000;
        _bus.config(cfg);

        reset_panel();
        uint32_t id = probe(cfg);
        if (detect(id) == Panel::None) {
            cfg.freq_write = 100000;
            cfg.freq_read  = 100000;
            id = probe(cfg);
        }
        _panel_kind = detect(id);
        FMRB_LOGI(TAG, "panel ID 0x%08lx: %s", (unsigned long)id,
                  _panel_kind == Panel::ST7735S ? "ST7735S"
                : _panel_kind == Panel::GC9107  ? "GC9107" : "unknown");
        if (_panel_kind == Panel::None) return false;

        // Working speed (M5GFX's values for this board). The probe's bus is
        // released: the panel's init() brings it up again with these.
        _bus.release();
        cfg.freq_write = 40000000;
        cfg.freq_read  = 16000000;
        _bus.config(cfg);

        lgfx::Panel_Device *panel;
        if (_panel_kind == Panel::ST7735S) {
            _st7735.bus(&_bus);
            auto pc = _st7735.config();
            pc.pin_cs          = FMRB_PIN_LCD_SPI_CS;
            pc.pin_rst         = FMRB_PIN_LCD_SPI_RST;
            pc.panel_width     = 128;
            pc.panel_height    = 128;
            pc.memory_height   = 132;
            pc.offset_x        = 2;
            pc.offset_y        = 1;
            pc.offset_rotation = 2;
            pc.readable        = true;
            pc.bus_shared      = false;
            pc.invert          = true;
            _st7735.config(pc);
            panel = &_st7735;
        } else {
            _gc9107.bus(&_bus);
            auto pc = _gc9107.config();
            pc.pin_cs       = FMRB_PIN_LCD_SPI_CS;
            pc.pin_rst      = FMRB_PIN_LCD_SPI_RST;
            pc.panel_width  = 128;
            pc.panel_height = 128;
            pc.offset_y     = 32;
            pc.readable     = false;
            pc.bus_shared   = false;
            _gc9107.config(pc);
            panel = &_gc9107;
        }
        panel->setLight(&_light);
        setPanel(panel);
        return true;
    }

    Panel panelKind(void) const { return _panel_kind; }

private:
    lgfx::Bus_SPI        _bus;
    lgfx::Panel_ST7735S  _st7735;
    lgfx::Panel_GC9107   _gc9107;
    Light_AtomS3R_LP5562 _light;
    Panel                _panel_kind = Panel::None;

    static Panel detect(uint32_t id)
    {
        if ((id & 0xFFFF) == 0x7683 || (id & 0xFFFF) == 0x897C) return Panel::ST7735S;
        if ((id & 0xFFFFFF) == 0x079100) return Panel::GC9107;
        return Panel::None;
    }

    static void reset_panel(void)
    {
        lgfx::pinMode(FMRB_PIN_LCD_SPI_RST, lgfx::pin_mode_t::output);
        lgfx::gpio_lo(FMRB_PIN_LCD_SPI_RST);
        lgfx::delay(2);
        lgfx::gpio_hi(FMRB_PIN_LCD_SPI_RST);
        lgfx::delay(10);
    }

    // RDDID (0x04) with one dummy bit, CS driven by hand (the panel object
    // that would own CS does not exist yet). Mirrors M5GFX's _read_panel_id.
    uint32_t probe(const lgfx::Bus_SPI::config_t &cfg)
    {
        _bus.release();
        _bus.config(cfg);
        _bus.init();
        const int cs = FMRB_PIN_LCD_SPI_CS;
        lgfx::pinMode(cs, lgfx::pin_mode_t::output);
        _bus.beginTransaction();
        lgfx::gpio_hi(cs);
        _bus.writeCommand(0, 8);
        _bus.wait();
        lgfx::gpio_lo(cs);
        _bus.writeCommand(0x04, 8);
        _bus.beginRead(1);
        uint32_t id = _bus.readData(32);
        _bus.endTransaction();
        lgfx::gpio_hi(cs);
        return id;
    }
};
