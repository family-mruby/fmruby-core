#include <mruby.h>
#include <mruby/value.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include "fmrb_kernel.h"
#include "fmrb_app.h"
#include "fmrb_rtos.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"
#include "fmrb_link_transport.h"
#include "../../../../../../main/boot.h"
#include "hal.h"

static const char* TAG = "kernel";

// Kernel#_init() - Initialize kernel handler
// Sets @tick, @max_app_num instance variables and creates message queue
static mrb_value mrb_kernel_handler_init(mrb_state *mrb, mrb_value self)
{
    // Set @tick instance variable (default 33ms)
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@tick"),
               mrb_fixnum_value(33));

    // Set @max_app_num instance variable
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@max_app_num"),
               mrb_fixnum_value(FMRB_MAX_APPS));

    // Set @max_path_len instance variable
    mrb_iv_set(mrb, self, mrb_intern_cstr(mrb, "@max_path_len"),
               mrb_fixnum_value(FMRB_MAX_PATH_LEN));

    // Create message queue for kernel
    fmrb_msg_queue_config_t queue_config = {
        .queue_length = 10,
        .message_size = sizeof(fmrb_msg_t)
    };

    fmrb_err_t ret = fmrb_msg_create_queue(PROC_ID_KERNEL, &queue_config);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR,
                   "Failed to create kernel message queue: %d", ret);
    }

    FMRB_LOGI(TAG, "Kernel handler initialized: tick=%d, max_apps=%d",
             33, FMRB_MAX_APPS);

    return mrb_nil_value();
}

// Kernel#_spin(timeout_ms) - Process messages
// Receives messages and calls Ruby msg_handler(msg) for each message
// Continues to receive messages until timeout expires
static mrb_value mrb_kernel_handler_spin(mrb_state *mrb, mrb_value self)
{
    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);

    // Record start time
    fmrb_tick_t start_tick = fmrb_task_get_tick_count();
    fmrb_tick_t target_tick = start_tick + FMRB_MS_TO_TICKS(timeout_ms);

    mrb_set_in_c_funcall(mrb, MRB_C_FUNCALL_ENTER);

    // Receive messages until timeout expires
    while (true) {
        // Calculate remaining time
        fmrb_tick_t current_tick = fmrb_task_get_tick_count();
        if (current_tick >= target_tick) {
            // Timeout expired
            break;
        }

        fmrb_tick_t remaining_ticks = target_tick - current_tick;

        // Try to receive message with remaining timeout
        fmrb_msg_t msg;
        fmrb_err_t ret = fmrb_msg_receive(PROC_ID_KERNEL, &msg, remaining_ticks);

        if (ret == FMRB_OK) {
            FMRB_LOGI(TAG, "Kernel received message: type=%d, src_pid=%d, size=%d",
                     msg.type, msg.src_pid, (int)msg.size);

            // Build Ruby hash: {type: int, src_pid: int, data: string}
            mrb_value hash = mrb_hash_new(mrb);
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "type")),
                         mrb_fixnum_value(msg.type));
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "src_pid")),
                         mrb_fixnum_value(msg.src_pid));
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_cstr(mrb, "data")),
                         mrb_str_new(mrb, (const char*)msg.data, msg.size));

            // Call Ruby method: self.msg_handler(msg)
            mrb_funcall(mrb, self, "msg_handler", 1, hash);

            // Continue loop to process more messages or wait for remaining time
        } else if (ret == FMRB_ERR_TIMEOUT) {
            // Timeout - exit loop
            break;
        } else {
            FMRB_LOGW(TAG, "Kernel message receive error: %d", ret);
            break;
        }
    }

    mrb_set_in_c_funcall(mrb, MRB_C_FUNCALL_EXIT);
    
    return mrb_nil_value();
}

// Kernel#_spawn_app_req(app_name) -> bool
// Spawn application by name
static mrb_value mrb_kernel_handler_spawn_app_req(mrb_state *mrb, mrb_value self)
{
    const char *app_name;
    bool mrb_result = false;
    mrb_get_args(mrb, "z", &app_name);

    FMRB_LOGI(TAG, "Spawning app: %s", app_name);

    fmrb_err_t result = fmrb_app_spawn_app(app_name);

    if (result == FMRB_OK) {
        FMRB_LOGI(TAG, "App %s spawned successfully", app_name);
        mrb_result = true;
    } else {
        FMRB_LOGE(TAG, "Failed to spawn app: %s", app_name);
        mrb_result = false;
    }

    return mrb_bool_value(mrb_result);
}

static mrb_value mrb_kernel_set_ready(mrb_state *mrb, mrb_value self)
{
    fmrb_kernel_set_ready();
    return mrb_nil_value();
}

// Kernel#check_protocol_version(timeout_ms = 5000) -> bool
// Check protocol version with host
static mrb_value mrb_kernel_check_protocol_version(mrb_state *mrb, mrb_value self)
{
    mrb_int timeout_ms = 5000;  // Default 5 seconds
    mrb_get_args(mrb, "|i", &timeout_ms);

    FMRB_LOGI(TAG, "Checking protocol version (timeout=%d ms)...", (int)timeout_ms);

    fmrb_err_t ret = fmrb_link_transport_check_version((uint32_t)timeout_ms);

    if (ret == FMRB_OK) {
        FMRB_LOGI(TAG, "Protocol version check succeeded");
        return mrb_true_value();
    } else {
        FMRB_LOGE(TAG, "Protocol version check failed: %d", ret);
        return mrb_false_value();
    }
}

// Kernel.set_hid_target(pid) - Set HID event target app
static mrb_value mrb_kernel_set_hid_target(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);

    if (pid < 0 || pid > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid PID");
    }

    fmrb_err_t ret = fmrb_kernel_set_hid_target((uint8_t)pid);
    if (ret != FMRB_OK) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to set HID target");
    }

    return mrb_nil_value();
}

// Kernel.set_focused_window(window_id) - Set focused window ID
static mrb_value mrb_kernel_set_focused_window(mrb_state *mrb, mrb_value self)
{
    mrb_int window_id;
    mrb_get_args(mrb, "i", &window_id);

    if (window_id < 0 || window_id > 255) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid window ID");
    }

    fmrb_err_t ret = fmrb_kernel_set_focused_window((uint8_t)window_id);
    if (ret != FMRB_OK) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to set focused window");
    }

    return mrb_nil_value();
}

void mrb_fmrb_kernel_init(mrb_state *mrb)
{
    // Define FmrbKernel class
    struct RClass *handler_class = mrb_define_class(mrb, "FmrbKernel", mrb->object_class);
    mrb_define_method(mrb, handler_class, "_set_ready", mrb_kernel_set_ready, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_init", mrb_kernel_handler_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_spin", mrb_kernel_handler_spin, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_spawn_app_req", mrb_kernel_handler_spawn_app_req, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "check_protocol_version", mrb_kernel_check_protocol_version, MRB_ARGS_OPT(1));

    // Define Kernel module for HID routing functions
    struct RClass *kernel_mod = mrb_define_module(mrb, "Kernel");
    mrb_define_module_function(mrb, kernel_mod, "set_hid_target", mrb_kernel_set_hid_target, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, kernel_mod, "set_focused_window", mrb_kernel_set_focused_window, MRB_ARGS_REQ(1));

    // Note: Constants now defined in FmrbConst module (picoruby-fmrb-const gem)
}

void mrb_fmrb_kernel_final(mrb_state *mrb)
{
    // Cleanup if needed
}
