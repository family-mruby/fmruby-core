#include "fmrb_hal_i2c.h"
#include "hw_proxy.h"
#include "hw_proxy_internal.h"

int fmrb_hal_i2c_init(int unit, uint32_t frequency, int8_t sda_pin, int8_t scl_pin)
{
    hw_proxy_i2c_init_params_t params = {
        .unit = unit, .freq = frequency, .sda = sda_pin, .scl = scl_pin
    };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_I2C_INIT, .params = &params };
    fmrb_err_t ret = hw_proxy_call(&req);
    return (ret == FMRB_OK) ? FMRB_I2C_OK : FMRB_I2C_ERROR;
}

int fmrb_hal_i2c_read(int unit, uint8_t addr, uint8_t *dst, size_t len,
                       bool nostop, uint32_t timeout_us)
{
    hw_proxy_i2c_rw_params_t params = {
        .unit = unit, .addr = addr, .buf = dst, .len = len,
        .nostop = nostop, .timeout_us = timeout_us
    };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_I2C_READ, .params = &params };
    fmrb_err_t ret = hw_proxy_call(&req);
    return (ret == FMRB_OK) ? (int)len : -1;
}

int fmrb_hal_i2c_write(int unit, uint8_t addr, uint8_t *src, size_t len,
                        bool nostop, uint32_t timeout_us)
{
    hw_proxy_i2c_rw_params_t params = {
        .unit = unit, .addr = addr, .buf = src, .len = len,
        .nostop = nostop, .timeout_us = timeout_us
    };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_I2C_WRITE, .params = &params };
    fmrb_err_t ret = hw_proxy_call(&req);
    return (ret == FMRB_OK) ? (int)len : -1;
}
