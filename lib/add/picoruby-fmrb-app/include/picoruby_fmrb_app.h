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

// Where the NSF on the audio side is (doc/nsf_player_v2), for the NSF player
// to move on at the end of a song. Implemented in main/app/fmrb_app.c; the
// fields mirror fmrb_audio_nsf_status_t (audio_commands.h), which this header
// cannot include for the same reason as above. A blocking round trip on the
// link: call it from an app task, not from the host task.
//
// Returns an fmrb_err_t value (0 on success; an error when the audio side did
// not answer, e.g. an older graphics-audio firmware).
typedef struct {
    int state;          // 0 idle, 1 playing, 2 fading out, 3 ended by itself
    int reason;         // 0 none, 1 silence, 2 loop, 3 time limit
    int track;          // 0-based
    int laps;           // loop laps heard so far (0: no loop found)
    int gen;            // play commands taken so far (wraps at 65536)
    int period_frames;  // the loop's period in 60 Hz frames (0: none)
    unsigned long frames;  // frames played since the song started
} fmrb_app_nsf_status_t;

int fmrb_app_audio_nsf_status(fmrb_app_nsf_status_t *status, unsigned int timeout_ms);
