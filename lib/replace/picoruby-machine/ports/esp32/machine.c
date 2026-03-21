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
 * mruby: multi-VM tick management via FreeRTOS task
 *
 * Each mrb_state is registered/unregistered dynamically.
 * A dedicated FreeRTOS task delivers mrb_tick() to all active VMs.
 */

#include "fmrb_app.h"

#ifndef MRB_TASK_MAX_VMS
#define MRB_TASK_MAX_VMS 16
#endif

typedef struct {
  mrb_state *mrb;
  int active;
  int in_c_funcall;  /* MRB_C_FUNCALL_EXIT=0, MRB_C_FUNCALL_ENTER=1 */
  int irq;           /* MRB_ENABLE_IRQ=0, MRB_DISABLE_IRQ=1 */
} mrb_vm_entry_t;

static struct {
  mrb_vm_entry_t vms[MRB_TASK_MAX_VMS];
  SemaphoreHandle_t mutex;
  TaskHandle_t tick_task_handle;
  int task_created;
} g_tick_manager = {
  .vms = {{NULL, 0, 0, 0}},
  .mutex = NULL,
  .tick_task_handle = NULL,
  .task_created = 0
};

volatile int sigint_status = 0; /* MACHINE_SIG_NONE */

/* FreeRTOS task: deliver mrb_tick() to all registered VMs periodically */
static void
mruby_tick_task(void *arg)
{
  (void)arg;
  const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

  while (1) {
    vTaskDelay(tick_interval);

    if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
      for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
        if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb) {
          if (g_tick_manager.vms[i].in_c_funcall == MRB_C_FUNCALL_EXIT &&
              g_tick_manager.vms[i].irq == MRB_ENABLE_IRQ) {
            mrb_tick(g_tick_manager.vms[i].mrb);
          }
        }
      }
      xSemaphoreGive(g_tick_manager.mutex);
    }
  }
}

void
machine_hal_init(mrb_state *mrb)
{
  (void)mrb;  /* VM registration is deferred to hal_register_vm() */

  if (!g_tick_manager.task_created) {
    g_tick_manager.mutex = xSemaphoreCreateMutex();
    if (g_tick_manager.mutex == NULL) {
      ESP_LOGE("hal", "Failed to create mutex");
      return;
    }

    BaseType_t ret = xTaskCreate(
      mruby_tick_task,
      "mruby_tick",
      2048,
      NULL,
      5,
      &g_tick_manager.tick_task_handle
    );

    if (ret == pdPASS) {
      g_tick_manager.task_created = 1;
    } else {
      ESP_LOGE("hal", "Failed to create mruby_tick_task");
      vSemaphoreDelete(g_tick_manager.mutex);
      g_tick_manager.mutex = NULL;
      return;
    }
  }
}

void
hal_register_vm(mrb_state *mrb)
{
  if (g_tick_manager.mutex == NULL) return;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (!g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].mrb = mrb;
        g_tick_manager.vms[i].active = 1;
        g_tick_manager.vms[i].in_c_funcall = MRB_C_FUNCALL_EXIT;
        g_tick_manager.vms[i].irq = MRB_ENABLE_IRQ;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
hal_deinit(mrb_state *mrb)
{
  if (g_tick_manager.mutex == NULL) return;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb) {
        g_tick_manager.vms[i].active = 0;
        g_tick_manager.vms[i].mrb = NULL;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
hal_deinit_by_pool(void *pool_ptr, size_t pool_size)
{
  if (g_tick_manager.mutex == NULL) return;

  uint8_t *start = (uint8_t *)pool_ptr;
  uint8_t *end = start + pool_size;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].active) {
        uint8_t *p = (uint8_t *)g_tick_manager.vms[i].mrb;
        if (p >= start && p < end) {
          g_tick_manager.vms[i].active = 0;
          g_tick_manager.vms[i].mrb = NULL;
        }
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
mrb_task_enable_irq(void)
{
  if (g_tick_manager.mutex == NULL) return;
  fmrb_app_task_context_t *ctx = fmrb_current();
  mrb_state *mrb = ctx->mrb;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb && g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].irq = MRB_ENABLE_IRQ;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
mrb_task_disable_irq(void)
{
  if (g_tick_manager.mutex == NULL) return;
  fmrb_app_task_context_t *ctx = fmrb_current();
  mrb_state *mrb = ctx->mrb;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb && g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].irq = MRB_DISABLE_IRQ;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
mrb_set_in_c_funcall(mrb_state *mrb, int flag)
{
  if (g_tick_manager.mutex == NULL) return;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb && g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].in_c_funcall = flag;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
hal_idle_cpu(mrb_state *mrb)
{
  (void)mrb;
  vTaskDelay(1);
}

#endif /* PICORB_VM_MRUBY */

int
hal_write(int fd, const void *buf, int nbytes)
{
  FILE *stream = (fd == 1) ? stdout : stderr;
  for (int i = 0 ; i < nbytes ; i++) {
    fputc(((char*)buf)[i], stream);
  }
  fflush(stream);
  return nbytes;
}

int hal_flush(int fd) {
  FILE *stream = (fd == 1) ? stdout : stderr;
  return fflush(stream);
}

int
hal_read_available(void)
{
  int c = fgetc(stdin);
  return ungetc(c, stdin) == EOF ? 0 : 1;
}

int
hal_getchar(void)
{
  return fgetc(stdin);
}

void
hal_abort(const char *s)
{
  if(s) {
    hal_write(1, s, strlen(s));
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
