#include "../../include/hal.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "hal/efuse_hal.h"
#include "rom/ets_sys.h"

#define ESP32_MSEC_PER_TICK       (10)
#define ESP32_TIMER_UNIT_PER_SEC  (1000000)

#ifdef MRBC_NO_TIMER
  #error "MRBC_NO_TIMER is not supported"
#endif

#if defined(PICORB_VM_MRUBYC)

/* mrubyc: single VM, esp_timer based */
static esp_timer_handle_t periodic_timer;
volatile int sigint_status = 0; /* MACHINE_SIG_NONE */

static void
alarm_handler(void *arg)
{
  mrbc_tick();
}

void
machine_hal_init(void)
{
  esp_timer_create_args_t timer_create_args;
  timer_create_args.callback = &alarm_handler;
  timer_create_args.arg = NULL;
  timer_create_args.dispatch_method = ESP_TIMER_TASK;
  timer_create_args.name = "mrbc_tick_timer";

  esp_timer_create(&timer_create_args, &periodic_timer);
  esp_timer_start_periodic(periodic_timer, MRBC_TICK_UNIT * 1000);
}

void hal_enable_irq(void) { portENABLE_INTERRUPTS(); }
void hal_disable_irq(void) { portDISABLE_INTERRUPTS(); }
void hal_idle_cpu(void) { vTaskDelay(1); }

#elif defined(PICORB_VM_MRUBY)

/*
 * mruby: the case-D tick manager (top-half timer task, per-VM pending-tick
 * registry) and the whole mruby-task HAL contract (mrb_hal_task_init/final,
 * mrb_hal_task_register_vm, mrb_hal_task_take_pending_ticks, idle_cpu,
 * sleep_us, mrb_task_enable/disable_irq) live in
 * mruby-task/ports/freertos/task_hal.c, compiled by the picoruby-esp32
 * CMake component (PICORUBY_SRCS). This file keeps only machine duties.
 */

volatile int sigint_status = 0; /* MACHINE_SIG_NONE */

#endif /* PICORB_VM_MRUBY */

int
picorb_hal_write(int fd, const void *buf, int nbytes)
{
  FILE *stream = (fd == 1) ? stdout : stderr;
  for (int i = 0 ; i < nbytes ; i++) {
    fputc(((char*)buf)[i], stream);
  }
  fflush(stream);
  return nbytes;
}

int picorb_hal_flush(int fd) {
  FILE *stream = (fd == 1) ? stdout : stderr;
  return fflush(stream);
}

int
picorb_hal_read_available(void)
{
  int c = fgetc(stdin);
  return ungetc(c, stdin) == EOF ? 0 : 1;
}

int
picorb_hal_getchar(void)
{
  return fgetc(stdin);
}

void
picorb_hal_abort(const char *s)
{
  if(s) {
    picorb_hal_write(1, s, strlen(s));
  }

  abort();
}


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
  ets_delay_us(1000 * ms);
}

void
Machine_busy_wait_us(uint32_t us)
{
  ets_delay_us(us);
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

bool
Machine_set_hwclock(const struct timespec *ts)
{
  clock_settime(CLOCK_REALTIME, ts);
  return true;
}

bool
Machine_get_hwclock(struct timespec *ts)
{
  clock_gettime(CLOCK_REALTIME, ts);
  return true;
}

void
Machine_exit(int status)
{
  (void)status; // no-op
}

uint64_t
Machine_uptime_us(void)
{
  return (uint64_t)esp_timer_get_time();
}

/* family-mruby: config query for boot diagnostics */
int
Machine_get_config_int(int type)
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
