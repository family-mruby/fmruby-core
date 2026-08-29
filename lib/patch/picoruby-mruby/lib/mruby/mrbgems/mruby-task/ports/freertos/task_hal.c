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
**
** MRB_TASK_TICK_SELF_SUPPLY (off by default) replaces the top-half with the VM
** supplying its own ticks:
**
**   - There is no mruby_tick task. Instead mrb_hal_task_self_tick(), called from
**     the VM's own dispatch loop (vm.c, RETURN_IF_TASK_STOPPED), reads the wall
**     clock and accumulates however many MRB_TICK_UNIT periods have gone by.
**     Everything downstream -- the pending counter, the timeslice countdown, the
**     switching flag -- works exactly as it does with the top-half, because both
**     halves go through the same vm_apply_ticks().
**
**   - The reason is the WebAssembly target (doc/wasm/), whose FreeRTOS port is
**     cooperative: a task that neither blocks nor yields keeps the CPU, so a
**     separate timer task can never run to pry a CPU-bound Ruby program loose.
**     A VM that ticks itself does not need anyone else to be scheduled.
**
**   - The clock is fmrb_hal_time_get_us(), deliberately not xTaskGetTickCount():
**     on a cooperative port the FreeRTOS tick does not advance while a task
**     spins, which is the whole problem being solved.
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

#ifdef MRB_TASK_TICK_SELF_SUPPLY
void mrb_hal_task_self_tick(mrb_state *mrb);
void mrb_hal_task_account_ticks(mrb_state *mrb);
/* Wall clock for the self-supplied tick. Reachable because this file is
 * compiled by the CMake side (components/picoruby-esp32/CMakeLists.txt), which
 * PRIV_REQUIREs fmrb_hal -- not by the rake mruby build, which has neither this
 * header nor the FreeRTOS ones. */
#include "fmrb_hal_time.h"
#endif

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
  int           switch_pending; /* a timeslice elapsed; hand it to the VM at a bytecode boundary */
#ifdef MRB_TASK_TICK_SELF_SUPPLY
  uint64_t      last_self_us;    /* wall clock the last self-supplied tick was charged at */
#endif
} mrb_vm_entry_t;

static struct {
  mrb_vm_entry_t   vms[MRB_TASK_MAX_VMS];
  SemaphoreHandle_t mutex;
  TaskHandle_t     tick_task_handle;
  int              task_created;
} g_tick_manager;

/* Charge `ticks` ticks to one VM: accumulate them for the bottom-half to apply,
 * and raise a preemption request once a whole timeslice has gone by. Both tick
 * sources go through here, so the two modes cannot drift apart in cadence.
 * Raising is separate from delivering (vm_deliver_switch below), because the
 * two modes learn that a timeslice is over at different moments.
 *
 * Caller holds g_tick_manager.mutex.
 */
static void
vm_apply_ticks(mrb_vm_entry_t *vm, uint32_t ticks)
{
  vm->pending_ticks += ticks;

  while (ticks--) {
    if (vm->tick_countdown > 0) vm->tick_countdown--;
    if (vm->tick_countdown == 0) {
      vm->switch_pending = 1;
      vm->tick_countdown = MRB_TIMESLICE_TICK_COUNT;
    }
  }
}

/* Hand a raised request to the VM. Only ever called with the VM's own task
 * running, because that is the only time mrb->task.switching survives:
 * execute_task() (task.c) clears it every time it gives a task the CPU. The
 * top-half meets that condition by writing from another thread while the task
 * runs; the self-supplied path meets it by writing from inside the dispatch
 * loop. Writing it from the scheduler instead looks like it works and does
 * nothing at all -- which cost a woken task a whole extra turn of the ready
 * queue (doc/wasm/report/p2.md).
 *
 * Caller holds g_tick_manager.mutex.
 */
static void
vm_deliver_switch(mrb_vm_entry_t *vm)
{
  if (vm->switch_pending) {
    vm->switch_pending = 0;
    vm->mrb->task.switching = TRUE;
  }
}

#ifndef MRB_TASK_TICK_SELF_SUPPLY
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
          vm_apply_ticks(vm, 1);
          vm_deliver_switch(vm);
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
#endif /* !MRB_TASK_TICK_SELF_SUPPLY */

