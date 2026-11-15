/*
 * The first wasm app written in C against the generated boundary header
 * (sdk/c/fmrb_boundary.h, doc/app_boundary W4a). It only calls
 * fmrb_gfx_fill_rect; the host plays the firmware and checks the canvas.
 * Built with: zig cc -target wasm32-freestanding (run.rb boundary).
 */
#include "fmrb_boundary.h"

static int32_t frame;

__attribute__((export_name("on_create"))) void on_create(void)
{
    fmrb_gfx_fill_rect(0, 0, 0, 64, 64, 0x00);
}

__attribute__((export_name("on_update"))) int32_t on_update(void)
{
    int32_t f = frame;
    for (int32_t i = 0; i < 8; i++) {
        int32_t x = (i * 7 + f * 3) & 63;
        int32_t y = (i * 13 + f * 5) & 63;
        int32_t c = (i * 31 + f * 17) & 255;
        fmrb_gfx_fill_rect(0, x, y, 1 + (i & 3), 1 + ((i >> 1) & 3), c);
    }
    frame = f + 1;
    return 16;
}
