// M5Stack StickS3: compile-time facts for the board display interface
// (board/board_display.h).
#pragma once

// The system I2C bus (G47/G48) carries the M5PM1 power manager, which the
// board code drives through LovyanGFX's I2C helpers before the panel comes
// up, so the display side owns the bus and lends it out (the BMI270 and the
// codec are on it too). The SPI panel keeps its frame in the controller: no
// frame buffer facts.
#define BOARD_DISPLAY_OWNS_I2C 1
