#include <stdint.h>
#include <string.h>
#include "picoruby.h"

// Forward declarations for HAL msg API (avoid including full fmrb_hal.h during PicoRuby build)
typedef int32_t fmrb_msg_task_id_t;
typedef enum {
    FMRB_OK = 0,
    FMRB_ERR_TIMEOUT = -3
} fmrb_err_t;

#define FMRB_MSG_MAX_TASKS    16
#define FMRB_MSG_TASK_HOST    0
#define FMRB_MSG_TASK_SYSTEM  1

typedef struct {
    uint32_t type;
    uint32_t size;
    uint8_t data[64];
} fmrb_msg_t;

// Forward declarations of message functions
extern fmrb_err_t fmrb_msg_send(fmrb_msg_task_id_t dest_task_id, const fmrb_msg_t *msg, uint32_t timeout_ms);
extern fmrb_err_t fmrb_msg_receive(fmrb_msg_task_id_t task_id, fmrb_msg_t *msg, uint32_t timeout_ms);
extern int fmrb_msg_broadcast(const fmrb_msg_t *msg, uint32_t timeout_ms);
extern bool fmrb_msg_queue_exists(fmrb_msg_task_id_t task_id);

/**
 * Send a message to a task's queue
 * @param task_id [Integer] Destination task ID (0-15)
 * @param msg_type [Integer] Message type
 * @param data [String] Message data (max 64 bytes)
 * @param timeout_ms [Integer] Timeout in milliseconds (optional, default: 100)
 * @return [Boolean] true on success, false on failure
 *
 * Example:
 *   Kernel.send_message(0, 1, "key_down:13", 100)  # Send to host task
 */
static mrb_value
mrb_kernel_send_message(mrb_state *mrb, mrb_value self)
{
  mrb_int task_id, msg_type;
  char *data;
  mrb_int data_len;
  mrb_int timeout_ms = 100;  // Default timeout

  mrb_get_args(mrb, "iis|i", &task_id, &msg_type, &data, &data_len, &timeout_ms);

  // Validate task ID
  if (task_id < 0 || task_id >= FMRB_MSG_MAX_TASKS) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid task ID (must be 0-15)");
  }

  // Validate data size
  if (data_len > 64) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Message data too large (max 64 bytes)");
  }

  // Create HAL message
  fmrb_msg_t msg = {
    .type = (uint32_t)msg_type,
    .size = (uint32_t)data_len
  };
  memcpy(msg.data, data, data_len);

  // Send message
  fmrb_err_t result = fmrb_msg_send((fmrb_msg_task_id_t)task_id, &msg, (uint32_t)timeout_ms);

  return mrb_bool_value(result == FMRB_OK);
}

/**
 * Receive a message from a task's queue
 * @param task_id [Integer] Task ID to receive from (usually current task)
 * @param timeout_ms [Integer] Timeout in milliseconds (optional, default: 1000)
 * @return [Array, nil] Message array [type, data], or nil on timeout
 *
 * Example:
 *   msg = Kernel.receive_message(2, 5000)  # Wait up to 5 seconds
 *   if msg
 *     type, data = msg
 *     puts "Received type #{type}: #{data}"
 *   end
 */
static mrb_value
mrb_kernel_receive_message(mrb_state *mrb, mrb_value self)
{
  mrb_int task_id;
  mrb_int timeout_ms = 1000;  // Default timeout

  mrb_get_args(mrb, "i|i", &task_id, &timeout_ms);

  // Validate task ID
  if (task_id < 0 || task_id >= FMRB_MSG_MAX_TASKS) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid task ID (must be 0-15)");
  }

  // Receive message
  fmrb_msg_t msg;
  fmrb_err_t result = fmrb_msg_receive((fmrb_msg_task_id_t)task_id, &msg, (uint32_t)timeout_ms);

  if (result == FMRB_ERR_TIMEOUT) {
    return mrb_nil_value();
  } else if (result != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to receive message: %d", result);
  }

  // Create Ruby array with message data [type, data]
  mrb_value ary = mrb_ary_new_capa(mrb, 2);
  mrb_ary_push(mrb, ary, mrb_fixnum_value(msg.type));
  mrb_ary_push(mrb, ary, mrb_str_new(mrb, (const char*)msg.data, msg.size));

  return ary;
}

