// POSIX side of the time HAL, shared by the linux simulation and the wasm
// build: a boot-relative gettimeofday clock and usleep.
#include "fmrb_hal_time.h"
#include <sys/time.h>
#include <stdbool.h>
#include <unistd.h>

static struct timeval boot_time;
static bool time_initialized = false;

static void ensure_time_init(void) {
    if (!time_initialized) {
        gettimeofday(&boot_time, NULL);
        time_initialized = true;
    }
}

fmrb_time_t fmrb_hal_time_get_us(void) {
    ensure_time_init();
    struct timeval now;
    gettimeofday(&now, NULL);
    return (fmrb_time_t)(now.tv_sec - boot_time.tv_sec) * 1000000ULL +
           (now.tv_usec - boot_time.tv_usec);
}

void fmrb_hal_time_delay_us(uint32_t us) {
    usleep(us);
}
