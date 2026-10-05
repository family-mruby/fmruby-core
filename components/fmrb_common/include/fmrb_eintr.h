#pragma once

// EINTR handling for the POSIX builds (linux simulation, wasm).
//
// In the simulation the FreeRTOS POSIX port drives its tick with SIGALRM
// (1 kHz) and installs the handler without SA_RESTART. The tick is delivered
// to the thread of the running task, so a syscall that sleeps in the kernel
// -- even bind() -- can come back with EINTR instead of being restarted. The
// socket setup on these paths retries such calls with this macro, the same
// way fmruby-graphics-audio does (main/common/fmrb_eintr.h there).

#include <errno.h>

// Evaluate a syscall expression until it does not fail with EINTR.
#define FMRB_RETRY_EINTR(expr)                                   \
    ({                                                           \
        __typeof__(expr) fmrb_eintr_r_;                          \
        do {                                                     \
            fmrb_eintr_r_ = (expr);                              \
        } while (fmrb_eintr_r_ == -1 && errno == EINTR);         \
        fmrb_eintr_r_;                                           \
    })
