// Custom LovyanGFX device for the NARYA v4 HDMI output:
// ESP32-P4 MIPI-DSI (2 lanes) -> LT8912B bridge -> HDMI, 800x600@60.
//
// Where this differs from lgfx_tab5.hpp, and why:
//
//   * The panel is not a LovyanGFX panel class. M5GFX v0.2.28 does ship a
//     Panel_LT8912B, but its timing table knows only 1280x720@60 and
//     1920x1080@30 -- and 800x600@60 is the one mode that works on this
//     hardware (doc/naryav4/report/p0.md: 40MHz is the only standard pixel
//     clock the P4 can divide exactly out of PLL_F240M, and the LT8912B's DDS
//     will not lock to the jittery APLL every other mode needs). So the bridge
//     is driven by Espressif's esp_lcd_lt8912b, the driver that was proven on
//     this board in P0, with its stock 800x600 timing and init table.
//   * The frame buffer is RGB888 (the LT8912B accepts nothing else),
//     800x600x3 = 1.44MB in PSRAM, allocated by the DPI panel.
//   * No touch, no backlight, no power sequencer: the picture goes to a
//     monitor and the board has no PI4IO expanders.
//
// The DSI bus itself is still lgfx's Bus_DSI -- it does exactly what the
// bring-up needs (DPHY LDO channel, esp_lcd_new_dsi_bus) and keeps the object
// graph the same shape as the Tab5's, so LGFX_Device::init() drives both.
//
// LovyanGFX draws on this panel through Panel_FrameBufferBase, i.e. straight
// into the DSI frame buffer: that is what the boot screen uses. Composited
// frames do not come this way -- PPA SRM writes the frame buffer directly
// (display_backend_ppa.cpp).
#pragma once

#include <M5GFX.h>  // LovyanGFX library headers (we do not use the M5GFX/M5Unified device objects)
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_lt8912b.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

// Display mode. Fixed by P0 measurement; see the file comment before changing
// any of it.
#define NARYAV4_HDMI_W          800
#define NARYAV4_HDMI_H          600
#define NARYAV4_DSI_LANES       2
#define NARYAV4_DSI_LANE_MBPS   1000
#define NARYAV4_DSI_LDO_CHAN    3
#define NARYAV4_DSI_LDO_MV      2500

// The board's single I2C bus: the LT8912B lives here together with the ES8311
// codec and whatever the user hangs off the header. The display owns it (it is
// the first thing that needs it) and lends the handle out.
#define NARYAV4_I2C_PORT        I2C_NUM_0
#define NARYAV4_I2C_FREQ        100000

// A LovyanGFX panel over the LT8912B's DSI frame buffer.
//
// Panel_FrameBufferBase wants an array of row pointers and draws with the CPU;
// everything below init() is the bridge bring-up that produces the buffer
// those rows point into.
class Panel_Naryav4Hdmi : public lgfx::Panel_FrameBufferBase
{
public:
    Panel_Naryav4Hdmi(void) { _cfg.bus_shared = false; }

    // The I2C bus the bridge is reached on. Must be set before init().
    void setI2cBus(i2c_master_bus_handle_t bus) { _i2c_bus = bus; }

    void *getFrameBuffer(void) const { return _fb; }
    esp_lcd_panel_handle_t getPanelHandle(void) const { return _panel; }

    bool init(bool use_reset) override
    {
        if (_lines_buffer) return false;

        _cfg.memory_width  = NARYAV4_HDMI_W;
        _cfg.memory_height = NARYAV4_HDMI_H;
        _cfg.panel_width   = NARYAV4_HDMI_W;
        _cfg.panel_height  = NARYAV4_HDMI_H;
        setColorDepth(lgfx::rgb888_nonswapped);

        // Brings up Bus_DSI (DPHY LDO + esp_lcd DSI bus) as a side effect.
        if (!lgfx::Panel_FrameBufferBase::init(use_reset)) return false;

        auto bus = getBus();
        if (!bus || bus->busType() != lgfx::bus_type_t::bus_dsi) {
            FMRB_LOGE("naryav4_hdmi", "no DSI bus attached");
            return false;
        }
        auto dsi = static_cast<lgfx::Bus_DSI *>(bus)->getMipiDsiBus();
        if (!dsi || !bring_up(dsi)) {
            release();
            return false;
        }
        return build_line_table();
    }

