/*
 * RX8900 RTC driver for ESP-IDF
 *
 * Ported from the Arduino RX8900RTC library.
 * Original library based on sample sketch by AKIZUKI DENSHI TSUSHO CO.,LTD.
 */
#include "rx8900.h"
#include "fmrb_log.h"
#include "fmrb_pin_assign.h"
#include "fmrb_hal_time.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/i2c_master.h"
#endif

static const char *TAG = "rx8900";

/* I2C timeout in milliseconds */
#define RX8900_I2C_TIMEOUT_MS  100

/* Control Register bits */
#define CTRL_RESET_BIT  0x01
#define CTRL_AIE_BIT    0x08
#define CTRL_TIE_BIT    0x10
#define CTRL_UIE_BIT    0x20

/* Flag Register bits */
#define FLAG_VDET_BIT   0x01
#define FLAG_VLF_BIT    0x02
#define FLAG_AF_BIT     0x08
#define FLAG_TF_BIT     0x10
#define FLAG_UF_BIT     0x20

/* Extension Register bits */
#define EXT_TE_BIT      0x10
#define EXT_USEL_BIT    0x20
#define EXT_WADA_BIT    0x40
#define EXT_TSEL_MASK   0x03

/* Alarm Enable bit (bit7 of alarm registers) */
#define ALARM_AE_BIT    0x80

#ifndef CONFIG_IDF_TARGET_LINUX

static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

