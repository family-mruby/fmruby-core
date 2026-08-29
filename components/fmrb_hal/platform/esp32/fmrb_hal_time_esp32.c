// ESP32 side of the time HAL: the esp_timer microsecond clock and the ROM
// busy-wait for the sub-millisecond remainder.
#include "fmrb_hal_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

fmrb_time_t fmrb_hal_time_get_us(void) {
    return esp_timer_get_time();
}

void fmrb_hal_time_delay_us(uint32_t us) {
    if (us >= 1000) {
        vTaskDelay(pdMS_TO_TICKS(us / 1000));
        us %= 1000;
    }
    if (us > 0) {
        esp_rom_delay_us(us);
    }
}
