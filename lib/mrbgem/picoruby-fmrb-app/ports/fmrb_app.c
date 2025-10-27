#include <string.h>

#include "fmrb_app.h"
#include "fmrb_app_local.h"
#include "fmrb_hal.h"
#include "fmrb_err.h"




void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb)
{
    mrb_fmrb_gfx_init(mrb);
    //mrb_fmrb_audio_init(mrb);
}

void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb)
{
    mrb_fmrb_gfx_final(mrb);
    //mrb_fmrb_audio_final(mrb);
}
