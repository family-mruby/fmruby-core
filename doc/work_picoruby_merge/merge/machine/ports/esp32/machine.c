#include "../../include/hal.h"
#include "../../include/machine.h"
#include "../../include/ringbuffer.h"
#include "../../../picoruby-io-console/include/io-console.h"

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

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "hal/usb_serial_jtag_ll.h"
#include "esp_intr_alloc.h"
#include "soc/periph_defs.h"
#endif

#define ESP32_MSEC_PER_TICK       (10)
#define ESP32_TIMER_UNIT_PER_SEC  (1000000)

#ifdef MRBC_NO_TIMER
  #error "MRBC_NO_TIMER is not supported"
#endif

#if defined(PICORB_VM_MRUBYC)

/* mrubyc: single VM, esp_timer based */
static esp_timer_handle_t periodic_timer;
volatile int sigint_status = 0; /* MACHINE_SIG_NONE */

/*-------------------------------------
 *
 * stdin RingBuffer
 *
 *------------------------------------*/

#ifndef PICORB_STDIN_BUFFER_SIZE
#define PICORB_STDIN_BUFFER_SIZE 1024
#endif

static uint8_t stdin_buf_mem[sizeof(RingBuffer) + PICORB_STDIN_BUFFER_SIZE]
  __attribute__((aligned(4)));
static RingBuffer *stdin_rb = (RingBuffer *)stdin_buf_mem;

bool
picorb_hal_stdin_push(uint8_t ch)
{
  if (!io_raw_q()) {
    if (ch == 3) {
      sigint_status = MACHINE_SIGINT_RECEIVED;
      return true;
    }
    if (ch == 26) {
      sigint_status = MACHINE_SIGTSTP_RECEIVED;
      return true;
    }
  }
  return RingBuffer_push(stdin_rb, ch);
}

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG

static TaskHandle_t stdin_task_handle = NULL;

static void IRAM_ATTR
usb_serial_jtag_rx_isr(void *arg)
{
  (void)arg;
  usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(stdin_task_handle, &woken);
  portYIELD_FROM_ISR(woken);
}

static void
stdin_reader_task(void *arg)
{
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    uint8_t buf[64];
    uint32_t len = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
    for (uint32_t i = 0; i < len; i++) {
      if (!picorb_hal_stdin_push(buf[i])) break;
    }
  }
}

#else /* !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG */

