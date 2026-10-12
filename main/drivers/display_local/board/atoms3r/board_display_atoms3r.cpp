// The board display interface (board/board_display.h) for the M5Stack
// AtomS3R.
//
// configure() probes the SPI panel (ST7735S or GC9107, lgfx_atoms3r.hpp). The
// backlight is the LP5562 on the system I2C bus, which LovyanGFX drives at
// register level, so every other transaction on that bus (the BMI270 through
// hw_proxy, for one) goes through lgfx's I2C helpers under the display's I2C
// lock.

#include "board_display.h"
#include "lgfx_atoms3r.hpp"

#include "fmrb_log.h"

static const char *TAG = "display_local";

static LGFX_AtomS3R g_lcd;

lgfx::LGFX_Device *board_display_lcd(void) { return &g_lcd; }

bool board_display_configure(void) {
    FMRB_LOGI(TAG, "AtomS3R display: SPI panel probe");
    return g_lcd.configure();
}

// No rotation (the LovyanGFX default).
void board_display_after_init(void) {}

void board_display_after_audio(void) {}

// An SPI panel keeps its frame in the controller.
void *board_display_panel_framebuffer(void) { return NULL; }

fmrb_err_t board_display_i2c_write(uint8_t addr, const uint8_t *data,
                                   size_t len, uint32_t freq) {
    auto res = lgfx::i2c::transactionWrite(ATOMS3R_I2C_PORT, addr, data,
                                           (uint8_t)len, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t board_display_i2c_read(uint8_t addr, uint8_t *data,
                                  size_t len, uint32_t freq) {
    auto res = lgfx::i2c::transactionRead(ATOMS3R_I2C_PORT, addr, data,
                                          (uint8_t)len, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

fmrb_err_t board_display_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                        uint8_t value, uint32_t freq) {
    auto res = lgfx::i2c::writeRegister8(ATOMS3R_I2C_PORT, addr, reg, value,
                                         0, freq);
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

// No headphone jack, no speaker.
void board_display_poll_headphone(void) {}
