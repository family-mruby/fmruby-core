// M5Stack Tab5: compile-time facts for the board display interface
// (board/board_display.h).
#pragma once

// The internal I2C bus (GPIO31/32): LovyanGFX drives it at register level for
// the touch panel, so the display side owns it and lends it out.
#define BOARD_DISPLAY_OWNS_I2C 1

// The DPI frame buffer the PPA writes: 720x1280 RGB565, native portrait. The
// landscape picture goes on rotated 90 degrees.
#define BOARD_DISPLAY_FB_W       720
#define BOARD_DISPLAY_FB_H       1280
#define BOARD_DISPLAY_FB_BPP     2
#define BOARD_DISPLAY_FB_ROTATED 1