#ifdef MRB_TASK_TICK_SELF_SUPPLY
/* ---- self-supplied tick ----------------------------------------------------
 * Charges the VM for however many whole MRB_TICK_UNIT periods have elapsed
 * since it was last charged. Carrying the remainder forward (rather than
 * resetting to now) keeps the cadence tied to the wall clock, so a long stretch
 * between calls is paid back rather than lost.
 *
 * Nothing is notified: the only task that would be woken is this one.
 */
static void
self_tick(mrb_state *mrb, int deliver)
{
  const uint64_t period_us = (uint64_t)MRB_TICK_UNIT * 1000ULL;
  uint64_t now;

  if (g_tick_manager.mutex == NULL) return;

  now = fmrb_hal_time_get_us();

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MRB_TASK_MAX_VMS; i++) {
      mrb_vm_entry_t *vm = &g_tick_manager.vms[i];
      if (vm->active && vm->mrb == mrb) {
        uint64_t elapsed = now - vm->last_self_us;
        if (elapsed >= period_us) {
          uint32_t ticks = (uint32_t)(elapsed / period_us);
          vm->last_self_us += (uint64_t)ticks * period_us;
          vm_apply_ticks(vm, ticks);
        }
        if (deliver) vm_deliver_switch(vm);
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

/* Called from the VM's own dispatch loop, every MRB_TASK_SELF_TICK_INTERVAL
 * bytecodes (vm.c, the patch marked "fmrb: self-supplied timeslice"). A task is
 * running, so this is the one place a preemption can be asked for -- it is what
 * the top-half's asynchronous write to switching used to do. */
void
mrb_hal_task_self_tick(mrb_state *mrb)
{
  self_tick(mrb, 1);
}

/* Called from the scheduler itself (task.c task_run_body's loop head, and the
 * idle wait below), where no task is running.
 *
 * Keeps the clock and the timeslice countdown moving -- which is what the app
 * main loop needs, since it spends nearly all its time blocked inside _spin and
 * executes too few bytecodes for the dispatch-loop hook to reach its interval --
 * but leaves any raised request for the VM side to deliver. */
void
mrb_hal_task_account_ticks(mrb_state *mrb)
{
  self_tick(mrb, 0);
}
#endif /* MRB_TASK_TICK_SELF_SUPPLY */

/* ---- registry -------------------------------------------------------------- */

static void
tick_manager_ensure_started(void)
{
  if (g_tick_manager.task_created) return;

  g_tick_manager.mutex = xSemaphoreCreateMutex();
  if (g_tick_manager.mutex == NULL) return;

#ifdef MRB_TASK_TICK_SELF_SUPPLY
  /* No top-half: each VM supplies its own ticks from its dispatch loop. The
   * mutex above is still needed -- the pending counter is read-modify-written
   * by the VM thread and, with several VMs, by several of them. */
  g_tick_manager.task_created = 1;
#else
  BaseType_t ret = xTaskCreate(mruby_tick_task, "mruby_tick", 2048, NULL, 5,
                               &g_tick_manager.tick_task_handle);
  if (ret == pdPASS) {
    g_tick_manager.task_created = 1;
  } else {
    vSemaphoreDelete(g_tick_manager.mutex);
    g_tick_manager.mutex = NULL;
  }
#endif
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
#ifdef MRB_TASK_TICK_SELF_SUPPLY
        /* Start the clock now, so the VM is not charged for the time before it
         * existed. */
        g_tick_manager.vms[slot].last_self_us = fmrb_hal_time_get_us();
#endif
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

#ifdef MRB_TASK_TICK_SELF_SUPPLY
  /* Nobody notifies us in this mode, and -- more to the point -- nobody ticks
   * us either: the ticks come from the dispatch loop, and an idle VM is running
   * no bytecode. Without this, a VM whose tasks are all sleeping would never
   * advance its own clock and would never wake any of them. The scheduler
   * drains the ticks at the head of its next pass (task.c, task_run_body). */
  mrb_hal_task_account_ticks(mrb);
#endif
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
