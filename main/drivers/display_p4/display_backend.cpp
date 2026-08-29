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
const display_backend_t *display_backend_wasm(void);

const display_backend_t *display_backend(void)
{
#if defined(FMRB_PLATFORM_WASM)
    /* wasm/backend/display_backend_wasm.cpp: the shared software compositor
     * with an RGBA frame for the browser (doc/wasm/ P4a). */
    return display_backend_wasm();
#elif defined(FMRB_DISPLAY_BACKEND_CPU)
    return display_backend_cpu();
#else
    return display_backend_ppa();
#endif
}
