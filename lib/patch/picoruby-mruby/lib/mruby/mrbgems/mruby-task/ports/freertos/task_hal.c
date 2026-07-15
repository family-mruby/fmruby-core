/*
** task_hal.c - FreeRTOS HAL for mruby-task (family-mruby, case-D tick split)
**
** Shared by the ESP32 firmware and the Linux dev build (POSIX FreeRTOS
** simulator): FreeRTOS API only, no ESP-IDF-specific calls (esp_timer, ESP_LOG).
**
** Why a task and not a timer interrupt (see doc/work_picoruby_merge/
** instruct_d7_b1_tick.md): upstream mruby-task only requires that mrb_tick and
** the VM's task-queue operations are never truly concurrent. On a single-core
** MCU an ISR/SIGALRM satisfies that by running on the VM's own execution flow.
** On fmrb (RTOS, dual-core) a foreign task calling mrb_tick would physically
** race the VM's queue writes and corrupt ci->proc. Case-D restores the
** invariant more strongly by confining every queue mutation to the VM's own
** thread:
**   - top-half  (mruby_tick_task, this file, a separate FreeRTOS task):
**       only sets mrb->task.switching and accumulates a per-VM pending count.
**       Never calls mrb_tick.
**   - bottom-half (task.c task_run_body, the VM's own thread):
**       drains the pending count via mrb_hal_task_take_pending_ticks and applies
**       mrb_tick there, where queue access is single-threaded.
**
** The only cross-thread shared state is mrb->task.switching (a one-way volatile
** word) and the per-VM pending counter. The pending counter is a genuine
** read-modify-write shared between the timer task (increment) and the VM thread
** (read+zero), so BOTH sides take g_tick_manager.mutex around it. This mutex IS
** the atomicity required by the review; do not remove it. (On the single-thread
** Linux simulator the race cannot be observed; it only bites on real dual-core
** hardware, so the guard is verified by code review, not by the Linux tests.)
*/

#include <mruby.h>          /* mrb_state, mrb->task.switching (mruby.h:293), TRUE */
#include "task_hal.h"       /* the HAL contract this file implements */

/* FreeRTOS headers use the <freertos/...> prefix (fmrb convention on both
 * ESP-IDF and the Linux POSIX simulator), which also disambiguates from
 * mruby-task's own "task.h". We do not include mruby-task/task.h: the only
 * symbol we need from the task subsystem is mrb->task.switching, declared in
 * mruby.h. */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifndef MRB_TICK_UNIT
#define MRB_TICK_UNIT 4
#endif
#ifndef MRB_TIMESLICE_TICK_COUNT
#define MRB_TIMESLICE_TICK_COUNT 3
#endif

typedef struct {
  mrb_state    *mrb;
  int           active;
  uint32_t      pending_ticks;   /* accumulated by the timer task, drained by the VM thread */
  uint32_t      tick_countdown;  /* ticks until the next preemption request (timeslice cadence) */
  TaskHandle_t  vm_task;         /* the VM's own task, notified on each tick to end idle waits */
} mrb_vm_entry_t;

static struct {
  mrb_vm_entry_t   vms[MRB_TASK_MAX_VMS];
  SemaphoreHandle_t mutex;
  TaskHandle_t     tick_task_handle;
  int              task_created;
} g_tick_manager;

/* ---- top-half: FreeRTOS timer task ----------------------------------------
 * Runs on its own thread. Accumulates ticks and requests a switch; never calls
 * mrb_tick(). Notifies each VM task so an idle scheduler wakes immediately.
 */
