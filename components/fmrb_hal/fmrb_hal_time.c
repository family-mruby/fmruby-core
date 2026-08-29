// Target-independent half of the time HAL. The microsecond clock and the
// sub-millisecond busy delay are platform work (platform/esp32/
// fmrb_hal_time_esp32.c on the devices, platform/posix/fmrb_hal_time_posix.c
// on the linux simulation and wasm); everything here is derived from them.
#include "fmrb_hal_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <time.h>

uint64_t fmrb_hal_time_get_ms(void) {
    return fmrb_hal_time_get_us() / 1000ULL;
}

void fmrb_hal_time_delay_ms(uint32_t ms) {
    // Use vTaskDelay for both Linux and ESP32 targets
    // This ensures proper delay in ESP-IDF linux simulation
    vTaskDelay(pdMS_TO_TICKS(ms));
}

bool fmrb_hal_time_is_timeout(fmrb_time_t start_time, uint32_t timeout_us) {
    fmrb_time_t current_time = fmrb_hal_time_get_us();
    return (current_time - start_time) >= timeout_us;
}

fmrb_err_t fmrb_hal_time_get_wallclock(fmrb_wallclock_t *out) {
    if (!out) return FMRB_ERR_INVALID_PARAM;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_buf;
    localtime_r(&tv.tv_sec, &tm_buf);

    out->year   = (uint16_t)(tm_buf.tm_year + 1900);
    out->month  = (uint8_t)(tm_buf.tm_mon + 1);
    out->day    = (uint8_t)tm_buf.tm_mday;
    out->hour   = (uint8_t)tm_buf.tm_hour;
    out->minute = (uint8_t)tm_buf.tm_min;
    out->second = (uint8_t)tm_buf.tm_sec;
    return FMRB_OK;
}
