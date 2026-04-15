#include <string.h>
#include "i2c.h"
#include "fmrb_hal_i2c.h"

int I2C_unit_name_to_unit_num(const char *unit_name)
{
    if (strcmp(unit_name, "ESP32_I2C0") == 0) {
        return 0;
    } else if (strcmp(unit_name, "ESP32_I2C1") == 0) {
        return 1;
    }
    return I2C_ERROR_INVALID_UNIT;
}

i2c_status_t I2C_gpio_init(int unit_num, uint32_t frequency, int8_t sda_pin, int8_t scl_pin)
{
    int ret = fmrb_hal_i2c_init(unit_num, frequency, sda_pin, scl_pin);
    return (ret == FMRB_I2C_OK) ? I2C_ERROR_NONE : I2C_ERROR_INVALID_UNIT;
}

void I2C_release(int unit_num)
{
    fmrb_hal_i2c_release(unit_num);
}

int I2C_read_timeout_us(int unit_num, uint8_t addr, uint8_t *dst, size_t len,
                         bool nostop, uint32_t timeout_us)
{
    return fmrb_hal_i2c_read(unit_num, addr, dst, len, nostop, timeout_us);
}

int I2C_write_timeout_us(int unit_num, uint8_t addr, uint8_t *src, size_t len,
                          bool nostop, uint32_t timeout_us)
{
    return fmrb_hal_i2c_write(unit_num, addr, src, len, nostop, timeout_us);
}
