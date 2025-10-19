#include "picoruby.h"
#include "fmrb_audio.h"

static mrb_value
fmrb_audio_init_wrapper(mrb_state *mrb, mrb_value self)
{
  fmrb_audio_err_t ret = fmrb_audio_init();
  return mrb_fixnum_value(ret);
}

static mrb_value
fmrb_audio_deinit_wrapper(mrb_state *mrb, mrb_value self)
{
  // fmrb_audio_deinit();
  return mrb_nil_value();
}

static mrb_value
fmrb_audio_play_wrapper(mrb_state *mrb, mrb_value self)
{
  // Placeholder for audio playback
  // TODO: Implement audio playback functionality
  return mrb_nil_value();
}

static mrb_value
fmrb_audio_stop_wrapper(mrb_state *mrb, mrb_value self)
{
  // Placeholder for audio stop
  // TODO: Implement audio stop functionality
  return mrb_nil_value();
}

static mrb_value
fmrb_audio_pause_wrapper(mrb_state *mrb, mrb_value self)
{
  // Placeholder for audio pause
  // TODO: Implement audio pause functionality
  return mrb_nil_value();
}

static mrb_value
fmrb_audio_resume_wrapper(mrb_state *mrb, mrb_value self)
{
  // Placeholder for audio resume
  // TODO: Implement audio resume functionality
  return mrb_nil_value();
}

static mrb_value
fmrb_audio_set_volume_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int volume;
  mrb_get_args(mrb, "i", &volume);

  if (volume < 0 || volume > 100) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Volume must be between 0 and 100");
  }

  // TODO: Implement volume control
  return mrb_nil_value();
}

void
mrb_picoruby_fmrb_audio_init(mrb_state *mrb)
{
  struct RClass *fmrb_audio_class = mrb_define_class(mrb, "FmrbAudio", mrb->object_class);

  // Audio module methods
  mrb_define_class_method(mrb, fmrb_audio_class, "init", fmrb_audio_init_wrapper, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, fmrb_audio_class, "deinit", fmrb_audio_deinit_wrapper, MRB_ARGS_NONE());

  // Playback control
  mrb_define_class_method(mrb, fmrb_audio_class, "play", fmrb_audio_play_wrapper, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, fmrb_audio_class, "stop", fmrb_audio_stop_wrapper, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, fmrb_audio_class, "pause", fmrb_audio_pause_wrapper, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, fmrb_audio_class, "resume", fmrb_audio_resume_wrapper, MRB_ARGS_NONE());

  // Volume control
  mrb_define_class_method(mrb, fmrb_audio_class, "set_volume", fmrb_audio_set_volume_wrapper, MRB_ARGS_REQ(1));

  // Constants
  mrb_define_const(mrb, fmrb_audio_class, "OK", mrb_fixnum_value(FMRB_AUDIO_OK));
  mrb_define_const(mrb, fmrb_audio_class, "ERR_INVALID_PARAM", mrb_fixnum_value(FMRB_AUDIO_ERR_INVALID_PARAM));
  mrb_define_const(mrb, fmrb_audio_class, "ERR_NO_MEMORY", mrb_fixnum_value(FMRB_AUDIO_ERR_NO_MEMORY));
  mrb_define_const(mrb, fmrb_audio_class, "ERR_FAILED", mrb_fixnum_value(FMRB_AUDIO_ERR_FAILED));
}
