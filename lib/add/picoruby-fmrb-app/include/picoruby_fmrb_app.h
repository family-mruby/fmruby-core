#pragma once

#include <mruby.h>
#include <stdbool.h>

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb);
void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb);

// C API: Cleanup VM resources after script execution completes
void fmrb_app_vm_cleanup(mrb_state *mrb);

// Send one APU note on/off to the kernel on behalf of an app.
//
// Implemented in main/app/fmrb_app.c and declared here as well, because this
// header is read by the rake-side mruby build too, which has none of the
// firmware headers on its include path. The MIDI scheduler fires notes from a
// timer, where entering the VM is not allowed (doc/midi/report/p7_6.md); this
// touches nothing but a stack buffer and the kernel's message queue, so it is
// safe from any task. Pass timeout_ms 0 from a context that must not block.
//
// Returns an fmrb_err_t value (0 on success).
int fmrb_app_send_audio_note(int src_pid, bool on, int channel, int freq,
                             int volume, int duty, int sweep,
                             unsigned int timeout_ms);
