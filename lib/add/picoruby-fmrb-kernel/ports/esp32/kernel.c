#include <mruby.h>
#include <mruby/value.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include "fmrb_kernel.h"
#include "fmrb_app.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"

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
    TickType_t start_tick = fmrb_task_get_tick_count();
    TickType_t target_tick = start_tick + FMRB_MS_TO_TICKS(timeout_ms);

    // Receive messages until timeout expires
    while (true) {
        // Calculate remaining time
        TickType_t current_tick = fmrb_task_get_tick_count();
        if (current_tick >= target_tick) {
            // Timeout expired
            break;
        }

        TickType_t remaining_ticks = target_tick - current_tick;

        // Try to receive message with remaining timeout
        fmrb_msg_t msg;
        fmrb_err_t ret = fmrb_msg_receive(PROC_ID_KERNEL, &msg, remaining_ticks);

        if (ret == FMRB_OK) {
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

    return mrb_nil_value();
}

// Kernel#_spawn_app_req(app_name) -> bool
// Spawn application by name
static mrb_value mrb_kernel_handler_spawn_app_req(mrb_state *mrb, mrb_value self)
{
    const char *app_name;
    mrb_get_args(mrb, "z", &app_name);

    FMRB_LOGI(TAG, "Spawning app: %s", app_name);

    bool result = fmrb_app_spawn_default_app(app_name);

    if (result) {
        FMRB_LOGI(TAG, "App %s spawned successfully", app_name);
    } else {
        FMRB_LOGE(TAG, "Failed to spawn app: %s", app_name);
    }

    return mrb_bool_value(result);
}

static mrb_value mrb_kernel_set_ready(mrb_state *mrb, mrb_value self)
{
    fmrb_kernel_set_ready();
    return mrb_nil_value();
}

void mrb_fmrb_kernel_init(mrb_state *mrb)
{
    // Define Kernel class
    struct RClass *handler_class = mrb_define_class(mrb, "Kernel", mrb->object_class);
    mrb_define_method(mrb, handler_class, "_set_ready", mrb_kernel_set_ready, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_init", mrb_kernel_handler_init, MRB_ARGS_NONE());
    mrb_define_method(mrb, handler_class, "_spin", mrb_kernel_handler_spin, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, handler_class, "_spawn_app_req", mrb_kernel_handler_spawn_app_req, MRB_ARGS_REQ(1));

    // Process ID constants
    mrb_define_const(mrb, handler_class, "PROC_ID_KERNEL", mrb_fixnum_value(PROC_ID_KERNEL));
    mrb_define_const(mrb, handler_class, "PROC_ID_HOST", mrb_fixnum_value(PROC_ID_HOST));
    mrb_define_const(mrb, handler_class, "PROC_ID_SYSTEM_APP", mrb_fixnum_value(PROC_ID_SYSTEM_APP));
    mrb_define_const(mrb, handler_class, "PROC_ID_USER_APP0", mrb_fixnum_value(PROC_ID_USER_APP0));
    mrb_define_const(mrb, handler_class, "PROC_ID_USER_APP1", mrb_fixnum_value(PROC_ID_USER_APP1));
    mrb_define_const(mrb, handler_class, "PROC_ID_USER_APP2", mrb_fixnum_value(PROC_ID_USER_APP2));

    // Message type constants
    mrb_define_const(mrb, handler_class, "MSG_TYPE_APP_CONTROL", mrb_fixnum_value(FMRB_MSG_TYPE_APP_CONTROL));
    mrb_define_const(mrb, handler_class, "MSG_TYPE_APP_GFX", mrb_fixnum_value(FMRB_MSG_TYPE_APP_GFX));
    mrb_define_const(mrb, handler_class, "MSG_TYPE_APP_AUDIO", mrb_fixnum_value(FMRB_MSG_TYPE_APP_AUDIO));

    // App control message subtypes
    mrb_define_const(mrb, handler_class, "APP_CTRL_SPAWN", mrb_fixnum_value(FMRB_APP_CTRL_SPAWN));
    mrb_define_const(mrb, handler_class, "APP_CTRL_KILL", mrb_fixnum_value(FMRB_APP_CTRL_KILL));
    mrb_define_const(mrb, handler_class, "APP_CTRL_SUSPEND", mrb_fixnum_value(FMRB_APP_CTRL_SUSPEND));
    mrb_define_const(mrb, handler_class, "APP_CTRL_RESUME", mrb_fixnum_value(FMRB_APP_CTRL_RESUME));
}

void mrb_fmrb_kernel_final(mrb_state *mrb)
{
    // Cleanup if needed
}
