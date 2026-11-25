#include <stdint.h>
#include <string.h>
#include "picoruby.h"

// Kernel.send_message, receive_message, broadcast_message,
// message_queue_exists? and the Kernel::MSG_TASK_* ids (defined in
// ports/esp32/kernel.c, which sees the body's fmrb_msg_t and PROC_ID_*)
extern void mrb_fmrb_kernel_msg_init(mrb_state *mrb);
// Forward declaration for FmrbKernel class initialization (defined in ports/esp32/kernel.c)
extern void mrb_fmrb_kernel_init(mrb_state *mrb);
// Forward declaration for FmrbNet module initialization (defined in ports/esp32/net.c)
extern void mrb_fmrb_net_init(mrb_state *mrb);

void
mrb_picoruby_fmrb_kernel_init(mrb_state *mrb)
{
  // Message queue methods and their task id constants
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
