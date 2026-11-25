#include <stdint.h>
#include <string.h>
#include "picoruby.h"

// Task-queue ids named on the Kernel module.
#define FMRB_MSG_TASK_HOST    0
#define FMRB_MSG_TASK_SYSTEM  1

// Kernel.send_message, receive_message, broadcast_message and
// message_queue_exists? (defined in ports/esp32/kernel.c, which sees the
// body's fmrb_msg_t)
extern void mrb_fmrb_kernel_msg_init(mrb_state *mrb);
// Forward declaration for FmrbKernel class initialization (defined in ports/esp32/kernel.c)
extern void mrb_fmrb_kernel_init(mrb_state *mrb);
// Forward declaration for FmrbNet module initialization (defined in ports/esp32/net.c)
extern void mrb_fmrb_net_init(mrb_state *mrb);

void
mrb_picoruby_fmrb_kernel_init(mrb_state *mrb)
{
  struct RClass *kernel_module = mrb->kernel_module;

  // Message queue task ID constants
  mrb_define_const(mrb, kernel_module, "MSG_TASK_HOST", mrb_fixnum_value(FMRB_MSG_TASK_HOST));
  mrb_define_const(mrb, kernel_module, "MSG_TASK_SYSTEM", mrb_fixnum_value(FMRB_MSG_TASK_SYSTEM));

  // Message queue methods
  mrb_fmrb_kernel_msg_init(mrb);
}

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_kernel_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_kernel_init(mrb);
  mrb_fmrb_kernel_init(mrb);  // Initialize FmrbKernel class
  mrb_fmrb_net_init(mrb);     // Initialize FmrbNet module
}

void
mrb_picoruby_fmrb_kernel_gem_final(mrb_state *mrb)
{
  // Cleanup if needed
}
