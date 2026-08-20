// Custom LovyanGFX device for the M5Stack Tab5 / ESP32-P4 MIPI-DSI panel.
//
// We use the LovyanGFX classes directly (NOT M5Unified's board autodetect /
// M5.begin()), configuring the Tab5 DSI bus, panel and touch explicitly from
// our own pin/timing values. This keeps the build free of the M5 device
// ecosystem and gives a clean base for a future parallel-RGB / forked backend.
//
// Config values mirror M5GFX's Tab5 bring-up (managed_components/m5stack__m5gfx
// /src/M5GFX.cpp):
//   Bus_DSI 2 lanes, LDO ch3 2500mV; panel 720x1280 DPI; touch on I2C port 1
//   (SDA31/SCL32/INT23). Backlight: LEDC PWM ch7 @ 44100Hz on GPIO22.
//
// The Tab5 panel varies by production batch, so the panel/touch classes and
// the DSI lane rate are chosen at run time, from the variant that
// display_p4_task's probe determines (a touch controller probe -- the rear
// label is NOT a reliable indicator: a unit labelled ST7123 reports touch
// firmware version 1 and needs the ST7121 settings below):
//   ILI9881C: ILI9881C display + GT911 touch (0x14), separate chips
//             lane_mbps=900, dpi 80MHz, hsync 140/40/40, vsync 20/4/20.
//   ST7121  : ST7121 display + ST7123 touch (0x55), touch FW version 1
//             lane_mbps=900, dpi 70MHz, hsync 40/2/40, vsync 24/20/200.
//   ST7123  : ST7123 display + touch integrated (0x55), touch FW version 3
//             lane_mbps=1040, dpi 80MHz, hsync 40/2/40, vsync 8/2/220.
// Everything above the panel object is variant-agnostic: both panels derive
// from Panel_DSI and both touch controllers from ITouch, so the drawing path
// (pushImage / getFrameBuffer) and display_p4_get_touch() are unchanged.
#pragma once

#include <M5GFX.h>  // LovyanGFX library headers (we do not use the M5GFX/M5Unified device objects)
// device.hpp (via M5GFX.h) pulls Bus_DSI on esp32p4 but NOT the Tab5 panel/touch
// classes; include them explicitly.
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ILI9881C.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ST7121.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ST7123.hpp>
#include <lgfx/v1/platforms/esp32p4/Touch_ST7123.hpp>
#include <lgfx/v1/touch/Touch_GT911.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>

// Panel/touch combination of this Tab5 unit, as detected by tab5_probe_panel().
enum class Tab5PanelVariant {
    ILI9881C,   // ILI9881C display + GT911 touch (pre 2025-10-14 batches)
    ST7121,     // ST7121 display + ST7123 touch (legacy)
    ST7123,     // ST7123 display + touch, integrated (post 2025-10-14 batches)
};

class LGFX_Tab5 : public lgfx::LGFX_Device
{
    lgfx::Bus_DSI     _bus;
    lgfx::Panel_DSI  *_panel = nullptr;   // owned; allocated by configure()
    lgfx::ITouch     *_touch = nullptr;   // owned; allocated by configure()
    lgfx::Light_PWM   _light;
    Tab5PanelVariant  _variant = Tab5PanelVariant::ILI9881C;

public:
    // Build the bus/panel/touch/backlight objects for this unit's panel
    // variant. Must be called exactly once, before init(); nothing else on
    // this object is valid until it has run.
    void configure(Tab5PanelVariant variant)
    {
        _variant = variant;

        {
            auto cfg = _bus.config();
            cfg.bus_id         = 0;
            cfg.lane_num       = 2;
            // ST7123 clocks the link faster; ILI9881C and the legacy ST7121
            // panel both run at 900.
            cfg.lane_mbps      = (variant == Tab5PanelVariant::ST7123) ? 1040 : 900;
            cfg.ldo_chan_id    = 3;
            cfg.ldo_voltage_mv = 2500;
            _bus.config(cfg);
        }

        lgfx::Panel_DSI::config_detail_t det = {};
        switch (variant) {
        case Tab5PanelVariant::ST7121:
            _panel = new lgfx::Panel_ST7121();
            det = _panel->config_detail();
            det.dpi_freq_mhz      = 70;
            det.hsync_back_porch  = 40;
            det.hsync_pulse_width = 2;
            det.hsync_front_porch = 40;
            det.vsync_back_porch  = 24;
            det.vsync_pulse_width = 20;
            det.vsync_front_porch = 200;
            break;
        case Tab5PanelVariant::ST7123:
            _panel = new lgfx::Panel_ST7123();
            det = _panel->config_detail();
            det.dpi_freq_mhz      = 80;
            det.hsync_back_porch  = 40;
            det.hsync_pulse_width = 2;
            det.hsync_front_porch = 40;
            // note: back + pulse == 10. If it is out of sync, the display
            // position will shift vertically.
            det.vsync_back_porch  = 8;
            det.vsync_pulse_width = 2;
            // note: reducing the front porch will cause the touch panel to
            // stop working.
            det.vsync_front_porch = 220;
            break;
        case Tab5PanelVariant::ILI9881C:
        default:
            _panel = new lgfx::Panel_ILI9881C();
            det = _panel->config_detail();
            det.dpi_freq_mhz      = 80;
            det.hsync_back_porch  = 140;
            det.hsync_pulse_width = 40;
            det.hsync_front_porch = 40;
            det.vsync_back_porch  = 20;
            det.vsync_pulse_width = 4;
            det.vsync_front_porch = 20;
            break;
        }

        {
            auto cfg = _panel->config();
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
            _panel->config(cfg);
            _panel->config_detail(det);
        }
        _panel->setBus(&_bus);

        {
            // GT911 touch: TP INT held HIGH during reset selects I2C address
            // 0x14 (done in tab5_power_on() before LGFX init). The ST7123
            // touch block answers at its fixed address 0x55 instead.
            if (variant == Tab5PanelVariant::ILI9881C) {
                _touch = new lgfx::Touch_GT911();
            } else {
                _touch = new lgfx::Touch_ST7123();
            }
            auto cfg = _touch->config();
            cfg.pin_rst    = -1;
            cfg.pin_sda    = GPIO_NUM_31;
            cfg.pin_scl    = GPIO_NUM_32;
            cfg.pin_int    = GPIO_NUM_23;
            cfg.freq       = 400000;
            // Both controllers report in panel coordinates once configured
            // this way (M5GFX uses the same range for either chip).
            cfg.x_min      = 0;
            cfg.x_max      = 719;
            cfg.y_min      = 0;
            cfg.y_max      = 1279;
            cfg.i2c_port   = 1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            _touch->config(cfg);
            _panel->setTouch(_touch);
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
            _panel->setLight(&_light);
        }

        setPanel(_panel);
    }

    Tab5PanelVariant variant(void) const { return _variant; }

    // Direct pointer to the esp_lcd DPI framebuffer (rgb565 non-swapped,
    // native portrait 720x1280, row stride = panel_width * 2 bytes).
    // Valid after init(). Used by display_p4 to let PPA SRM write the
    // scaled+rotated frame directly, bypassing the per-pixel rotated copy
    // that pushImage performs under setRotation(3).
    void* getFrameBuffer(void) { return _panel ? _panel->config_detail().buffer : nullptr; }
};
