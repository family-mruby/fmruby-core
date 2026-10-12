// NARYA v4: compile-time facts for the board display interface
// (board/board_display.h).
#pragma once

// The board's one I2C bus: the display creates it (the HDMI bridge is the
// first thing on it that has to answer) and lends it out.
#define BOARD_DISPLAY_OWNS_I2C 1

// The DPI frame buffer the PPA writes: 1280x720 RGB888 (the LT8912B takes
// nothing else), already landscape.
#define BOARD_DISPLAY_FB_W       1280
#define BOARD_DISPLAY_FB_H       720
#define BOARD_DISPLAY_FB_BPP     3
#define BOARD_DISPLAY_FB_ROTATED 0
