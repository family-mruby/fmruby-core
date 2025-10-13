#include <inttypes.h>

#include "picoruby.h"
#include "mrb/main_task.c"

#ifndef HEAP_SIZE
#define HEAP_SIZE (1024 * 128)
#endif

static uint8_t vm_heap[HEAP_SIZE];
picorb_state *vm;

int picoruby_esp32(void)
{
  char *argv[] = {"picoruby"};

  picorb_vm_init();

  mrb_irep *irep = mrb_read_irep(vm, main_task);
  if (irep) {
    mrb_context_run(vm, mrb_proc_new(vm, irep), mrb_top_self(vm), 0);
  }

  return 0;
}