/**
 * Broadcast a message to all registered task queues
 * @param msg_type [Integer] Message type
 * @param data [String] Message data (max 64 bytes)
 * @param timeout_ms [Integer] Timeout in milliseconds per queue (optional, default: 10)
 * @return [Integer] Number of tasks that successfully received the message
 *
 * Example:
 *   count = Kernel.broadcast_message(99, "system_shutdown")
 *   puts "Notified #{count} tasks"
 */
static mrb_value
mrb_kernel_broadcast_message(mrb_state *mrb, mrb_value self)
{
  mrb_int msg_type;
  char *data;
  mrb_int data_len;
  mrb_int timeout_ms = 10;  // Default timeout

  mrb_get_args(mrb, "is|i", &msg_type, &data, &data_len, &timeout_ms);

  // Validate data size
  if (data_len > 64) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Message data too large (max 64 bytes)");
  }

  // Create HAL message
  fmrb_msg_t msg = {
    .type = (uint32_t)msg_type,
    .size = (uint32_t)data_len
  };
  memcpy(msg.data, data, data_len);

  // Broadcast message
  int count = fmrb_msg_broadcast(&msg, (uint32_t)timeout_ms);

  return mrb_fixnum_value(count);
}

/**
 * Check if a task has a registered message queue
 * @param task_id [Integer] Task ID to check
 * @return [Boolean] true if queue exists, false otherwise
 *
 * Example:
 *   if Kernel.message_queue_exists?(0)
 *     puts "Host task is ready"
 *   end
 */
static mrb_value
mrb_kernel_message_queue_exists(mrb_state *mrb, mrb_value self)
{
  mrb_int task_id;

  mrb_get_args(mrb, "i", &task_id);

  // Validate task ID
  if (task_id < 0 || task_id >= FMRB_MSG_MAX_TASKS) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid task ID (must be 0-15)");
  }

  bool exists = fmrb_msg_queue_exists((fmrb_msg_task_id_t)task_id);

  return mrb_bool_value(exists);
}

// Forward declaration for FmrbKernel class initialization (defined in ports/esp32/kernel.c)
extern void mrb_fmrb_kernel_init(mrb_state *mrb);

void
mrb_picoruby_fmrb_kernel_init(mrb_state *mrb)
{
  struct RClass *kernel_module = mrb->kernel_module;

  // Message queue task ID constants
  mrb_define_const(mrb, kernel_module, "MSG_TASK_HOST", mrb_fixnum_value(FMRB_MSG_TASK_HOST));
  mrb_define_const(mrb, kernel_module, "MSG_TASK_SYSTEM", mrb_fixnum_value(FMRB_MSG_TASK_SYSTEM));

  // Message queue methods
  mrb_define_module_function(mrb, kernel_module, "send_message", mrb_kernel_send_message, MRB_ARGS_ARG(3, 1));
  mrb_define_module_function(mrb, kernel_module, "receive_message", mrb_kernel_receive_message, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function(mrb, kernel_module, "broadcast_message", mrb_kernel_broadcast_message, MRB_ARGS_ARG(2, 1));
  mrb_define_module_function(mrb, kernel_module, "message_queue_exists?", mrb_kernel_message_queue_exists, MRB_ARGS_REQ(1));
}

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_kernel_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_kernel_init(mrb);
  mrb_fmrb_kernel_init(mrb);  // Initialize FmrbKernel class
}

void
mrb_picoruby_fmrb_kernel_gem_final(mrb_state *mrb)
{
  // Cleanup if needed
}
