#include "fmrb_rtc.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "fmrb_hal_i2c.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"

static const char *TAG = "fmrb_rtc";

// Both parts answer on 0x32 and hold the time as seven BCD registers in the
// order sec, min, hour, weekday, day, month, year -- only the first register
// address differs. Retro (S3) carries an RX8900, Modern (P4) an RX8130.
#define RTC_I2C_UNIT 1
#define RTC_I2C_ADDR 0x32
#define RTC_I2C_FREQ 100000
#define RTC_I2C_TIMEOUT_US 100000
#define RTC_TIME_REGS 7

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define RTC_REG_SEC 0x10  // RX8130
#else
#define RTC_REG_SEC 0x00  // RX8900
#endif

static int bcd2dec(uint8_t v)
{
    return (v >> 4) * 10 + (v & 0x0F);
}

static bool is_leap_year(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

/**
 * The registers hold UTC, and the epoch is computed from the fields directly,
 * which is what the Ruby driver did (a device showing 03:08 in its registers
 * reads as 12:08 through a JST-9 log prefix). Doing the arithmetic here rather
 * than through mktime keeps the result independent of the process timezone.
 */
static int64_t fields_to_epoch(int year, int month, int day, int hour, int min, int sec)
{
    static const int mdays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int64_t days = 0;
    for (int y = 1970; y < year; ++y) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int m = 1; m < month; ++m) {
        days += mdays[m];
        if (m == 2 && is_leap_year(year)) {
            days += 1;
        }
    }
    days += day - 1;
    return days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
}

fmrb_err_t fmrb_rtc_sync_system_clock(void)
{
#ifdef CONFIG_IDF_TARGET_LINUX
    // No RTC hardware in the simulation; the host clock is already right.
    return FMRB_ERR_NOT_SUPPORTED;
#else
    if (fmrb_hal_i2c_init(RTC_I2C_UNIT, RTC_I2C_FREQ, FMRB_PIN_I2C1_SDA,
                          FMRB_PIN_I2C1_SCL) != FMRB_I2C_OK) {
        FMRB_LOGE(TAG, "I2C init failed");
        return FMRB_ERR_FAILED;
    }

    // Register read: address the register without releasing the bus, then read
    // the burst. This is the sequence picoruby's I2C#read(addr, len, reg) uses.
    uint8_t reg = RTC_REG_SEC;
    uint8_t buf[RTC_TIME_REGS] = {0};
    fmrb_err_t result = FMRB_OK;

    if (fmrb_hal_i2c_write(RTC_I2C_UNIT, RTC_I2C_ADDR, &reg, 1, true,
                           RTC_I2C_TIMEOUT_US) < 0 ||
        fmrb_hal_i2c_read(RTC_I2C_UNIT, RTC_I2C_ADDR, buf, sizeof(buf), false,
                          RTC_I2C_TIMEOUT_US) < 0) {
        FMRB_LOGW(TAG, "RTC did not answer on I2C%d addr 0x%02X", RTC_I2C_UNIT,
                  RTC_I2C_ADDR);
        result = FMRB_ERR_NOT_FOUND;
        goto release;
    }

    const int sec = bcd2dec(buf[0]);
    const int min = bcd2dec(buf[1]);
    const int hour = bcd2dec(buf[2]);
    const int day = bcd2dec(buf[4]);
    const int month = bcd2dec(buf[5]);
    const int year = 2000 + bcd2dec(buf[6]);

    // A cell that has run flat reads back as zeros or garbage. Setting the
    // clock from that is worse than leaving it: a wrong date propagates into
    // file timestamps and the desktop clock with nothing to say it is wrong.
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || min > 59 ||
        sec > 59 || year < 2020 || year > 2099) {
        FMRB_LOGW(TAG, "RTC time not usable: %04d-%02d-%02d %02d:%02d:%02d "
                       "(battery flat, or never set)",
                  year, month, day, hour, min, sec);
        result = FMRB_ERR_FAILED;
        goto release;
    }

    struct timespec ts = {
        .tv_sec = (time_t)fields_to_epoch(year, month, day, hour, min, sec),
        .tv_nsec = 0
    };
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        FMRB_LOGE(TAG, "clock_settime failed");
        result = FMRB_ERR_FAILED;
        goto release;
    }
    FMRB_LOGI(TAG, "RTC read %04d-%02d-%02d %02d:%02d:%02d UTC (epoch=%lld)",
              year, month, day, hour, min, sec, (long long)ts.tv_sec);

release:
    fmrb_hal_i2c_release(RTC_I2C_UNIT);
    return result;
#endif
}
