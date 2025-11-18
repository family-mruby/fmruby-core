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

// FreeRTOS/ESP-IDF環境でのみインクルード（mrbcビルドを除外）
#ifndef PICORUBY_HOST_BUILD
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
                    }else{
                        //printf("skip tick\n");
                    }
                }
            }
            xSemaphoreGive(g_tick_manager.mutex);
        }
    }
}
#endif

/***** Global functions *****************************************************/

//================================================================
/*!@brief
  initialize

*/
void
hal_init(mrb_state *mrb)
{
  // FreeRTOS environment: Multitask-based tick management
#ifndef PICORUBY_HOST_BUILD
  ESP_LOGI("hal", "hal_init called (FreeRTOS mode)");

  // First call only: Create mutex and tick task
  if (!g_tick_manager.task_created) {
    // Create mutex
    g_tick_manager.mutex = xSemaphoreCreateMutex();
    if (g_tick_manager.mutex == NULL) {
      ESP_LOGE("hal", "Failed to create mutex");
      return;
    }

    // Create tick task
    BaseType_t ret = xTaskCreate(
        mruby_tick_task,                      // Task function
        "mruby_tick",                         // Task name
        2048,                                 // Stack size
        NULL,                                 // Parameter
        5,                                    // Priority (medium)
        &g_tick_manager.tick_task_handle      // Task handle
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

  // Add mrb to list
  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    int added = 0;
    for (int i = 0; i < MAX_MRB_VMS; i++) {
      if (!g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].mrb = mrb;
        g_tick_manager.vms[i].active = 1;
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

#else
  // mrbc build: POSIX sleep only (existing implementation)
  // No tick management needed
#endif
}


//================================================================
/*!@brief
  enable interrupt

*/
void
mrb_task_enable_irq(void)
{
#ifndef PICORUBY_HOST_BUILD
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
#endif
}


//================================================================
/*!@brief
  disable interrupt

*/
void
mrb_task_disable_irq(void)
{
#ifndef PICORUBY_HOST_BUILD
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
#endif
}


#ifndef PICORUBY_HOST_BUILD
//================================================================
/*!@brief
  deinitialize (FreeRTOS environment only)

  Remove mrb from VM list
*/
void
hal_deinit(mrb_state *mrb)
{
  if (g_tick_manager.mutex == NULL) return;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    // Remove mrb from list
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

//================================================================
/*!@brief
  Set in_c_funcall flag for mrb VM

  @param mrb    mruby state
  @param flag   1=in C->Ruby funcall (skip tick), 0=normal
*/
void
mrb_set_in_c_funcall(mrb_state *mrb, int flag)
{
  if (g_tick_manager.mutex == NULL) return;

  if (xSemaphoreTake(g_tick_manager.mutex, portMAX_DELAY) == pdTRUE) {
    for (int i = 0; i < MAX_MRB_VMS; i++) {
      if (g_tick_manager.vms[i].mrb == mrb && g_tick_manager.vms[i].active) {
        g_tick_manager.vms[i].in_c_funcall = flag;
        //ESP_LOGI("hal", "mrb VM slot %d: in_c_funcall=%d", i, flag);
        break;
      }
    }
    xSemaphoreGive(g_tick_manager.mutex);
  }
}

#else
//================================================================
/*!@brief
  Set in_c_funcall flag (stub for POSIX/Linux build)

  @param mrb    mruby state
  @param flag   1=in C->Ruby funcall (skip tick), 0=normal
*/
void
mrb_set_in_c_funcall(mrb_state *mrb, int flag)
{
  (void)mrb;
  (void)flag;
  // No-op for POSIX build (no tick task in Linux environment)
}

#endif


void
hal_idle_cpu(mrb_state *mrb)
{
  (void)mrb;

#ifndef PICORUBY_HOST_BUILD
  // FreeRTOS environment: Task switch
  taskYIELD();
#else
  // mrbc build: POSIX sleep
  usleep(5000);
#endif
}
