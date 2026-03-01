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
#include <stdio.h>
#include <unistd.h>

#include "../../include/hal.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esp_log.h"

#include "fmrb_app.h"

#define MAX_MRB_VMS 16  // Maximum number of VMs

typedef struct {
    mrb_state *mrb;
    int active;        // 1=in use, 0=unused
    int in_c_funcall;  // 0=MRB_C_FUNCALL_EXIT  1=MRB_C_FUNCALL_ENTER
    int irq;           // 0=MRB_ENABLE_IRQ  1=MRB_DISABLE_IRQ
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
                    // Skip tick if VM is executing C->Ruby funcall or IRQ is disabled
                    if (MRB_C_FUNCALL_EXIT == g_tick_manager.vms[i].in_c_funcall && MRB_ENABLE_IRQ == g_tick_manager.vms[i].irq) {
                        mrb_tick(g_tick_manager.vms[i].mrb);
                    }
                }
            }
            xSemaphoreGive(g_tick_manager.mutex);
        }
    }
}

/***** Global functions *****************************************************/

//================================================================
/*!@brief
  initialize
*/
void
picoruby_hal_init(mrb_state *mrb)
{
  ESP_LOGI("hal", "hal_init called");

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

  // Add mrb to list
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


//================================================================
/*!@brief
  deinitialize

  Remove mrb from VM list
*/
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


//================================================================
/*!@brief
  enable interrupt
*/
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


//================================================================
/*!@brief
  disable interrupt
*/
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
