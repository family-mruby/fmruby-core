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

int display_scale_for(int panel_w, int panel_h, int fb_w, int fb_h)
{
    if (fb_w <= 0 || fb_h <= 0) return 1;
    int sx = panel_w / fb_w;
    int sy = panel_h / fb_h;
    int s = (sx < sy) ? sx : sy;
    return (s < 1) ? 1 : s;
}

void display_lcd_clear_outside(LGFX_Device *lcd, int x, int y, int w, int h)
{
    const int pw = lcd->width();
    const int ph = lcd->height();
    if (y > 0)          lcd->fillRect(0, 0, pw, y, 0);
    if (y + h < ph)     lcd->fillRect(0, y + h, pw, ph - (y + h), 0);
    if (x > 0)          lcd->fillRect(0, y, x, h, 0);
    if (x + w < pw)     lcd->fillRect(x + w, y, pw - (x + w), h, 0);
}
