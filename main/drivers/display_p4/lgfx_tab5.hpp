// Custom LovyanGFX device for the M5Stack Tab5 / ESP32-P4 MIPI-DSI panel.
//
// We use the LovyanGFX classes directly (NOT M5Unified's board autodetect /
// M5.begin()), configuring the Tab5 DSI bus, panel and GT911 touch explicitly
// from our own pin/timing values. This keeps the build free of the M5 device
// ecosystem and gives a clean base for a future parallel-RGB / forked backend.
//
// Config values mirror M5GFX's Tab5 bring-up (managed_components/m5stack__m5gfx
// /src/M5GFX.cpp) for the ILI9881C panel variant:
//   Bus_DSI 2 lanes @ 900Mbps, LDO ch3 2500mV; Panel_ILI9881C 720x1280 DPI
//   80MHz; Touch_GT911 on I2C port 1 (SDA31/SCL32/INT23 addr 0x14).
//   Backlight: LEDC PWM ch7 @ 44100Hz on GPIO22.
//
// NOTE: Tab5 panel variant depends on production batch (check rear label):
//   Pre 2025-10-14 : ILI9881C (display) + GT911 (touch, separate chips)
//                    -> This file. lane_mbps=900, Panel_ILI9881C, Touch_GT911.
//   Post 2025-10-14: ST7123 (display+touch integrated)
//                    -> Needs Panel_ST7123, Touch_ST7123, lane_mbps=1040,
//                       dpi=80MHz, hsync 40/2/40, vsync 8/2/220.
//   Legacy ST7121   : Panel_ST7121, Touch_ST7123, lane_mbps=900, dpi=70MHz.
// M5GFX detects the variant at runtime via DSI ID reads; this file hardcodes
// ILI9881C. Auto-detection is a future task (needed for NARYAv4 production).
#pragma once

#include <M5GFX.h>  // LovyanGFX library headers (we do not use the M5GFX/M5Unified device objects)
// device.hpp (via M5GFX.h) pulls Bus_DSI on esp32p4 but NOT the Tab5 panel/touch
// classes; include them explicitly.
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ILI9881C.hpp>
#include <lgfx/v1/touch/Touch_GT911.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

class LGFX_Tab5 : public lgfx::LGFX_Device
{
    lgfx::Bus_DSI        _bus;
    lgfx::Panel_ILI9881C _panel;
    lgfx::Touch_GT911    _touch;
    lgfx::Light_PWM      _light;

public:
    LGFX_Tab5()
    {
        {
            auto cfg = _bus.config();
            cfg.bus_id         = 0;
            cfg.lane_num       = 2;
            cfg.lane_mbps      = 900;    // ILI9881C (ST7123 uses 1040)
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
            det.hsync_back_porch  = 140;
            det.hsync_pulse_width = 40;
            det.hsync_front_porch = 40;
            det.vsync_back_porch  = 20;
            det.vsync_pulse_width = 4;
            det.vsync_front_porch = 20;
            _panel.config_detail(det);
        }
        _panel.setBus(&_bus);

        {
            // GT911 touch: TP INT held HIGH during reset selects I2C address 0x14.
            // This is done in tab5_power_on() before LGFX init.
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

        {
            // Backlight: LEDC PWM ch7 @ 44100Hz on GPIO22, matching M5GFX Tab5 init.
            // Simple gpio_set_level(HIGH) does not drive the Tab5 backlight IC.
            auto cfg = _light.config();
            cfg.pin_bl      = GPIO_NUM_22;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            cfg.invert      = false;
            cfg.offset      = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }

    // Direct pointer to the esp_lcd DPI framebuffer (rgb565 non-swapped,
    // native portrait 720x1280, row stride = panel_width * 2 bytes).
    // Valid after init(). Used by display_p4 to let PPA SRM write the
    // scaled+rotated frame directly, bypassing the per-pixel rotated copy
    // that pushImage performs under setRotation(3).
    void* getFrameBuffer(void) { return _panel.config_detail().buffer; }
};
