#include "fmrb_hal_rmt.h"
#include "hw_proxy.h"
#include "hw_proxy_internal.h"

int fmrb_hal_rmt_init(uint32_t gpio, uint32_t t0h_ns, uint32_t t0l_ns,
                       uint32_t t1h_ns, uint32_t t1l_ns, uint32_t reset_ns)
{
    hw_proxy_rmt_init_params_t params = {
        .gpio = gpio, .t0h_ns = t0h_ns, .t0l_ns = t0l_ns,
        .t1h_ns = t1h_ns, .t1l_ns = t1l_ns, .reset_ns = reset_ns
    };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_RMT_INIT, .params = &params };
    fmrb_err_t ret = hw_proxy_call(&req);
    return (ret == FMRB_OK) ? 0 : -1;
}

int fmrb_hal_rmt_write(uint8_t *buffer, uint32_t nbytes)
{
    hw_proxy_rmt_write_params_t params = { .buffer = buffer, .nbytes = nbytes };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_RMT_WRITE, .params = &params };
    fmrb_err_t ret = hw_proxy_call(&req);
    return (ret == FMRB_OK) ? 0 : -1;
}
