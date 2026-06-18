// Custom LovyanGFX device for the M5Stack Tab5 / ESP32-P4 MIPI-DSI panel.
//
// We use the LovyanGFX classes directly (NOT M5Unified's board autodetect /
// M5.begin()), configuring the Tab5 DSI bus, panel and GT911 touch explicitly
// from our own pin/timing values. This keeps the build free of the M5 device
// ecosystem and gives a clean base for a future parallel-RGB / forked backend.
//
// Config values mirror M5GFX's Tab5 bring-up (managed_components/m5stack__m5gfx
// /src/M5GFX.cpp): Bus_DSI 2 lanes @ 1040Mbps, LDO ch3 2500mV; Panel_ST7123
// 720x1280 DPI 80MHz; Touch_GT911 on I2C port 1 (SDA31/SCL32/INT23).
//
// NOTE: Phase 1 hardcodes the ST7123 panel (current Tab5). Boards using the
// legacy ILI9881C or the ST7121 need a different Panel class / timings.
#pragma once

#include <M5GFX.h>  // LovyanGFX library headers (we do not use the M5GFX/M5Unified device objects)
// device.hpp (via M5GFX.h) pulls Bus_DSI on esp32p4 but NOT the Tab5 panel/touch
// classes; include them explicitly.
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ST7123.hpp>
#include <lgfx/v1/touch/Touch_GT911.hpp>

class LGFX_Tab5 : public lgfx::LGFX_Device
{
    lgfx::Bus_DSI       _bus;
    lgfx::Panel_ST7123  _panel;
    lgfx::Touch_GT911   _touch;

public:
    LGFX_Tab5()
    {
        {
            auto cfg = _bus.config();
            cfg.bus_id         = 0;
            cfg.lane_num       = 2;
            cfg.lane_mbps      = 1040;   // ST7123 (ILI9881C uses 900)
            cfg.ldo_chan_id    = 3;
            cfg.ldo_voltage_mv = 2500;
            _bus.config(cfg);
        }
        {
            auto cfg = _panel.config();
            cfg.memory_width    = 720;
            cfg.memory_height   = 1280;
            cfg.panel_width     = 720;
            cfg.panel_height    = 1280;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.readable        = true;
            cfg.rgb_order       = true;
            cfg.bus_shared      = false;
            cfg.pin_cs          = -1;
            cfg.pin_rst         = -1;
            _panel.config(cfg);

            auto det = _panel.config_detail();
            det.dpi_freq_mhz      = 80;
            det.hsync_back_porch  = 40;
            det.hsync_pulse_width = 2;
            det.hsync_front_porch = 40;
            det.vsync_back_porch  = 8;
            det.vsync_pulse_width = 2;
            det.vsync_front_porch = 220;
            _panel.config_detail(det);
        }
        _panel.setBus(&_bus);

        {
            auto cfg = _touch.config();
            cfg.pin_rst    = -1;
            cfg.pin_sda    = GPIO_NUM_31;
            cfg.pin_scl    = GPIO_NUM_32;
            cfg.pin_int    = GPIO_NUM_23;
            cfg.freq       = 400000;
            cfg.x_min      = 0;
            cfg.x_max      = 719;
            cfg.y_min      = 0;
            cfg.y_max      = 1279;
            cfg.i2c_port   = 1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};
