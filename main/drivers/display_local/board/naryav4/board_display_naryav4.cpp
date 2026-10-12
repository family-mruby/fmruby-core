// The board display interface (board/board_display.h) for NARYA v4.
//
// The panel is an LT8912B DSI-to-HDMI bridge (lgfx_naryav4.hpp). Its
// configure() creates the board's one I2C bus, because the bridge is the
// first thing on it that has to answer; everything else on the bus (the
// codec, mruby app transactions) borrows the handle through the I2C
// primitives below rather than making a second bus on the same pins.

#include "board_display.h"
#include "lgfx_naryav4.hpp"

#include "fmrb_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "display_local";

static LGFX_Naryav4 g_lcd;
static i2c_master_bus_handle_t g_naryav4_i2c = nullptr;

lgfx::LGFX_Device *board_display_lcd(void) { return &g_lcd; }

bool board_display_configure(void) {
    FMRB_LOGI(TAG, "NARYAv4 display: I2C + DSI bus");
    bool configured = g_lcd.configure();
    g_naryav4_i2c = g_lcd.i2cBus();
    return configured;
}

// NARYA v4 is landscape already (1280x720 over HDMI); no rotation.
void board_display_after_init(void) {}

void board_display_after_audio(void) {}

void *board_display_panel_framebuffer(void) { return g_lcd.getFrameBuffer(); }

// ============================================================
// I2C primitives. NARYA v4 has no touch controller, so LovyanGFX never drives
// this bus and the mixing hazard the Tab5 works around does not exist here:
// every transaction goes through the normal i2c_master driver on the bus the
// display created. The display's lock stays, because the callers are still
// several tasks.
//
// A device handle per transaction rather than a cache: the addresses are not
// known in advance (an mruby app can talk to anything on the header), and at
// this traffic level -- volume changes, occasional app I/O -- the add/remove
// pair costs nothing worth keeping state for.
// ============================================================

static fmrb_err_t naryav4_i2c_xfer(uint8_t addr, const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len, uint32_t freq) {
    if (!g_naryav4_i2c) return FMRB_ERR_INVALID_STATE;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = addr;
    dev_cfg.scl_speed_hz    = freq ? freq : NARYAV4_I2C_FREQ;
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(g_naryav4_i2c, &dev_cfg, &dev) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    esp_err_t err;
    if (rx && rx_len) {
        err = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, 100);
    } else {
        err = i2c_master_transmit(dev, tx, tx_len, 100);
    }
    i2c_master_bus_rm_device(dev);
    return err == ESP_OK ? FMRB_OK : FMRB_ERR_FAILED;
}

fmrb_err_t board_display_i2c_write(uint8_t addr, const uint8_t *data,
                                   size_t len, uint32_t freq) {
    return naryav4_i2c_xfer(addr, data, len, NULL, 0, freq);
}

fmrb_err_t board_display_i2c_read(uint8_t addr, uint8_t *data,
                                  size_t len, uint32_t freq) {
    if (!g_naryav4_i2c) return FMRB_ERR_INVALID_STATE;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = addr;
    dev_cfg.scl_speed_hz    = freq ? freq : NARYAV4_I2C_FREQ;
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(g_naryav4_i2c, &dev_cfg, &dev) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    fmrb_err_t res = i2c_master_receive(dev, data, len, 100) == ESP_OK
        ? FMRB_OK : FMRB_ERR_FAILED;
    i2c_master_bus_rm_device(dev);
    return res;
}

fmrb_err_t board_display_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                        uint8_t value, uint32_t freq) {
    const uint8_t buf[2] = { reg, value };
    return naryav4_i2c_xfer(addr, buf, sizeof(buf), NULL, 0, freq);
}

// No headphone jack detect on this board: the amplifier enable is a plain
// GPIO and there is nothing to sense.
void board_display_poll_headphone(void) {}
