#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../include/hal.h"
#include "../../include/machine.h"

#include "esp_sleep.h"
#include "hal/efuse_hal.h"

#define ESP32_MSEC_PER_TICK       (10)
#define ESP32_TIMER_UNIT_PER_SEC  (1000000)


/*-------------------------------------
 *
 * USB
 *
 *------------------------------------*/

void
Machine_tud_task(void)
{
  /* Not required for ESP32 */
}

bool
Machine_tud_mounted_q(void)
{
  /* Not required for ESP32 */
  return 0;
}


/*-------------------------------------
 *
 * RTC
 *
 *------------------------------------*/

/*
 * deep_sleep doesn't work yet
 */
void
Machine_deep_sleep(uint8_t gpio_pin, bool edge, bool high)
{
}

void
Machine_sleep(uint32_t seconds)
{
  esp_sleep_enable_timer_wakeup(seconds * ESP32_TIMER_UNIT_PER_SEC);
  esp_light_sleep_start();
}

void
Machine_delay_ms(uint32_t ms)
{
  vTaskDelay(ms / ESP32_MSEC_PER_TICK);
}

/*
 * busy_wait_ms doesn't work yet
 */
void
Machine_busy_wait_ms(uint32_t ms)
{
}

bool
Machine_get_unique_id(char *id_str)
{
  uint8_t mac[6];
  efuse_hal_get_mac(mac);
  sprintf(id_str, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

uint32_t
Machine_stack_usage(void)
{
  // Not implemented
  return 0;
}

const char *
Machine_mcu_name(void)
{
  return "ESP32";
}

bool
Machine_set_hwclock(const struct timespec *ts)
{
  // Not implemented
  return false;
}

bool
Machine_get_hwclock(struct timespec *ts)
{
  // Not implemented
  return false;
}

void
Machine_exit(int status)
{
  (void)status; // no-op
}


int Machine_get_config_int(int type)
{
  switch(type)
  {
    case 0:
    return MRB_TICK_UNIT;
    case 1:
    return MRB_TIMESLICE_TICK_COUNT;
  }
  return 0;
}
