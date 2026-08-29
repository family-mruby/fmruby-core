#include "fmrb_hal_rmt.h"
#include "fmrb_log_port.h"

static const char *TAG = "fmrb_hal_rmt";

int fmrb_hal_rmt_init(uint32_t gpio, uint32_t t0h_ns, uint32_t t0l_ns,
                       uint32_t t1h_ns, uint32_t t1l_ns, uint32_t reset_ns)
{
    ESP_LOGI(TAG, "Linux RMT init (gpio=%lu)", (unsigned long)gpio);
    (void)t0h_ns; (void)t0l_ns; (void)t1h_ns; (void)t1l_ns; (void)reset_ns;
    return 0;
}

int fmrb_hal_rmt_write(uint8_t *buffer, uint32_t nbytes)
{
    ESP_LOGI(TAG, "Linux RMT write (%lu bytes)", (unsigned long)nbytes);
    (void)buffer;
    return 0;
}
