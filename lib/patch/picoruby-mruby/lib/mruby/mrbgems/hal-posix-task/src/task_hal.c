/*
** task_hal.c - HAL implementation for mruby-task (Family mruby patched)
**
** On Family mruby, tick management and IRQ control are handled by
** hal_freertos.c (FreeRTOS task-based). This file only initializes
** the mruby task state fields in mrb_hal_task_init().
** SIGALRM/setitimer are NOT used to avoid conflicting with FreeRTOS.
*/

#include <mruby.h>
#include "task_hal.h"
#include <time.h>
#include <unistd.h>
#include <stdint.h>

/* hal_init is provided by hal_freertos.c (linked at final link time) */
extern void hal_init(mrb_state *mrb);

/* Time conversion constants */
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_SEC  1000000000ULL
#define USEC_PER_MSEC 1000ULL

void
mrb_hal_task_init(mrb_state *mrb)
{
  int i;

  /* Initialize task state */
  for (i = 0; i < 4; i++) {
    mrb->task.queues[i] = NULL;
  }
  mrb->task.tick = 0;
  mrb->task.wakeup_tick = UINT32_MAX;
  mrb->task.switching = FALSE;

  /* Create tick task (FreeRTOS) - idempotent, only creates once */
  hal_init(mrb);
}

/* mrb_task_enable_irq / mrb_task_disable_irq are provided by hal_freertos.c */

void
mrb_hal_task_idle_cpu(mrb_state *mrb)
{
  (void)mrb;
  usleep(MRB_TICK_UNIT * 1000);
}

void
mrb_hal_task_sleep_us(mrb_state *mrb, mrb_int usec)
{
  struct timespec start, now, sleep_time;
  int ret;

  (void)mrb;

  if (usec < 0) {
    return;
  }

  ret = clock_gettime(CLOCK_MONOTONIC, &start);
  if (ret != 0) {
    usleep(usec);
    return;
  }

  uint64_t target_ns = (uint64_t)usec * USEC_PER_MSEC;

  while (1) {
    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret != 0) {
      break;
    }

    uint64_t elapsed_ns = (uint64_t)(now.tv_sec - start.tv_sec) * NSEC_PER_SEC +
                          (uint64_t)(now.tv_nsec - start.tv_nsec);

    if (elapsed_ns >= target_ns) {
      break;
    }

    uint64_t remaining_ns = target_ns - elapsed_ns;
    if (remaining_ns > NSEC_PER_MSEC) {
      sleep_time.tv_sec = remaining_ns / NSEC_PER_SEC;
      sleep_time.tv_nsec = remaining_ns % NSEC_PER_SEC;
    }
    else {
      sleep_time.tv_sec = 0;
      sleep_time.tv_nsec = NSEC_PER_MSEC;
    }

    nanosleep(&sleep_time, NULL);
  }
}

void
mrb_hal_task_final(mrb_state *mrb)
{
  (void)mrb;
}

void
mrb_hal_posix_task_gem_init(mrb_state *mrb)
{
  (void)mrb;
}

void
mrb_hal_posix_task_gem_final(mrb_state *mrb)
{
  (void)mrb;
}
