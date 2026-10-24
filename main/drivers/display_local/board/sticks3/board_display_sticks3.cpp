// The board display interface (board/board_display.h) for the M5Stack
// StickS3.
//
// configure() sets the M5PM1 power manager up, turns the panel's rail on and
// attaches the ST7789 (lgfx_sticks3.hpp). The panel is 135x240 portrait; the
// firmware uses it landscape (240x135, rotation 1). The system I2C bus is
// driven through lgfx's I2C helpers, so every other transaction on it goes
// through them under the display's I2C lock, as on the AtomS3R.

#include "board_display.h"
#include "lgfx_sticks3.hpp"

#include "fmrb_log.h"

static const char *TAG = "display_local";

static LGFX_StickS3 g_lcd;

lgfx::LGFX_Device *board_display_lcd(void) { return &g_lcd; }

bool board_display_configure(void) {
    FMRB_LOGI(TAG, "StickS3 display: M5PM1 power-up, ST7789 panel");
    return g_lcd.configure();
}

// Landscape: rotation 1 of the 135x240 panel gives 240x135.
void board_display_after_init(void) {
    g_lcd.setRotation(1);
    FMRB_LOGI(TAG, "StickS3 panel ID 0x%06lx, %dx%d",
              (unsigned long)(g_lcd.panelId() & 0xFFFFFF),
              (int)g_lcd.width(), (int)g_lcd.height());
}

void board_display_after_audio(void) {}

// An SPI panel keeps its frame in the controller.
void *board_display_panel_framebuffer(void) { return NULL; }

fmrb_err_t board_display_i2c_write(uint8_t addr, const uint8_t *data,
                                   size_t len, uint32_t freq) {
    auto res = lgfx::i2c::transactionWrite(STICKS3_I2C_PORT, addr, data,
                                           (uint8_t)len, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t board_display_i2c_read(uint8_t addr, uint8_t *data,
                                  size_t len, uint32_t freq) {
    auto res = lgfx::i2c::transactionRead(STICKS3_I2C_PORT, addr, data,
                                          (uint8_t)len, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t board_display_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                        uint8_t value, uint32_t freq) {
    auto res = lgfx::i2c::writeRegister8(STICKS3_I2C_PORT, addr, reg, value,
                                         0, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

// No headphone jack; the codec is not driven yet.
void board_display_poll_headphone(void) {}
