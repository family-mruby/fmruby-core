#include "fmrb_hal_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef FMRB_PLATFORM_LINUX
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#else
#include "esp_timer.h"
#endif

#ifdef FMRB_PLATFORM_LINUX
static struct timeval boot_time;
static bool time_initialized = false;

static void ensure_time_init(void) {
    if (!time_initialized) {
        gettimeofday(&boot_time, NULL);
        time_initialized = true;
    }
}
#endif

fmrb_time_t fmrb_hal_time_get_us(void) {
#ifdef FMRB_PLATFORM_LINUX
    ensure_time_init();
    struct timeval now;
    gettimeofday(&now, NULL);
    return (fmrb_time_t)(now.tv_sec - boot_time.tv_sec) * 1000000ULL +
           (now.tv_usec - boot_time.tv_usec);
#else
    return esp_timer_get_time();
#endif
}

uint64_t fmrb_hal_time_get_ms(void) {
    return fmrb_hal_time_get_us() / 1000ULL;
}

void fmrb_hal_time_delay_us(uint32_t us) {
#ifdef FMRB_PLATFORM_LINUX
    usleep(us);
#else
    if (us >= 1000) {
        vTaskDelay(pdMS_TO_TICKS(us / 1000));
        us %= 1000;
    }
    if (us > 0) {
        esp_rom_delay_us(us);
    }
#endif
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