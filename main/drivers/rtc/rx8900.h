#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RX8900 I2C Address (7-bit) */
#define RX8900_I2C_ADDR  0x32

/* RX8900 Register Addresses */
#define RX8900_REG_SEC                0x00
#define RX8900_REG_MIN                0x01
#define RX8900_REG_HOUR               0x02
#define RX8900_REG_WEEK               0x03
#define RX8900_REG_DAY                0x04
#define RX8900_REG_MONTH              0x05
#define RX8900_REG_YEAR               0x06
#define RX8900_REG_RAM                0x07
#define RX8900_REG_MIN_ALARM          0x08
#define RX8900_REG_HOUR_ALARM         0x09
#define RX8900_REG_WEEK_DAY_ALARM     0x0A
#define RX8900_REG_TIMER_COUNTER_0    0x0B
#define RX8900_REG_TIMER_COUNTER_1    0x0C
#define RX8900_REG_EXTENSION          0x0D
#define RX8900_REG_FLAG               0x0E
#define RX8900_REG_CONTROL            0x0F
#define RX8900_REG_TEMP               0x17
#define RX8900_REG_BACKUP             0x18

/* Week day bitmasks */
#define RX8900_WEEK_SUN  0x01
#define RX8900_WEEK_MON  0x02
#define RX8900_WEEK_TUE  0x04
#define RX8900_WEEK_WED  0x08
#define RX8900_WEEK_THU  0x10
#define RX8900_WEEK_FRI  0x20
#define RX8900_WEEK_SAT  0x40

/* Week/Day alarm type */
typedef enum {
    RX8900_ALARM_NONE = 0,
    RX8900_ALARM_WEEK = 1,
    RX8900_ALARM_DAY  = 2,
} rx8900_alarm_type_t;

/* Source clock for fixed-cycle timer */
typedef enum {
    RX8900_CLOCK_4096HZ  = 0x00,
    RX8900_CLOCK_64HZ    = 0x01,
    RX8900_CLOCK_SECOND  = 0x02,
    RX8900_CLOCK_MINUTE  = 0x03,
} rx8900_source_clock_t;

/* Update interrupt select */
typedef enum {
    RX8900_USEL_SECOND = 0x00,
    RX8900_USEL_MINUTE = 0x20,
} rx8900_usel_t;

/* Time structure for RX8900 */
typedef struct {
    uint8_t sec;    /* 0-59 */
    uint8_t min;    /* 0-59 */
    uint8_t hour;   /* 0-23 */
    uint8_t wday;   /* week bitmask */
    uint8_t day;    /* 1-31 */
    uint8_t month;  /* 1-12 */
    uint8_t year;   /* 0-99 (offset from 2000) */
} rx8900_time_t;

/**
 * @brief Initialize the RX8900 RTC driver.
 * @param i2c_bus_num I2C bus number (1 or 2)
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_init(int i2c_bus_num);

/**
 * @brief Deinitialize the RX8900 RTC driver.
 */
void rx8900_deinit(void);

/**
 * @brief Read the current time from the RTC.
 * @param[out] t Time structure to fill
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_read(rx8900_time_t *t);

/**
 * @brief Write time to the RTC.
 * @param t Time structure to write
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_write(const rx8900_time_t *t);

/**
 * @brief Read the current time as a POSIX time_t.
 * @param[out] epoch Pointer to store the time
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_get_epoch(time_t *epoch);

/**
 * @brief Set the RTC from a POSIX time_t.
 * @param epoch Time to set
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_set_epoch(time_t epoch);

/**
 * @brief Set a full alarm (minute/hour/week-or-day).
 * @param type Alarm type (NONE, WEEK, DAY)
 * @param minute Minute match (0-59), or 0xFF to disable minute match
 * @param hour Hour match (0-23), or 0xFF to disable hour match
 * @param daydate Day-of-month or week bitmask depending on type
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_set_alarm(rx8900_alarm_type_t type, uint8_t minute,
                             uint8_t hour, uint8_t daydate);

/**
 * @brief Disable the alarm.
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_disable_alarm(void);

/**
 * @brief Enable or disable alarm interrupt on INT pin.
 * @param enable true to enable, false to disable
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_alarm_interrupt(bool enable);

/**
 * @brief Check alarm flag and reset it if set.
 * @param[out] fired true if alarm was triggered
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_check_alarm(bool *fired);

/**
 * @brief Set a fixed-cycle timer.
 * @param counter Timer counter value (1-4095)
 * @param clock Source clock selection
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_set_timer(uint16_t counter, rx8900_source_clock_t clock);

/**
 * @brief Disable the fixed-cycle timer.
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_disable_timer(void);

/**
 * @brief Enable or disable timer interrupt on INT pin.
 * @param enable true to enable, false to disable
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_timer_interrupt(bool enable);

/**
 * @brief Check timer flag and reset it if set.
 * @param[out] fired true if timer was triggered
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_check_timer(bool *fired);

/**
 * @brief Set time update interrupt timing.
 * @param usel Second or minute update
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_set_update_timer(rx8900_usel_t usel);

/**
 * @brief Enable or disable time update interrupt on INT pin.
 * @param enable true to enable, false to disable
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_update_interrupt(bool enable);

/**
 * @brief Check update flag and reset it if set.
 * @param[out] fired true if update event occurred
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_check_update(bool *fired);

/**
 * @brief Read the temperature from the RTC.
 * @param[out] temp_c Temperature in degrees Celsius
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_read_temperature(float *temp_c);

/**
 * @brief Check VLF (Voltage Low Flag).
 * @param[out] vlf true if supply voltage dropped below 1.6V or oscillation stopped
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_check_vlf(bool *vlf);

/**
 * @brief Check VDET (Voltage Detection Flag).
 * @param[out] vdet true if supply voltage dropped below 1.95V
 * @return FMRB_OK on success
 */
fmrb_err_t rx8900_check_vdet(bool *vdet);

#ifdef __cplusplus
}
#endif