static void
stdin_reader_task(void *arg)
{
  (void)arg;
  for (;;) {
    int c;
    while ((c = fgetc(stdin)) != EOF) {
      if (!picorb_hal_stdin_push((uint8_t)c)) break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#endif /* CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG */

static void
alarm_handler(void *arg)
{
<<<<<<< ours
  mrbc_tick();
}

void
machine_hal_init(void)
=======
#if defined(PICORB_VM_MRUBYC)
  picorb_tick();
#else
  picorb_tick(mrb_);
#endif
}

void
#if defined(PICORB_VM_MRUBY)
picorb_hal_init(mrb_state *mrb)
#elif defined(PICORB_VM_MRUBYC)
picorb_hal_init(void)
#endif
>>>>>>> upstream
{
  esp_timer_create_args_t timer_create_args;
  timer_create_args.callback = &alarm_handler;
  timer_create_args.arg = NULL;
  timer_create_args.dispatch_method = ESP_TIMER_TASK;
  timer_create_args.name = "mrbc_tick_timer";

  esp_timer_create(&timer_create_args, &periodic_timer);
  esp_timer_start_periodic(periodic_timer, MRBC_TICK_UNIT * 1000);
<<<<<<< ours
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
#include "fmrb_task_config.h"

typedef struct {
  mrb_state *mrb;
  int active;
  uint32_t pending_ticks;   /* 案D: ticks accumulated by signal source, drained by VM thread */
  uint32_t tick_countdown;  /* 案D: ticks until next preemption signal (timeslice cadence) */
} mrb_vm_entry_t;

static struct {
  mrb_vm_entry_t vms[FMRB_MRB_MAX_VMS];
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

/*
 * FreeRTOS tick signal source (案D top-half).
 *
 * This task runs on a thread other than the VMs. It must therefore NOT call
 * mrb_tick() (which mutates the per-VM task queues) - doing so races the VM's
 * own thread and corrupts the task context (see doc/known_issue). Instead it
 * only: (1) accumulates a pending-tick count, and (2) sets mrb->task.switching
 * so a running (possibly CPU-bound) task yields at the next bytecode boundary.
 * The real mrb_tick() is applied by the VM itself in mrb_task_run() via
 * mrb_hal_task_take_pending_ticks().
 */
static void
mruby_tick_task(void *arg)
=======
#endif

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  Machine_delay_ms(3000);
#endif

  RingBuffer_init(stdin_rb, PICORB_STDIN_BUFFER_SIZE);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  {
    TaskHandle_t h;
    xTaskCreate(stdin_reader_task, "stdin_reader", 2048, NULL, tskIDLE_PRIORITY + 2, &h);
    stdin_task_handle = h;
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, ESP_INTR_FLAG_IRAM,
                   usb_serial_jtag_rx_isr, NULL, NULL);
  }
#else
  xTaskCreate(stdin_reader_task, "stdin_reader", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif
}

#if defined(PICORB_VM_MRUBY)
void
picorb_hal_final(mrb_state *mrb)
{
  (void)mrb;
}
#endif

void
picorb_hal_enable_irq(void)
>>>>>>> upstream
{
  (void)arg;
  const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

  while (1) {
    vTaskDelay(tick_interval);

    if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
      for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
        mrb_vm_entry_t *vm = &g_tick_manager.vms[i];
        if (vm->active && vm->mrb) {
          /* Time tracking: ticks are applied on the VM's own thread in
           * mrb_task_run (bottom-half), never from this thread. */
          vm->pending_ticks++;
          /* Preempt CPU-bound tasks at timeslice cadence. Requesting the switch
           * asynchronously is safe: the VM acts on switching only at a top-level
           * bytecode boundary (mrb_task_yield_ok in vm.c), never inside a nested
           * C call, so the pending switch is deferred to the next safe point. */
          if (vm->tick_countdown > 0) vm->tick_countdown--;
          if (vm->tick_countdown == 0) {
            mrb_task_request_switch(vm->mrb);
            vm->tick_countdown = MRB_TIMESLICE_TICK_COUNT;
          }
        }
      }
      xSemaphoreGive(g_tick_manager.mutex);
    }
  }
}

/*
 * Return and zero the pending-tick count for this VM (案D bottom-half source).
 * Called from mrb_task_run() on the VM's own thread.
 */
uint32_t
mrb_hal_task_take_pending_ticks(mrb_state *mrb)
{
  uint32_t n = 0;
  if (g_tick_manager.mutex == NULL) return 0;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb && g_tick_manager.vms[i].active) {
        n = g_tick_manager.vms[i].pending_ticks;
        g_tick_manager.vms[i].pending_ticks = 0;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
  return n;
}

void
<<<<<<< ours
machine_hal_init(mrb_state *mrb)
{
  (void)mrb;  /* VM registration is deferred to hal_register_vm() */

  ESP_LOGI("hal", "machine_hal_init called");

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
      ESP_LOGI("hal", "mruby_tick_task created (interval=%dms)", MRB_TICK_UNIT);
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
    for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
      if (!g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].mrb = mrb;
        g_tick_manager.vms[i].active = 1;
        g_tick_manager.vms[i].pending_ticks = 0;
        g_tick_manager.vms[i].tick_countdown = MRB_TIMESLICE_TICK_COUNT;
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
    for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
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
    for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
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

/*
 * 案D: the task queues are now mutated only by each VM's own thread (the signal
 * source no longer calls mrb_tick), so the scheduler needs no IRQ/lock guard.
 * These remain as no-ops to satisfy the mruby-task HAL contract.
 */
void
mrb_task_enable_irq(void)
{
}

void
mrb_task_disable_irq(void)
=======
picorb_hal_disable_irq(void)
>>>>>>> upstream
{
}

void
<<<<<<< ours
hal_idle_cpu(mrb_state *mrb)
=======
#if defined(PICORB_VM_MRUBYC)
picorb_hal_idle_cpu()
#elif defined(PICORB_VM_MRUBY)
picorb_hal_idle_cpu(mrb_state *mrb)
#endif
>>>>>>> upstream
{
  (void)mrb;
  vTaskDelay(1);
}

<<<<<<< ours
/*
 * Task HAL interface (required by mruby-task gem)
 * On ESP32, tick delivery is handled by mruby_tick_task above.
 */
void
mrb_hal_task_init(mrb_state *mrb)
{
  /* 案D: re-enabled. The tick task is now a signal-only source (sets
   * switching + accumulates pending ticks); it no longer calls mrb_tick from
   * a foreign thread, so the VM-corruption race is gone. Idempotent. */
  machine_hal_init(mrb);
}

void
mrb_hal_task_final(mrb_state *mrb)
{
  hal_deinit(mrb);
}

void
mrb_hal_task_idle_cpu(mrb_state *mrb)
{
  (void)mrb;
  vTaskDelay(1);
}

void
mrb_hal_task_sleep_us(mrb_state *mrb, mrb_int usec)
{
  (void)mrb;
  if (usec <= 0) return;
  TickType_t ticks = pdMS_TO_TICKS(usec / 1000);
  if (ticks < 1) ticks = 1;
  vTaskDelay(ticks);
}

#endif /* PICORB_VM_MRUBY */

/*
 * Prism allocator mutex (used by prism_alloc.c via extern)
 */
static SemaphoreHandle_t s_prism_mutex = NULL;
void fmrb_prism_lock(void)
{
  if (s_prism_mutex == NULL) s_prism_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_prism_mutex, portMAX_DELAY);
}
void fmrb_prism_unlock(void)
{
  if (s_prism_mutex != NULL) xSemaphoreGive(s_prism_mutex);
}
=======
#if defined(PICORB_VM_MRUBY)
void
picorb_hal_sleep_us(mrb_state *mrb, mrb_int usec)
{
  (void)mrb;
  ets_delay_us((uint32_t)usec);
}
#endif
>>>>>>> upstream

int
picorb_hal_write(int fd, const void *buf, int nbytes)
{
  FILE *stream = (fd == 1) ? stdout : stderr;
  for (int i = 0 ; i < nbytes ; i++) {
    fputc(((char*)buf)[i], stream);
  }
  fflush(stream);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  usb_serial_jtag_ll_txfifo_flush();
#endif
  return nbytes;
}

int picorb_hal_flush(int fd) {
  FILE *stream = (fd == 1) ? stdout : stderr;
  int ret = fflush(stream);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  usb_serial_jtag_ll_txfifo_flush();
#endif
  return ret;
}


int
picorb_hal_read_available(void)
{
  return (RingBuffer_data_size(stdin_rb) > 0) ? 1 : 0;
}

int
picorb_hal_getchar(void)
{
  if (sigint_status == MACHINE_SIGINT_RECEIVED) {
    sigint_status = MACHINE_SIG_NONE;
    return 3;
  }
  if (sigint_status == MACHINE_SIGTSTP_RECEIVED) {
    sigint_status = MACHINE_SIG_NONE;
    return 26;
  }

  uint8_t ch;
  if (RingBuffer_pop(stdin_rb, &ch)) {
    return (int)ch;
  }
  return HAL_GETCHAR_NODATA;
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
