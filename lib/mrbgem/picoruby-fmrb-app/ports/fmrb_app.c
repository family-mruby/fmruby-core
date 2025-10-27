#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "fmrb_app.h"
#include "fmrb_app_local.h"
#include "fmrb_hal.h"
#include "fmrb_err.h"

// FmrbApp._init() - Initialize application
static mrb_value mrb_fmrb_app_init(mrb_state *mrb, mrb_value self)
{
    // Application initialization logic
    // This can be extended based on requirements
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
