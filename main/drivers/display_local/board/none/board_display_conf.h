// No panel (the browser build): compile-time facts for the board display
// interface (board/board_display.h).
#pragma once

// The canvases and the compositor run; nothing is put on glass, there is no
// boot screen and no touch, and no I2C bus is held here.
#define BOARD_DISPLAY_NO_PANEL 1
#define BOARD_DISPLAY_OWNS_I2C 0