    // Nothing refreshes on demand: the DPI panel scans the frame buffer
    // continuously, so a finished draw only has to reach PSRAM. The base class
    // does that write-back (it sets _auto_display for exactly this reason).
    void waitDisplay(void) override {}
    bool displayBusy(void) override { return false; }

private:
    static esp_lcd_panel_io_i2c_config_t io_cfg(uint8_t address)
    {
        // Field by field rather than through the vendor's LT8912B_IO_CFG
        // macro: that macro's designated initialisers are not in declaration
        // order, which C accepts and C++ does not.
        esp_lcd_panel_io_i2c_config_t cfg = {};
        cfg.dev_addr             = address;
        cfg.control_phase_bytes  = 1;
        cfg.lcd_cmd_bits         = 8;
        cfg.lcd_param_bits       = 8;
        cfg.flags.disable_control_phase = 1;
        cfg.scl_speed_hz         = NARYAV4_I2C_FREQ;
        return cfg;
    }

    bool bring_up(esp_lcd_dsi_bus_handle_t dsi)
    {
        static const char *TAG = "naryav4_hdmi";

        if (!_i2c_bus) {
            FMRB_LOGE(TAG, "no I2C bus for the LT8912B");
            return false;
        }

        // Three register pages, three I2C addresses.
        auto main_cfg = io_cfg(LT8912B_IO_I2C_MAIN_ADDRESS);
        auto cec_cfg  = io_cfg(LT8912B_IO_I2C_CEC_ADDRESS);
        auto avi_cfg  = io_cfg(LT8912B_IO_I2C_AVI_ADDRESS);
        if (esp_lcd_new_panel_io_i2c(_i2c_bus, &main_cfg, &_io.main) != ESP_OK
         || esp_lcd_new_panel_io_i2c(_i2c_bus, &cec_cfg,  &_io.cec_dsi) != ESP_OK
         || esp_lcd_new_panel_io_i2c(_i2c_bus, &avi_cfg,  &_io.avi) != ESP_OK) {
            FMRB_LOGE(TAG, "LT8912B panel IO failed (bridge not answering on I2C?)");
            return false;
        }

        // 800x600@60, VESA timing. Same numbers as the driver's own
        // LT8912B_800x600_PANEL_60HZ_DPI_CONFIG / VIDEO_TIMING_800x600_60Hz
        // macros, spelled out here for the same C++ reason as above.
        //
        // dpi_clk_src stays at DEFAULT (PLL_F240M): 240/6 = exactly 40MHz.
        // Do not move this to APLL -- it produces the same nominal frequency
        // with fractional-divider jitter, and the LT8912B's DDS never locks to
        // it (measured both ways, doc/naryav4/report/p0.md).
        _dpi = {};
        _dpi.virtual_channel    = 0;
        _dpi.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        _dpi.dpi_clock_freq_mhz = 40;
        _dpi.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB888;
        _dpi.num_fbs            = 1;
        _dpi.video_timing.h_size            = NARYAV4_HDMI_W;
        _dpi.video_timing.v_size            = NARYAV4_HDMI_H;
        _dpi.video_timing.hsync_pulse_width = 128;
        _dpi.video_timing.hsync_back_porch  = 88;
        _dpi.video_timing.hsync_front_porch = 48;
        _dpi.video_timing.vsync_pulse_width = 4;
        _dpi.video_timing.vsync_back_porch  = 23;
        _dpi.video_timing.vsync_front_porch = 1;
        _dpi.flags.disable_lp   = true;

        lt8912b_vendor_config_t vendor = {};
        vendor.video_timing = ESP_LCD_LT8912B_VIDEO_TIMING_800x600_60Hz();
        vendor.mipi_config.dsi_bus    = dsi;
        vendor.mipi_config.dpi_config = &_dpi;
        vendor.mipi_config.lane_num   = NARYAV4_DSI_LANES;

        esp_lcd_panel_dev_config_t dev = {};
        dev.reset_gpio_num = -1;          // no reset line to the bridge
        dev.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
        dev.bits_per_pixel = 24;
        dev.vendor_config  = &vendor;

        esp_lcd_panel_lt8912b_io_t io_all = { _io.main, _io.cec_dsi, _io.avi };
        if (esp_lcd_new_panel_lt8912b(&io_all, &dev, &_panel) != ESP_OK) {
            FMRB_LOGE(TAG, "LT8912B panel create failed");
            return false;
        }
        // With no reset line this is the bridge's software reset.
        if (esp_lcd_panel_reset(_panel) != ESP_OK
         || esp_lcd_panel_init(_panel) != ESP_OK) {
            FMRB_LOGE(TAG, "LT8912B panel init failed");
            return false;
        }

        if (esp_lcd_dpi_panel_get_frame_buffer(_panel, 1, &_fb) != ESP_OK || !_fb) {
            FMRB_LOGE(TAG, "DPI frame buffer unavailable");
            return false;
        }
        FMRB_LOGI(TAG, "HDMI %dx%d@60 up, RGB888 fb @%p (DDS needs ~20s to lock)",
                  NARYAV4_HDMI_W, NARYAV4_HDMI_H, _fb);
        return true;
    }