static void
mruby_tick_task(void *arg)
{
  (void)arg;
  const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

  while (1) {
    vTaskDelay(tick_interval);

    if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
      for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
        mrb_vm_entry_t *vm = &g_tick_manager.vms[i];
        if (vm->active && vm->mrb) {
          vm->pending_ticks++;
          /* Preempt CPU-bound tasks at timeslice cadence. Setting switching is
           * safe asynchronously: the VM honors it only at a top-level bytecode
           * boundary (upstream RETURN_IF_TASK_STOPPED / task_across_c_boundary),
           * never inside a nested C call. */
          if (vm->tick_countdown > 0) vm->tick_countdown--;
          if (vm->tick_countdown == 0) {
            vm->mrb->task.switching = TRUE;
            vm->tick_countdown = MRB_TIMESLICE_TICK_COUNT;
          }
          /* Wake the VM if it is idle-waiting in mrb_hal_task_idle_cpu. */
          if (vm->vm_task) {
            xTaskNotifyGive(vm->vm_task);
          }
        }
      }
      xSemaphoreGive(g_tick_manager.mutex);
    }
  }
}

/* ---- registry -------------------------------------------------------------- */

static void
tick_manager_ensure_started(void)
{
  if (g_tick_manager.task_created) return;

  g_tick_manager.mutex = xSemaphoreCreateMutex();
  if (g_tick_manager.mutex == NULL) return;

  BaseType_t ret = xTaskCreate(mruby_tick_task, "mruby_tick", 2048, NULL, 5,
                               &g_tick_manager.tick_task_handle);
  if (ret == pdPASS) {
    g_tick_manager.task_created = 1;
  } else {
    vSemaphoreDelete(g_tick_manager.mutex);
    g_tick_manager.mutex = NULL;
  }
}

/* Register the calling VM. Idempotent: re-registering an already-known mrb just
 * refreshes its VM task handle (the scheduler may run on a different task than
 * the one that first opened the VM). */
void
mrb_hal_task_register_vm(mrb_state *mrb)
{
  if (g_tick_manager.mutex == NULL) return;
  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    int slot = -1;
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb == mrb) { slot = i; break; }
    }
    if (slot < 0) {
      for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
        if (!g_tick_manager.vms[i].active) { slot = i; break; }
      }
      if (slot >= 0) {
        g_tick_manager.vms[slot].mrb = mrb;
        g_tick_manager.vms[slot].active = 1;
        g_tick_manager.vms[slot].pending_ticks = 0;
        g_tick_manager.vms[slot].tick_countdown = MRB_TIMESLICE_TICK_COUNT;
      }
    }
    if (slot >= 0) {
      g_tick_manager.vms[slot].vm_task = xTaskGetCurrentTaskHandle();
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

static void
tick_manager_unregister(mrb_state *mrb)
{
  if (g_tick_manager.mutex == NULL) return;
  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb) {
        g_tick_manager.vms[i].active = 0;
        g_tick_manager.vms[i].mrb = NULL;
        g_tick_manager.vms[i].vm_task = NULL;
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

/* ---- bottom-half source: drain pending ticks ------------------------------- */

uint32_t
mrb_hal_task_take_pending_ticks(mrb_state *mrb)
{
  uint32_t n = 0;
  if (g_tick_manager.mutex == NULL) return 0;
  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
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

/* ---- mruby-task HAL contract ----------------------------------------------- */

void
mrb_hal_task_init(mrb_state *mrb)
{
  tick_manager_ensure_started();
  mrb_hal_task_register_vm(mrb);
}

void
mrb_hal_task_final(mrb_state *mrb)
{
  tick_manager_unregister(mrb);
}

/* Case-D confines all queue mutation to the VM thread, so the scheduler needs
 * no IRQ/lock guard. These satisfy the HAL contract as no-ops. */
void mrb_task_enable_irq(void)  {}
void mrb_task_disable_irq(void) {}

/* Idle: block until the timer task notifies (tick arrived) or the tick interval
 * elapses, so a woken sleeping task is picked up with near-zero latency without
 * busy-waiting. */
void
mrb_hal_task_idle_cpu(mrb_state *mrb)
{
  (void)mrb;
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MRB_TICK_UNIT));
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
