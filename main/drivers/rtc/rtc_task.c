#include "rtc_task.h"
#include "rx8900.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"

// Stack size, priority, and flags are defined in fmrb_task_config.h
#define RTC_I2C_BUS          1

static const char *TAG = "rtc_task";

static void rtc_task(void *pvParameters)
{
    (void)pvParameters;

    fmrb_err_t err = rx8900_init(RTC_I2C_BUS);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "RX8900 init failed: %d", err);
        fmrb_task_delete(NULL);
        return;
    }

    /* Set time to 2026/1/1 00:00:00 */
    rx8900_time_t set_time = {
        .sec   = 0,
        .min   = 0,
        .hour  = 0,
        .day   = 1,
        .month = 1,
        .year  = 26,  /* 2026 - 2000 */
    };

    err = rx8900_write(&set_time);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "RX8900 write failed: %d", err);
        rx8900_deinit();
        fmrb_task_delete(NULL);
        return;
    }
    FMRB_LOGI(TAG, "RTC set to 2026/01/01 00:00:00");

    /* Wait 10 seconds */
    fmrb_task_delay_ms(10000);

    /* Read back */
    rx8900_time_t read_time;
    err = rx8900_read(&read_time);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "RX8900 read failed: %d", err);
    } else {
        FMRB_LOGI(TAG, "RTC read: %04d/%02d/%02d %02d:%02d:%02d",
                   2000 + read_time.year, read_time.month, read_time.day,
                   read_time.hour, read_time.min, read_time.sec);
    }

    /* Read temperature */
    float temp;
    err = rx8900_read_temperature(&temp);
    if (err == FMRB_OK) {
        FMRB_LOGI(TAG, "RTC temperature: %.1f C", temp);
    }

    /* Check voltage flags */
    bool vlf, vdet;
    if (rx8900_check_vlf(&vlf) == FMRB_OK && vlf) {
        FMRB_LOGW(TAG, "VLF flag set: voltage dropped below 1.6V or oscillation stopped");
    }
    if (rx8900_check_vdet(&vdet) == FMRB_OK && vdet) {
        FMRB_LOGW(TAG, "VDET flag set: voltage dropped below 1.95V");
    }

    FMRB_LOGI(TAG, "RTC task done");
    fmrb_task_delete(NULL);
}

fmrb_err_t rtc_task_start(void)
{
    fmrb_task_handle_t handle;
    fmrb_task_create_ex(rtc_task, "rtc_task",
                     FMRB_RTC_TASK_STACK_SIZE, NULL,
                     FMRB_RTC_TASK_PRIORITY, &handle,
                     FMRB_RTC_TASK_FLAGS);
    FMRB_LOGI(TAG, "RTC task started");
    return FMRB_OK;
}
