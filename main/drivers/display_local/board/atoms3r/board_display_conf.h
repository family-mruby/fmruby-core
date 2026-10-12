// M5Stack AtomS3R: compile-time facts for the board display interface
// (board/board_display.h).
#pragma once

// The system I2C bus (G45/G0) carries the LP5562 backlight driver, which
// LovyanGFX drives at register level, so the display side owns the bus and
// lends it out. The SPI panel keeps its frame in the controller: no frame
// buffer facts.
#define BOARD_DISPLAY_OWNS_I2C 1
