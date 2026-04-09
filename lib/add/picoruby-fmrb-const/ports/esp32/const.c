#include <mruby.h>
#include <string.h>

#include "fmrb_app.h"
#include "fmrb_task_config.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "../../include/picoruby_fmrb_const.h"

/* Default theme (can be overridden by system_conf.toml before VMs start) */
static fmrb_theme_t g_theme = {
    .desktop_bg  = 0xF6,
    .menu_bg     = 0xC5,
    .window_bg   = 0xFF,
    .text        = 0x00,
    .text_light  = 0xFF,
    .highlight   = 0xC5,
    .border      = 0x60,
    .button      = 0x60,
    .dir_color   = 0x03,
};

void fmrb_theme_set(const fmrb_theme_t *theme)
{
    if (theme) {
        memcpy(&g_theme, theme, sizeof(fmrb_theme_t));
    }
}

const fmrb_theme_t* fmrb_theme_get(void)
{
    return &g_theme;
}

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
    mrb_define_const(mrb, const_module, "PROC_STATE_INIT", mrb_fixnum_value(PROC_STATE_INIT));
    mrb_define_const(mrb, const_module, "PROC_STATE_RUNNING", mrb_fixnum_value(PROC_STATE_RUNNING));
    mrb_define_const(mrb, const_module, "PROC_STATE_SUSPENDED", mrb_fixnum_value(PROC_STATE_SUSPENDED));
    mrb_define_const(mrb, const_module, "PROC_STATE_STOPPING", mrb_fixnum_value(PROC_STATE_STOPPING));

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

    // Theme color constants (from g_theme, possibly overridden by system_conf.toml)
    const fmrb_theme_t *t = fmrb_theme_get();
    mrb_define_const(mrb, const_module, "THEME_DESKTOP_BG", mrb_fixnum_value(t->desktop_bg));
    mrb_define_const(mrb, const_module, "THEME_MENU_BG", mrb_fixnum_value(t->menu_bg));
    mrb_define_const(mrb, const_module, "THEME_WINDOW_BG", mrb_fixnum_value(t->window_bg));
    mrb_define_const(mrb, const_module, "THEME_TEXT", mrb_fixnum_value(t->text));
    mrb_define_const(mrb, const_module, "THEME_TEXT_LIGHT", mrb_fixnum_value(t->text_light));
    mrb_define_const(mrb, const_module, "THEME_HIGHLIGHT", mrb_fixnum_value(t->highlight));
    mrb_define_const(mrb, const_module, "THEME_BORDER", mrb_fixnum_value(t->border));
    mrb_define_const(mrb, const_module, "THEME_BUTTON", mrb_fixnum_value(t->button));
    mrb_define_const(mrb, const_module, "THEME_DIR_COLOR", mrb_fixnum_value(t->dir_color));
}
