/*
  Original source code from mruby/mrubyc:
    Copyright (C) 2015- Kyushu Institute of Technology.
    Copyright (C) 2015- Shimane IT Open-Innovation Center.
  Modified source code for picoruby/microruby:
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
#define mrb_tick(mrb) mrbc_tick()
#define hal_init(mrb) hal_init()
#define MRB_TICK_UNIT MRBC_TICK_UNIT
#endif

/***** Global functions *****************************************************/

/*
 * hal_init, hal_register_vm, hal_deinit, hal_deinit_by_pool,
 * mrb_set_in_c_funcall, hal_idle_cpu are provided by hal_freertos.c
 * (ESP-IDF build) and resolved at final link time.
 * They are NOT defined here to avoid libmruby.a shadowing them.
 */

#if defined(PICORB_VM_MRUBYC)
void hal_enable_irq(void)
{
#ifndef __EMSCRIPTEN__
  sigprocmask(SIG_SETMASK, &sigset2_, 0);
#endif
}

void hal_disable_irq(void)
{
#ifndef __EMSCRIPTEN__
  sigprocmask(SIG_BLOCK, &sigset_, &sigset2_);
#endif
}

void
hal_idle_cpu(void){
  sleep(1);
}
#endif

int
hal_write(int fd, const void *buf, int nbytes)
{
  return (int)write(1, buf, nbytes);
}


/* mrb_task_enable_irq / mrb_task_disable_irq are provided by hal-posix-task */

void
hal_abort(const char *s)
{
  if (s) {
    hal_write(1, s, strlen(s));
  }
  exit(1);
}

/* hal_register_vm, hal_deinit, hal_deinit_by_pool, mrb_set_in_c_funcall
 * are provided by hal_freertos.c (ESP-IDF) at link time. */

/*
 * Prism allocator mutex (used by prism_alloc.c via extern)
 */
#include <pthread.h>
static pthread_mutex_t s_prism_mutex = PTHREAD_MUTEX_INITIALIZER;
void fmrb_prism_lock(void)   { pthread_mutex_lock(&s_prism_mutex); }
void fmrb_prism_unlock(void) { pthread_mutex_unlock(&s_prism_mutex); }