static fmrb_err_t byte_write(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = { reg, data };
    esp_err_t ret = i2c_master_transmit(s_dev_handle, buf, 2, RX8900_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "I2C write failed reg=0x%02X: %s", reg, esp_err_to_name(ret));
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

static fmrb_err_t byte_read(uint8_t reg, uint8_t *data)
{
    esp_err_t ret = i2c_master_transmit_receive(s_dev_handle,
                                                 &reg, 1, data, 1,
                                                 RX8900_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "I2C read failed reg=0x%02X: %s", reg, esp_err_to_name(ret));
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

static fmrb_err_t burst_read(uint8_t start_reg, uint8_t *buf, size_t len)
{
    esp_err_t ret = i2c_master_transmit_receive(s_dev_handle,
                                                 &start_reg, 1, buf, len,
                                                 RX8900_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "I2C burst read failed reg=0x%02X len=%d: %s",
                   start_reg, (int)len, esp_err_to_name(ret));
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

static fmrb_err_t reg_set_bits(uint8_t reg, uint8_t bits)
{
    uint8_t val;
    fmrb_err_t err = byte_read(reg, &val);
    if (err != FMRB_OK) return err;
    return byte_write(reg, val | bits);
}

static fmrb_err_t reg_clear_bits(uint8_t reg, uint8_t bits)
{
    uint8_t val;
    fmrb_err_t err = byte_read(reg, &val);
    if (err != FMRB_OK) return err;
    return byte_write(reg, val & ~bits);
}

static fmrb_err_t reg_modify(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t val;
    fmrb_err_t err = byte_read(reg, &val);
    if (err != FMRB_OK) return err;
    return byte_write(reg, (val & ~mask) | (value & mask));
}

static uint8_t dec2bcd(uint8_t n)
{
    return n + 6 * (n / 10);
}

static uint8_t bcd2dec(uint8_t n)
{
    return n - 6 * (n >> 4);
}

/* Zeller's congruence: returns 0=Sunday .. 6=Saturday */
static uint8_t zeller(uint16_t y, uint16_t m, uint16_t d)
{
    if (m < 3) {
        y--;
        m += 12;
    }
    return (y + y / 4 - y / 100 + y / 400 + (13 * m + 8) / 5 + d) % 7;
}

static fmrb_err_t reset_subsecond(void)
{
    return reg_set_bits(RX8900_REG_CONTROL, CTRL_RESET_BIT);
}

fmrb_err_t rx8900_init(int i2c_bus_num)
{
    int sda, scl;
    int i2c_port;

    if (i2c_bus_num == 1) {
        sda = FMRB_PIN_I2C1_SDA;
        scl = FMRB_PIN_I2C1_SCL;
        i2c_port = 0;
    } else if (i2c_bus_num == 2) {
        sda = FMRB_PIN_I2C2_SDA;
        scl = FMRB_PIN_I2C2_SCL;
        i2c_port = 1;
    } else {
        FMRB_LOGE(TAG, "Invalid I2C bus number: %d", i2c_bus_num);
        return FMRB_ERR_INVALID_PARAM;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = i2c_port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return FMRB_ERR_FAILED;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RX8900_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    ret = i2c_master_bus_add_device(s_bus_handle, &dev_config, &s_dev_handle);
    if (ret != ESP_OK) {
        FMRB_LOGE(TAG, "Failed to add RX8900 device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
        return FMRB_ERR_FAILED;
    }

    /* Wait for oscillation start */
    fmrb_hal_time_delay_ms(1000);

    /* Initialize registers: WEEK ALARM mode, 1Hz FOUT */
    fmrb_err_t err;
    err = byte_write(RX8900_REG_EXTENSION, 0x08);
    if (err != FMRB_OK) return err;

    err = byte_write(RX8900_REG_FLAG, 0x00);
    if (err != FMRB_OK) return err;

    /* Control: CSEL1=1 (1Hz FOUT), do not set RESET bit */
    err = byte_write(RX8900_REG_CONTROL, 0x40);
    if (err != FMRB_OK) return err;

    FMRB_LOGI(TAG, "RX8900 initialized on I2C%d", i2c_bus_num);
    return FMRB_OK;
}

void rx8900_deinit(void)
{
    if (s_dev_handle != NULL) {
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
    }
    if (s_bus_handle != NULL) {
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
    }
}

fmrb_err_t rx8900_read(rx8900_time_t *t)
{
    if (t == NULL) return FMRB_ERR_INVALID_PARAM;

    /* Burst read registers 0x00-0x06 (SEC..YEAR) */
    uint8_t buf[7];
    fmrb_err_t err = burst_read(RX8900_REG_SEC, buf, 7);
    if (err != FMRB_OK) return err;

    t->sec   = bcd2dec(buf[0]);
    t->min   = bcd2dec(buf[1]);
    t->hour  = bcd2dec(buf[2]);
    t->wday  = buf[3];
    t->day   = bcd2dec(buf[4]);
    t->month = bcd2dec(buf[5]);
    t->year  = bcd2dec(buf[6]);

    return FMRB_OK;
}

fmrb_err_t rx8900_write(const rx8900_time_t *t)
{
    if (t == NULL) return FMRB_ERR_INVALID_PARAM;

    /* Calculate day-of-week from the date (year is offset from 2000) */
    uint8_t dow = zeller(2000 + t->year, t->month, t->day);
    uint8_t wday = 1 << dow;

    fmrb_err_t err = reset_subsecond();
    if (err != FMRB_OK) return err;

    err = byte_write(RX8900_REG_SEC,   dec2bcd(t->sec));
    if (err != FMRB_OK) return err;
    err = byte_write(RX8900_REG_MIN,   dec2bcd(t->min));
    if (err != FMRB_OK) return err;
    err = byte_write(RX8900_REG_HOUR,  dec2bcd(t->hour));
    if (err != FMRB_OK) return err;
    err = byte_write(RX8900_REG_WEEK,  wday);
    if (err != FMRB_OK) return err;
    err = byte_write(RX8900_REG_DAY,   dec2bcd(t->day));
    if (err != FMRB_OK) return err;
    err = byte_write(RX8900_REG_MONTH, dec2bcd(t->month));
    if (err != FMRB_OK) return err;
    err = byte_write(RX8900_REG_YEAR,  dec2bcd(t->year));
    if (err != FMRB_OK) return err;

    return FMRB_OK;
}

fmrb_err_t rx8900_get_epoch(time_t *epoch)
{
    if (epoch == NULL) return FMRB_ERR_INVALID_PARAM;

    rx8900_time_t rt;
    fmrb_err_t err = rx8900_read(&rt);
    if (err != FMRB_OK) return err;

    struct tm tm_val = {
        .tm_sec  = rt.sec,
        .tm_min  = rt.min,
        .tm_hour = rt.hour,
        .tm_mday = rt.day,
        .tm_mon  = rt.month - 1,  /* struct tm months: 0-11 */
        .tm_year = rt.year + 100, /* struct tm year: since 1900, RX8900 year: since 2000 */
        .tm_isdst = -1,
    };

    *epoch = mktime(&tm_val);
    return FMRB_OK;
}

fmrb_err_t rx8900_set_epoch(time_t epoch)
{
    struct tm tm_val;
    gmtime_r(&epoch, &tm_val);

    rx8900_time_t rt = {
        .sec   = (uint8_t)tm_val.tm_sec,
        .min   = (uint8_t)tm_val.tm_min,
        .hour  = (uint8_t)tm_val.tm_hour,
        .day   = (uint8_t)tm_val.tm_mday,
        .month = (uint8_t)(tm_val.tm_mon + 1),
        .year  = (uint8_t)(tm_val.tm_year - 100), /* 2000-based from 1900-based */
    };

    return rx8900_write(&rt);
}

fmrb_err_t rx8900_set_alarm(rx8900_alarm_type_t type, uint8_t minute,
                             uint8_t hour, uint8_t daydate)
{
    fmrb_err_t err;

    /* Disable alarm interrupt first */
    err = reg_clear_bits(RX8900_REG_CONTROL, CTRL_AIE_BIT);
    if (err != FMRB_OK) return err;

    /* Minute alarm */
    if (minute < 60) {
        err = byte_write(RX8900_REG_MIN_ALARM, dec2bcd(minute));
    } else {
        err = reg_set_bits(RX8900_REG_MIN_ALARM, ALARM_AE_BIT);
    }
    if (err != FMRB_OK) return err;

    /* Hour alarm */
    if (hour < 24) {
        err = byte_write(RX8900_REG_HOUR_ALARM, dec2bcd(hour));
    } else {
        err = reg_set_bits(RX8900_REG_HOUR_ALARM, ALARM_AE_BIT);
    }
    if (err != FMRB_OK) return err;

    /* Week/Day alarm */
    if (type == RX8900_ALARM_DAY) {
        err = reg_set_bits(RX8900_REG_EXTENSION, EXT_WADA_BIT);
        if (err != FMRB_OK) return err;
        err = byte_write(RX8900_REG_WEEK_DAY_ALARM, dec2bcd(daydate));
    } else if (type == RX8900_ALARM_WEEK) {
        err = reg_clear_bits(RX8900_REG_EXTENSION, EXT_WADA_BIT);
        if (err != FMRB_OK) return err;
        err = byte_write(RX8900_REG_WEEK_DAY_ALARM, dec2bcd(daydate));
    } else {
        err = reg_set_bits(RX8900_REG_WEEK_DAY_ALARM, ALARM_AE_BIT);
    }
    if (err != FMRB_OK) return err;

    /* Reset alarm flag */
    err = reg_clear_bits(RX8900_REG_FLAG, FLAG_AF_BIT);
    return err;
}

fmrb_err_t rx8900_disable_alarm(void)
{
    fmrb_err_t err;

    err = reg_clear_bits(RX8900_REG_CONTROL, CTRL_AIE_BIT);
    if (err != FMRB_OK) return err;

    err = reg_clear_bits(RX8900_REG_FLAG, FLAG_AF_BIT);
    if (err != FMRB_OK) return err;

    /* Set to WEEK mode and clear alarm register */
    err = reg_clear_bits(RX8900_REG_EXTENSION, EXT_WADA_BIT);
    if (err != FMRB_OK) return err;

    err = byte_write(RX8900_REG_WEEK_DAY_ALARM, 0);
    if (err != FMRB_OK) return err;

    return reg_clear_bits(RX8900_REG_FLAG, FLAG_AF_BIT);
}

fmrb_err_t rx8900_alarm_interrupt(bool enable)
{
    if (enable) {
        return reg_set_bits(RX8900_REG_CONTROL, CTRL_AIE_BIT);
    } else {
        return reg_clear_bits(RX8900_REG_CONTROL, CTRL_AIE_BIT);
    }
}

fmrb_err_t rx8900_check_alarm(bool *fired)
{
    if (fired == NULL) return FMRB_ERR_INVALID_PARAM;

    uint8_t flag;
    fmrb_err_t err = byte_read(RX8900_REG_FLAG, &flag);
    if (err != FMRB_OK) return err;

    *fired = (flag & FLAG_AF_BIT) != 0;
    if (*fired) {
        err = reg_clear_bits(RX8900_REG_FLAG, FLAG_AF_BIT);
    }
    return err;
}

fmrb_err_t rx8900_set_timer(uint16_t counter, rx8900_source_clock_t clock)
{
    fmrb_err_t err;

    /* Disable timer */
    err = reg_clear_bits(RX8900_REG_EXTENSION, EXT_TE_BIT);
    if (err != FMRB_OK) return err;

    err = reg_clear_bits(RX8900_REG_FLAG, FLAG_TF_BIT);
    if (err != FMRB_OK) return err;

    err = reg_clear_bits(RX8900_REG_CONTROL, CTRL_TIE_BIT);
    if (err != FMRB_OK) return err;

    /* Set counter value */
    err = byte_write(RX8900_REG_TIMER_COUNTER_0, counter & 0xFF);
    if (err != FMRB_OK) return err;

    err = byte_write(RX8900_REG_TIMER_COUNTER_1, (counter >> 8) & 0x0F);
    if (err != FMRB_OK) return err;

    /* Set source clock */
    err = reg_modify(RX8900_REG_EXTENSION, EXT_TSEL_MASK, (uint8_t)clock);
    if (err != FMRB_OK) return err;

    /* Enable timer */
    err = reg_set_bits(RX8900_REG_EXTENSION, EXT_TE_BIT);
    if (err != FMRB_OK) return err;

    return reg_clear_bits(RX8900_REG_FLAG, FLAG_TF_BIT);
}

fmrb_err_t rx8900_disable_timer(void)
{
    fmrb_err_t err;

    err = reg_clear_bits(RX8900_REG_EXTENSION, EXT_TE_BIT);
    if (err != FMRB_OK) return err;

    err = reg_clear_bits(RX8900_REG_FLAG, FLAG_TF_BIT);
    if (err != FMRB_OK) return err;

    return reg_clear_bits(RX8900_REG_CONTROL, CTRL_TIE_BIT);
}

fmrb_err_t rx8900_timer_interrupt(bool enable)
{
    if (enable) {
        return reg_set_bits(RX8900_REG_CONTROL, CTRL_TIE_BIT);
    } else {
        return reg_clear_bits(RX8900_REG_CONTROL, CTRL_TIE_BIT);
    }
}

fmrb_err_t rx8900_check_timer(bool *fired)
{
    if (fired == NULL) return FMRB_ERR_INVALID_PARAM;

    uint8_t flag;
    fmrb_err_t err = byte_read(RX8900_REG_FLAG, &flag);
    if (err != FMRB_OK) return err;

    *fired = (flag & FLAG_TF_BIT) != 0;
    if (*fired) {
        err = reg_clear_bits(RX8900_REG_FLAG, FLAG_TF_BIT);
    }
    return err;
}

fmrb_err_t rx8900_set_update_timer(rx8900_usel_t usel)
{
    fmrb_err_t err;

    err = reg_clear_bits(RX8900_REG_CONTROL, CTRL_UIE_BIT);
    if (err != FMRB_OK) return err;

    return reg_modify(RX8900_REG_EXTENSION, EXT_USEL_BIT, (uint8_t)usel);
}

fmrb_err_t rx8900_update_interrupt(bool enable)
{
    if (enable) {
        return reg_set_bits(RX8900_REG_CONTROL, CTRL_UIE_BIT);
    } else {
        return reg_clear_bits(RX8900_REG_CONTROL, CTRL_UIE_BIT);
    }
}

fmrb_err_t rx8900_check_update(bool *fired)
{
    if (fired == NULL) return FMRB_ERR_INVALID_PARAM;

    uint8_t flag;
    fmrb_err_t err = byte_read(RX8900_REG_FLAG, &flag);
    if (err != FMRB_OK) return err;

    *fired = (flag & FLAG_UF_BIT) != 0;
    if (*fired) {
        err = reg_clear_bits(RX8900_REG_FLAG, FLAG_UF_BIT);
    }
    return err;
}

fmrb_err_t rx8900_read_temperature(float *temp_c)
{
    if (temp_c == NULL) return FMRB_ERR_INVALID_PARAM;

    uint8_t raw;
    fmrb_err_t err = byte_read(RX8900_REG_TEMP, &raw);
    if (err != FMRB_OK) return err;

    *temp_c = (raw * 2.0f - 187.19f) / 3.218f;
    return FMRB_OK;
}

fmrb_err_t rx8900_check_vlf(bool *vlf)
{
    if (vlf == NULL) return FMRB_ERR_INVALID_PARAM;

    uint8_t flag;
    fmrb_err_t err = byte_read(RX8900_REG_FLAG, &flag);
    if (err != FMRB_OK) return err;

    *vlf = (flag & FLAG_VLF_BIT) != 0;
    return FMRB_OK;
}

fmrb_err_t rx8900_check_vdet(bool *vdet)
{
    if (vdet == NULL) return FMRB_ERR_INVALID_PARAM;

    uint8_t flag;
    fmrb_err_t err = byte_read(RX8900_REG_FLAG, &flag);
    if (err != FMRB_OK) return err;

    *vdet = (flag & FLAG_VDET_BIT) != 0;
    return FMRB_OK;
}

#else
/* Linux stubs */

fmrb_err_t rx8900_init(int i2c_bus_num)
{
    (void)i2c_bus_num;
    FMRB_LOGI(TAG, "RX8900 not available on Linux target");
    return FMRB_OK;
}

void rx8900_deinit(void) {}

fmrb_err_t rx8900_read(rx8900_time_t *t)
{
    if (t == NULL) return FMRB_ERR_INVALID_PARAM;

    time_t now = time(NULL);
    struct tm tm_val;
    gmtime_r(&now, &tm_val);

    t->sec   = (uint8_t)tm_val.tm_sec;
    t->min   = (uint8_t)tm_val.tm_min;
    t->hour  = (uint8_t)tm_val.tm_hour;
    t->wday  = 1 << tm_val.tm_wday;
    t->day   = (uint8_t)tm_val.tm_mday;
    t->month = (uint8_t)(tm_val.tm_mon + 1);
    t->year  = (uint8_t)(tm_val.tm_year - 100);
    return FMRB_OK;
}

fmrb_err_t rx8900_write(const rx8900_time_t *t)
{
    (void)t;
    return FMRB_OK;
}

fmrb_err_t rx8900_get_epoch(time_t *epoch)
{
    if (epoch == NULL) return FMRB_ERR_INVALID_PARAM;
    *epoch = time(NULL);
    return FMRB_OK;
}

fmrb_err_t rx8900_set_epoch(time_t epoch)
{
    (void)epoch;
    return FMRB_OK;
}

fmrb_err_t rx8900_set_alarm(rx8900_alarm_type_t type, uint8_t minute,
                             uint8_t hour, uint8_t daydate)
{
    (void)type; (void)minute; (void)hour; (void)daydate;
    return FMRB_OK;
}

fmrb_err_t rx8900_disable_alarm(void) { return FMRB_OK; }
fmrb_err_t rx8900_alarm_interrupt(bool enable) { (void)enable; return FMRB_OK; }
fmrb_err_t rx8900_check_alarm(bool *fired) { if (fired) *fired = false; return FMRB_OK; }
fmrb_err_t rx8900_set_timer(uint16_t counter, rx8900_source_clock_t clock) { (void)counter; (void)clock; return FMRB_OK; }
fmrb_err_t rx8900_disable_timer(void) { return FMRB_OK; }
fmrb_err_t rx8900_timer_interrupt(bool enable) { (void)enable; return FMRB_OK; }
fmrb_err_t rx8900_check_timer(bool *fired) { if (fired) *fired = false; return FMRB_OK; }
fmrb_err_t rx8900_set_update_timer(rx8900_usel_t usel) { (void)usel; return FMRB_OK; }
fmrb_err_t rx8900_update_interrupt(bool enable) { (void)enable; return FMRB_OK; }
fmrb_err_t rx8900_check_update(bool *fired) { if (fired) *fired = false; return FMRB_OK; }

fmrb_err_t rx8900_read_temperature(float *temp_c)
{
    if (temp_c) *temp_c = 25.0f;
    return FMRB_OK;
}

fmrb_err_t rx8900_check_vlf(bool *vlf) { if (vlf) *vlf = false; return FMRB_OK; }
fmrb_err_t rx8900_check_vdet(bool *vdet) { if (vdet) *vdet = false; return FMRB_OK; }

#endif
