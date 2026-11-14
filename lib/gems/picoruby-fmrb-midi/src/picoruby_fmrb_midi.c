#include "picoruby.h"

/* Implemented in ports/esp32/fmrb_midi_serial.c, which the picoruby-esp32
 * component compiles for both targets (that directory name is historical;
 * the Linux build uses it too). Declared here rather than included so this
 * file needs no firmware headers. */
void mrb_fmrb_midi_serial_init(mrb_state *mrb);
void mrb_fmrb_midi_sched_init(mrb_state *mrb);
void mrb_fmrb_midi_sched_final(mrb_state *mrb);

void
mrb_picoruby_fmrb_midi_gem_init(mrb_state *mrb)
{
  mrb_fmrb_midi_serial_init(mrb);
  mrb_fmrb_midi_sched_init(mrb);
}

void
mrb_picoruby_fmrb_midi_gem_final(mrb_state *mrb)
{
  /* The scheduler outlives this VM, so anything this app queued has to go
   * with it - a killed app must not keep playing (doc/midi/report/p7_6.md). */
  mrb_fmrb_midi_sched_final(mrb);
}
