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

#define MAX_MRB_VMS 16

typedef struct {
    mrb_state *mrb;
    int active;
    int in_c_funcall;
    int irq;
} mrb_vm_entry_t;

static struct {
    mrb_vm_entry_t vms[MAX_MRB_VMS];
    SemaphoreHandle_t mutex;
    TaskHandle_t tick_task_handle;
    int task_created;
} g_tick_manager = {
    .vms = {{NULL, 0}},
    .mutex = NULL,
    .tick_task_handle = NULL,
    .task_created = 0
};

static void mruby_tick_task(void* arg) {
    (void)arg;
    const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

    ESP_LOGI("hal", "mruby_tick_task started (interval=%dms)", MRB_TICK_UNIT);

    while (1) {
        vTaskDelay(tick_interval);

        if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < MAX_MRB_VMS; i++) {
                if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb) {
                    if (MRB_C_FUNCALL_EXIT == g_tick_manager.vms[i].in_c_funcall && MRB_ENABLE_IRQ == g_tick_manager.vms[i].irq) {
                        mrb_tick(g_tick_manager.vms[i].mrb);
                    }
                }
            }
            xSemaphoreGive(g_tick_manager.mutex);
        }
    }
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
    for (int i = 0; i < MAX_MRB_VMS; i++) {
      if (!g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].mrb = mrb;
        g_tick_manager.vms[i].active = 1;
        g_tick_manager.vms[i].in_c_funcall = MRB_C_FUNCALL_EXIT;
        g_tick_manager.vms[i].irq = MRB_ENABLE_IRQ;
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

void
mrb_task_enable_irq(void)
{
  if (g_tick_manager.mutex == NULL) return;
  fmrb_app_task_context_t* ctx = fmrb_current();
  mrb_state* mrb = ctx->mrb;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MAX_MRB_VMS; i++) {
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
  fmrb_app_task_context_t* ctx = fmrb_current();
  mrb_state* mrb = ctx->mrb;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MAX_MRB_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb && g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].irq = MRB_DISABLE_IRQ;
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
    for (int i = 0; i < MAX_MRB_VMS; i++) {
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
    for (int i = 0; i < MAX_MRB_VMS; i++) {
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
mrb_set_in_c_funcall(mrb_state *mrb, int flag)
{
  if (g_tick_manager.mutex == NULL) return;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MAX_MRB_VMS; i++) {
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
  taskYIELD();
}
