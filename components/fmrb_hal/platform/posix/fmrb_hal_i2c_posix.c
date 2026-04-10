#include "fmrb_hal_i2c.h"
#include "esp_log.h"

static const char *TAG = "fmrb_hal_i2c";

int fmrb_hal_i2c_init(int unit, uint32_t frequency, int8_t sda_pin, int8_t scl_pin)
{
    ESP_LOGI(TAG, "Linux I2C%d init (freq=%lu, sda=%d, scl=%d)", unit, (unsigned long)frequency, sda_pin, scl_pin);
    return FMRB_I2C_OK;
}

int fmrb_hal_i2c_read(int unit, uint8_t addr, uint8_t *dst, size_t len,
                       bool nostop, uint32_t timeout_us)
{
    (void)nostop;
    (void)timeout_us;
    ESP_LOGI(TAG, "Linux I2C%d read addr=0x%02x len=%zu", unit, addr, len);
    return (int)len;
}

int fmrb_hal_i2c_write(int unit, uint8_t addr, uint8_t *src, size_t len,
                        bool nostop, uint32_t timeout_us)
{
    (void)nostop;
    (void)timeout_us;
    ESP_LOGI(TAG, "Linux I2C%d write addr=0x%02x len=%zu", unit, addr, len);
    return (int)len;
}
