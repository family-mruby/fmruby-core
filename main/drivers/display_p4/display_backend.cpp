/*
 * display_backend.cpp - which output backend this build uses
 *
 * Build-time, not runtime: the CPU backend exists to be flashed and looked at
 * (and, later, to be the thing wasm compiles), not to be switched into while
 * the machine is running. Select it with
 *
 *     FMRB_DISPLAY_BACKEND=cpu rake build:esp32
 *
 * which reaches here as FMRB_DISPLAY_BACKEND_CPU (see main/CMakeLists.txt).
 * The default is the PPA path the device has always used.
 */

#include "display_backend.h"

const display_backend_t *display_backend_ppa(void);
const display_backend_t *display_backend_cpu(void);

const display_backend_t *display_backend(void)
{
#ifdef FMRB_DISPLAY_BACKEND_CPU
    return display_backend_cpu();
#else
    return display_backend_ppa();
#endif
}
