/*
 * hal_freertos.c - FreeRTOS-based HAL implementation for Family mruby
 *
 * This file is compiled ONLY by the ESP-IDF build (via CMakeLists.txt).
 * It overrides the stub functions in hal.c (compiled by picoruby mruby build).
 *
 * Provides multi-VM tick management using a dedicated FreeRTOS task
 * instead of SIGALRM (which conflicts with FreeRTOS scheduler on Linux).
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal.h"

#ifdef hal_idle_cpu
#undef hal_idle_cpu
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esp_log.h"

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
    .vms = {{NULL, 0}},
    .mutex = NULL,
    .tick_task_handle = NULL,
    .task_created = 0
};

/*
 * 案D top-half: signal source only. Must NOT call mrb_tick() from this thread
 * (would race the VM thread on the task queues). It only accumulates a pending
 * tick count and sets mrb->task.switching so a running task yields. The real
 * mrb_tick() runs on the VM's own thread in mrb_task_run() via
 * mrb_hal_task_take_pending_ticks(). See doc/known_issue.
 */
static void mruby_tick_task(void* arg) {
    (void)arg;
    const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

    ESP_LOGI("hal", "mruby_tick_task started (interval=%dms)", MRB_TICK_UNIT);

    while (1) {
        vTaskDelay(tick_interval);

        if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
                mrb_vm_entry_t *vm = &g_tick_manager.vms[i];
                if (vm->active && vm->mrb) {
                    /* Time tracking: applied on the VM's own thread in
                     * mrb_task_run (bottom-half). */
                    vm->pending_ticks++;
                    /* Preempt at timeslice cadence. Safe because the VM acts on
                     * switching only at a top-level bytecode boundary
                     * (mrb_task_yield_ok in vm.c), never inside a nested C call. */
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
hal_init(mrb_state *mrb)
{
  (void)mrb;
  ESP_LOGI("hal", "hal_init called (FreeRTOS mode)");

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
      ESP_LOGI("hal", "mruby_tick_task created");
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
    int added = 0;
    for (int i = 0; i < FMRB_MRB_MAX_VMS; i++) {
      if (!g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].mrb = mrb;
        g_tick_manager.vms[i].active = 1;
        g_tick_manager.vms[i].pending_ticks = 0;
        g_tick_manager.vms[i].tick_countdown = MRB_TIMESLICE_TICK_COUNT;
        ESP_LOGI("hal", "mrb VM registered at slot %d (mrb=%p)", i, mrb);
        added = 1;
        break;
      }
    }
    if (!added) {
      ESP_LOGE("hal", "Failed to register mrb VM: list full");
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

/*
 * 案D: queues are mutated only by each VM's own thread now, so no IRQ/lock
 * guard is needed. Kept as no-ops to satisfy the mruby-task HAL contract.
 */
void
mrb_task_enable_irq(void)
{
}

void
mrb_task_disable_irq(void)
{
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
        ESP_LOGI("hal", "mrb VM unregistered from slot %d", i);
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
hal_deinit_by_pool(void* pool_ptr, size_t pool_size)
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
          ESP_LOGI("hal", "mrb VM unregistered from slot %d (pool cleanup)", i);
        }
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

void
hal_idle_cpu(mrb_state *mrb)
{
  (void)mrb;
  taskYIELD();
}
