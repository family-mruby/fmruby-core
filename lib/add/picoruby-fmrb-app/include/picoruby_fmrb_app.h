#pragma once

#include <mruby.h>
#include <stdbool.h>

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb);
void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb);

// C API: Cleanup VM resources after script execution completes
void fmrb_app_vm_cleanup(mrb_state *mrb);

// Send one APU note on/off to the kernel on behalf of an app.
//
// The same message FmrbAudio#note_on sends, built without a Ruby Hash (see
// the note in ports/esp32/app.c). Exported because the MIDI scheduler fires
// notes from a timer, where entering the VM is not allowed
// (doc/midi/report/p7_6.md): this touches nothing but a stack buffer and the
// kernel's message queue, so it is safe from any task. Pass timeout_ms 0
// from a context that must not block.
//
// Returns an fmrb_err_t value (0 on success). Spelled int because this
// header is also read by the rake-side mruby build, which has none of the
// firmware headers on its include path.
int fmrb_app_send_audio_note(int src_pid, bool on, int channel, int freq,
                             int volume, int duty, int sweep,
                             unsigned int timeout_ms);
