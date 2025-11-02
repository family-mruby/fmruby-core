#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_err.h"
#include "fmrb_msg.h"
#include "../../include/picoruby_fmrb_app.h"
#include "app_local.h"

// FmrbApp._init() - Initialize application
static mrb_value mrb_fmrb_app_init(mrb_state *mrb, mrb_value self)
{
    // Application initialization logic
    // This can be extended based on requirements
    fmrb_app_task_context_t* ctx = fmrb_current();

    // Create message queue for this app
    fmrb_msg_queue_config_t queue_config = {
        .queue_length = FMRB_USER_APP_MSG_QUEUE_LEN,
        .message_size = sizeof(fmrb_msg_t)
    };

    fmrb_err_t ret = fmrb_msg_create_queue(ctx->app_id, &queue_config);
    if (ret != FMRB_OK) {
        // Error handling - raise mruby exception
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to create message queue: %d", ret);
    }

    return self;
}

// FmrbApp.create_ipc_handle() - Create IPC handle
static mrb_value mrb_fmrb_app_create_ipc_handle(mrb_state *mrb, mrb_value self)
{
    // IPC handle creation logic
    // Returns an integer handle ID
    // TODO: Implement actual IPC handle creation via fmrb_hal
    return mrb_fixnum_value(0);
}

// FmrbApp.app_name() - Get application name
static mrb_value mrb_fmrb_app_app_name(mrb_state *mrb, mrb_value self)
{
    // Returns the application name
    return mrb_str_new_cstr(mrb, "FamilyMruby");
}

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb)
{
    // Define FmrbApp class
    struct RClass *app_class = mrb_define_class(mrb, "FmrbApp", mrb->object_class);

    mrb_define_class_method(mrb, app_class, "_init", mrb_fmrb_app_init, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "create_ipc_handle", mrb_fmrb_app_create_ipc_handle, MRB_ARGS_NONE());
    mrb_define_class_method(mrb, app_class, "app_name", mrb_fmrb_app_app_name, MRB_ARGS_NONE());

    // Initialize graphics subsystem
    mrb_fmrb_gfx_init(mrb);

    // Audio subsystem will be initialized when needed
    //mrb_fmrb_audio_init(mrb);
}

void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb)
{
    mrb_fmrb_gfx_final(mrb);
    //mrb_fmrb_audio_final(mrb);
}
