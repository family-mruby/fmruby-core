/*
  Original source code from mruby/mrubyc:
    Copyright (C) 2015- Kyushu Institute of Technology.
    Copyright (C) 2015- Shimane IT Open-Innovation Center.
  Modified source code for picoruby/microruby:
    Copyright (C) 2025 HASUMI Hitoshi.

  This file is distributed under BSD 3-Clause License.
*/


#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include "hal.h"

// picoruby-mruby/include/hal.h may define hal_idle_cpu as a macro on POSIX.
// We provide our own implementation, so undef it.
#ifdef hal_idle_cpu
#undef hal_idle_cpu
#endif

// FreeRTOS/ESP-IDF environment (real implementation)
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esp_sleep.h"
#include "esp_log.h"

#include "fmrb_app.h"

#define MAX_MRB_VMS 16  // Maximum number of VMs

typedef struct {
    mrb_state *mrb;
    int active;  // 1=in use, 0=unused
    int in_c_funcall;  // 0=MRB_C_FUNCALL_EXIT  1=MRB_C_FUNCALL_ENTER
    int irq; // 0=MRB_ENABLE_IRQ  1=MRB_DISABLE_IRQ
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

//================================================================
/*!@brief
  mruby tick task (FreeRTOS)

  Executes mrb_tick() for all registered mrb VMs at MRB_TICK_UNIT interval
*/
static void mruby_tick_task(void* arg) {
    (void)arg;
    const TickType_t tick_interval = pdMS_TO_TICKS(MRB_TICK_UNIT);

    ESP_LOGI("hal", "mruby_tick_task started (interval=%dms)", MRB_TICK_UNIT);

    while (1) {
        vTaskDelay(tick_interval);

        // Send tick to all active VMs
        if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < MAX_MRB_VMS; i++) {
                if (g_tick_manager.vms[i].active && g_tick_manager.vms[i].mrb) {
                    // Skip tick if VM is executing C->Ruby funcall
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
picoruby_hal_init(mrb_state *mrb)
{
  (void)mrb;  // VM registration is deferred to hal_register_vm()
  ESP_LOGI("hal", "hal_init called (FreeRTOS mode)");

  // First call only: Create mutex and tick task
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

#else
/*
 * Non-ESP_PLATFORM (picoruby mruby build context)
 *
 * These weak stubs are overridden by the ESP_PLATFORM versions at link time.
 * mrb_task_enable_irq / mrb_task_disable_irq are NOT defined here;
 * they are provided by hal-posix-task (task_hal.c).
 */

__attribute__((weak)) void picoruby_hal_init(mrb_state *mrb) { (void)mrb; }
__attribute__((weak)) void hal_register_vm(mrb_state *mrb) { (void)mrb; }
__attribute__((weak)) void hal_deinit(mrb_state *mrb) { (void)mrb; }
__attribute__((weak)) void hal_deinit_by_pool(void *pool_ptr, size_t pool_size) { (void)pool_ptr; (void)pool_size; }
__attribute__((weak)) void mrb_set_in_c_funcall(mrb_state *mrb, int flag) { (void)mrb; (void)flag; }
__attribute__((weak)) void hal_idle_cpu(mrb_state *mrb) { (void)mrb; usleep(5000); }

#endif


int
hal_write(int fd, const void *buf, int nbytes)
{
  return (int)write(1, buf, nbytes);
}


void
hal_abort(const char *s)
{
  if (s) {
    hal_write(1, s, strlen(s));
  }
  exit(1);
}


/*
 * Prism allocator mutex (used by prism_alloc.c via extern)
 */
#include <pthread.h>
static pthread_mutex_t s_prism_mutex = PTHREAD_MUTEX_INITIALIZER;
void fmrb_prism_lock(void)   { pthread_mutex_lock(&s_prism_mutex); }
void fmrb_prism_unlock(void) { pthread_mutex_unlock(&s_prism_mutex); }