    bool build_line_table(void)
    {
        const size_t stride = (size_t)NARYAV4_HDMI_W * (_write_bits >> 3);
        auto rows = (uint8_t **)heap_caps_malloc(NARYAV4_HDMI_H * sizeof(uint8_t *),
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!rows) return false;
        auto p = (uint8_t *)_fb;
        for (int y = 0; y < NARYAV4_HDMI_H; ++y) {
            rows[y] = p;
            p += stride;
        }
        _lines_buffer = rows;
        return true;
    }

    void release(void)
    {
        if (_panel)       { esp_lcd_panel_del(_panel);      _panel = nullptr; }
        if (_io.avi)      { esp_lcd_panel_io_del(_io.avi);      _io.avi = nullptr; }
        if (_io.cec_dsi)  { esp_lcd_panel_io_del(_io.cec_dsi);  _io.cec_dsi = nullptr; }
        if (_io.main)     { esp_lcd_panel_io_del(_io.main);     _io.main = nullptr; }
        _fb = nullptr;
    }

    i2c_master_bus_handle_t   _i2c_bus = nullptr;
    esp_lcd_panel_lt8912b_io_t _io     = {};
    esp_lcd_panel_handle_t    _panel   = nullptr;
    esp_lcd_dpi_panel_config_t _dpi    = {};   // referenced by the driver during init
    void                     *_fb      = nullptr;
};

class LGFX_Naryav4 : public lgfx::LGFX_Device
{
    lgfx::Bus_DSI      _bus;
    Panel_Naryav4Hdmi  _panel;
    i2c_master_bus_handle_t _i2c = nullptr;

public:
    // Build the bus/panel objects and the I2C bus they need. Must be called
    // exactly once, before init(); nothing else on this object is valid until
    // it has run. Unlike the Tab5's configure() this touches no power rails --
    // the board has none to sequence.
    bool configure(void)
    {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port   = NARYAV4_I2C_PORT;
        bus_cfg.sda_io_num = FMRB_PIN_I2C1_SDA;
        bus_cfg.scl_io_num = FMRB_PIN_I2C1_SCL;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        // The board fits 2.2k pull-ups; the internal ones would only fight
        // them, and they are far too weak for this bus anyway.
        bus_cfg.flags.enable_internal_pullup = false;
        if (i2c_new_master_bus(&bus_cfg, &_i2c) != ESP_OK) {
            FMRB_LOGE("naryav4_hdmi", "I2C bus init failed (SDA=%d SCL=%d)",
                      (int)FMRB_PIN_I2C1_SDA, (int)FMRB_PIN_I2C1_SCL);
            return false;
        }

        {
            auto cfg = _bus.config();
            cfg.bus_id         = 0;
            cfg.lane_num       = NARYAV4_DSI_LANES;
            cfg.lane_mbps      = NARYAV4_DSI_LANE_MBPS;
            cfg.ldo_chan_id    = NARYAV4_DSI_LDO_CHAN;
            cfg.ldo_voltage_mv = NARYAV4_DSI_LDO_MV;
            _bus.config(cfg);
        }

        {
            auto cfg = _panel.config();
            cfg.memory_width    = NARYAV4_HDMI_W;
            cfg.memory_height   = NARYAV4_HDMI_H;
            cfg.panel_width     = NARYAV4_HDMI_W;
            cfg.panel_height    = NARYAV4_HDMI_H;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = true;
            cfg.rgb_order       = true;
            cfg.bus_shared      = false;
            cfg.pin_cs          = -1;
            cfg.pin_rst         = -1;
            _panel.config(cfg);
        }
        _panel.setBus(&_bus);
        _panel.setI2cBus(_i2c);
        setPanel(&_panel);
        return true;
    }

    // Direct pointer to the esp_lcd DPI frame buffer (RGB888, 800x600, row
    // stride = 800*3 bytes). Valid after init(). This is what PPA SRM writes.
    void *getFrameBuffer(void) { return _panel.getFrameBuffer(); }

    // The board I2C bus, for everything else that lives on it (the ES8311
    // codec, mruby app transactions). Valid after configure().
    i2c_master_bus_handle_t i2cBus(void) const { return _i2c; }
};
