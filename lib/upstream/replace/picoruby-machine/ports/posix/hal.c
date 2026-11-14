/*
  Original source code from mruby/mrubyc:
    Copyright (C) 2015- Kyushu Institute of Technology.
    Copyright (C) 2015- Shimane IT Open-Innovation Center.
  Modified source code for picoruby/femtoruby:
    Copyright (C) 2025 HASUMI Hitoshi.

  This file is distributed under BSD 3-Clause License.
*/


/***** System headers *******************************************************/
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>


/***** Local headers ********************************************************/
#include "hal.h"


/***** Local variables ******************************************************/
#if defined(PICORB_VM_MRUBYC)
#ifndef __EMSCRIPTEN__
static sigset_t sigset_, sigset2_;
#endif
typedef void mrb_state;
#define MRB_TICK_UNIT MRBC_TICK_UNIT
#endif

/***** Global functions *****************************************************/

/*
 * hal_init, hal_register_vm, hal_deinit, hal_deinit_by_pool,
 * hal_idle_cpu are provided by hal_freertos.c
 * (ESP-IDF build) and resolved at final link time.
 * They are NOT defined here to avoid libmruby.a shadowing them.
 */

#if defined(PICORB_VM_MRUBYC)
void picorb_hal_enable_irq(void)
{
#ifndef __EMSCRIPTEN__
  sigprocmask(SIG_SETMASK, &sigset2_, 0);
#endif
}

void picorb_hal_disable_irq(void)
{
#ifndef __EMSCRIPTEN__
  sigprocmask(SIG_BLOCK, &sigset_, &sigset2_);
#endif
}

void
picorb_hal_idle_cpu(void){
  sleep(1);
}
#endif

int
picorb_hal_write(int fd, const void *buf, int nbytes)
{
  return (int)write(1, buf, nbytes);
}


/* mrb_task_enable_irq / mrb_task_disable_irq are provided by hal-posix-task */

void
picorb_hal_abort(const char *s)
{
  if (s) {
    picorb_hal_write(1, s, strlen(s));
  }
  exit(1);
}

/* hal_register_vm, hal_deinit, hal_deinit_by_pool
 * are provided by hal_freertos.c (ESP-IDF) at link time. */

