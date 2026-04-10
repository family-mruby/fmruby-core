#include "rmt.h"
#include "fmrb_hal_rmt.h"

int RMT_init(uint32_t gpio, RMT_symbol_dulation_t *rsd)
{
    return fmrb_hal_rmt_init(gpio, rsd->t0h_ns, rsd->t0l_ns,
                              rsd->t1h_ns, rsd->t1l_ns, rsd->reset_ns);
}

int RMT_write(uint8_t *buffer, uint32_t nbytes)
{
    return fmrb_hal_rmt_write(buffer, nbytes);
}
