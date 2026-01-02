#include <mruby.h>

#include "fmrb_app.h"
#include "fmrb_task_config.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "../../include/picoruby_fmrb_const.h"

void mrb_picoruby_fmrb_const_init_impl(mrb_state *mrb)
{
    // Define FmrbConst module
    struct RClass *const_module = mrb_define_module(mrb, "FmrbConst");

    // Process ID constants
    mrb_define_const(mrb, const_module, "PROC_ID_KERNEL", mrb_fixnum_value(PROC_ID_KERNEL));
    mrb_define_const(mrb, const_module, "PROC_ID_HOST", mrb_fixnum_value(PROC_ID_HOST));
    mrb_define_const(mrb, const_module, "PROC_ID_SYSTEM_APP", mrb_fixnum_value(PROC_ID_SYSTEM_APP));
    mrb_define_const(mrb, const_module, "PROC_ID_USER_APP0", mrb_fixnum_value(PROC_ID_USER_APP0));
    mrb_define_const(mrb, const_module, "PROC_ID_USER_APP1", mrb_fixnum_value(PROC_ID_USER_APP1));
    mrb_define_const(mrb, const_module, "PROC_ID_USER_APP2", mrb_fixnum_value(PROC_ID_USER_APP2));

    // Process state constants
    mrb_define_const(mrb, const_module, "PROC_STATE_FREE", mrb_fixnum_value(PROC_STATE_FREE));
    mrb_define_const(mrb, const_module, "PROC_STATE_ALLOCATED", mrb_fixnum_value(PROC_STATE_ALLOCATED));
    mrb_define_const(mrb, const_module, "PROC_STATE_INIT", mrb_fixnum_value(PROC_STATE_INIT));
    mrb_define_const(mrb, const_module, "PROC_STATE_RUNNING", mrb_fixnum_value(PROC_STATE_RUNNING));
    mrb_define_const(mrb, const_module, "PROC_STATE_SUSPENDED", mrb_fixnum_value(PROC_STATE_SUSPENDED));
    mrb_define_const(mrb, const_module, "PROC_STATE_STOPPING", mrb_fixnum_value(PROC_STATE_STOPPING));
    mrb_define_const(mrb, const_module, "PROC_STATE_ZOMBIE", mrb_fixnum_value(PROC_STATE_ZOMBIE));

    // Message type constants
    mrb_define_const(mrb, const_module, "MSG_TYPE_APP_CONTROL", mrb_fixnum_value(FMRB_MSG_TYPE_APP_CONTROL));
    mrb_define_const(mrb, const_module, "MSG_TYPE_APP_GFX", mrb_fixnum_value(FMRB_MSG_TYPE_APP_GFX));
    mrb_define_const(mrb, const_module, "MSG_TYPE_APP_AUDIO", mrb_fixnum_value(FMRB_MSG_TYPE_APP_AUDIO));
    mrb_define_const(mrb, const_module, "MSG_TYPE_HID_EVENT", mrb_fixnum_value(FMRB_MSG_TYPE_HID_EVENT));

    // App control message subtypes
    mrb_define_const(mrb, const_module, "APP_CTRL_SPAWN", mrb_fixnum_value(FMRB_APP_CTRL_SPAWN));
    mrb_define_const(mrb, const_module, "APP_CTRL_KILL", mrb_fixnum_value(FMRB_APP_CTRL_KILL));
    mrb_define_const(mrb, const_module, "APP_CTRL_SUSPEND", mrb_fixnum_value(FMRB_APP_CTRL_SUSPEND));
    mrb_define_const(mrb, const_module, "APP_CTRL_RESUME", mrb_fixnum_value(FMRB_APP_CTRL_RESUME));

    // Path length constant
    mrb_define_const(mrb, const_module, "MAX_PATH_LEN", mrb_fixnum_value(FMRB_MAX_PATH_LEN));
}
