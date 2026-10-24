// The board display interface: everything the local display (display_local)
// needs from the board it runs on, and nothing else.
//
// The drawing part (display_local_task.cpp, the output backends) is the same
// on every board that composites its own screen. What differs per board --
// how the panel is powered up and identified, which LovyanGFX device drives
// it, its orientation, the backlight, who owns the I2C bus the panel's
// helpers sit on, and the odd board chore that has to share that bus -- is
// one implementation of this interface:
//
//   board/tab5/     M5Stack Tab5: MIPI-DSI panel (three variants, probed over
//                   I2C after a PI4IO power sequence), touch, headphone detect
//   board/naryav4/  NARYA v4: LT8912B DSI-to-HDMI bridge, the board's I2C bus
//   board/atoms3r/  M5Stack AtomS3R: 128x128 SPI panel (ST7735S or GC9107),
//                   LP5562 backlight on the system I2C bus
//   board/sticks3/  M5Stack StickS3: 135x240 SPI panel (ST7789P3) used
//                   landscape, PWM backlight, the M5PM1 power manager
//                   switching the panel's rail on the system I2C bus
//   board/none/     no panel at all (the browser build): the canvases and the
//                   compositor run, nothing is put on glass
//
// CMake picks one from the board (fmrb_hw_board_display() in
// components/fmrb_common/fmrb_hw_defines.cmake) and puts its directory on the
// include path, so board_display_conf.h below is that board's.
//
// Compile-time facts about the board live in board_display_conf.h:
//   BOARD_DISPLAY_NO_PANEL   1: there is no panel (board/none)
//   BOARD_DISPLAY_OWNS_I2C   1: the display side owns an I2C bus and lends it
//                            out through display_local_i2c_* (the
//                            board_display_i2c_* primitives below are
//                            defined); 0: those answer "not supported"
//   BOARD_DISPLAY_FB_W/H/BPP/ROTATED  only on a board whose panel exposes a
//                            frame buffer the PPA writes (the P4 boards): its
//                            native size, bytes per pixel (2: RGB565,
//                            3: RGB888), and 1 when the picture goes on
//                            rotated 90 degrees (a portrait panel shown
//                            landscape)
//
// The backlight has no function here: each board attaches its light (PWM,
// LP5562, ...) to its LovyanGFX device in board_display_configure(), so the
// brightness is board_display_lcd()->setBrightness() on every board.
//
// Small screens are not a board property: the display task decides the
// compact boot screen (and the kernel the halt screen) from the panel width.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_display_conf.h"
#include "fmrb_err.h"

#ifndef BOARD_DISPLAY_NO_PANEL
#define BOARD_DISPLAY_NO_PANEL 0
#endif
#ifndef BOARD_DISPLAY_OWNS_I2C
#define BOARD_DISPLAY_OWNS_I2C 0
#endif

#ifdef __cplusplus
#include <M5GFX.h>  // LovyanGFX headers; the M5GFX device object is not used

// The board's LovyanGFX device. A static object of the board's own class;
// valid (as an address) from program start, usable as a panel only after
// board_display_configure() and init().
lgfx::LGFX_Device *board_display_lcd(void);

// Power the panel up, identify it and attach it (with its light and touch)
// to the device. Logs which board it is bringing up. false: the panel did not
// answer and init() must not be called. Not called on a board with no panel.
bool board_display_configure(void);

// After init() succeeded: the orientation the rest of the firmware assumes
// (landscape).
void board_display_after_init(void);

// After the audio codec came up, still before the touch task polls: one-off
// board state that must be read in that race-free window (the Tab5's
// headphone jack).
void board_display_after_audio(void);

// The panel's own frame buffer, for an output backend that writes it
// directly (the PPA), or NULL when the panel keeps its frame in the
// controller (SPI) or there is no panel.
void *board_display_panel_framebuffer(void);

// The I2C primitives behind display_local_i2c_*. Called with the display's
// I2C lock held and the arguments already checked. Defined only when
// BOARD_DISPLAY_OWNS_I2C.
fmrb_err_t board_display_i2c_write(uint8_t addr, const uint8_t *data,
                                   size_t len, uint32_t freq);
fmrb_err_t board_display_i2c_read(uint8_t addr, uint8_t *data,
                                  size_t len, uint32_t freq);
fmrb_err_t board_display_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                        uint8_t value, uint32_t freq);

// Periodic board chore on the shared bus, from the touch task loop (behind
// display_local_poll_headphone). Takes the I2C lock itself, if it needs the
// bus at all.
void board_display_poll_headphone(void);

// The display's I2C lock, for the board code that runs outside the
// primitives above (board_display_poll_headphone). Defined in
// display_local_task.cpp. lock is false before the display is ready.
bool display_local_i2c_lock(void);
void display_local_i2c_unlock(void);
#endif /* __cplusplus */
