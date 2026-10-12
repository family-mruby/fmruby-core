// The board display interface (board/board_display.h) with no panel: the
// browser build, which reads the composited frame itself
// (wasm/backend/display_backend_wasm.cpp).

#include "board_display.h"

// Sprite parent only: no panel behind it, and nothing may draw on it.
static lgfx::LGFX_Device g_lcd;

lgfx::LGFX_Device *board_display_lcd(void) { return &g_lcd; }

void *board_display_panel_framebuffer(void) { return NULL; }

void board_display_poll_headphone(void) {}
