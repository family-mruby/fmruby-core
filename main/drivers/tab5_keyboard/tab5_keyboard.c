// M5Stack Tab5 Keyboard accessory driver (I2C, STM32F030 @ 0x6D).
//
// Phase 0: stub. Real implementation (character-mode read -> host key events)
// lands in Phase 3, modeled on main/drivers/i2c_keyboard/i2c_keyboard.c.

#include "tab5_keyboard.h"
#include "fmrb_log.h"

static const char *TAG = "tab5_kbd";

fmrb_err_t tab5_keyboard_init(void)
{
    FMRB_LOGI(TAG, "tab5_keyboard_init (stub)");
    return FMRB_OK;
}

fmrb_err_t tab5_keyboard_deinit(void)
{
    FMRB_LOGI(TAG, "tab5_keyboard_deinit (stub)");
    return FMRB_OK;
}
