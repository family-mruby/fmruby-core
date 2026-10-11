/*
 * zp_emscripten_wait.h - how the zenoh-pico Emscripten layer of this
 * repository waits (doc/ruby_asterism/report/b1.md).
 *
 * zenoh-pico's own Emscripten layer sleeps with emscripten_sleep(), which
 * needs ASYNCIFY: it unwinds the wasm stack so the browser's event loop can
 * run and deliver WebSocket messages. The Family mruby browser build does not
 * use ASYNCIFY. It runs every FreeRTOS task in its own Web Worker under
 * PROXY_TO_PTHREAD, and the WebSocket lives on the page's main thread, which
 * keeps running on its own and fills a ring in the shared memory as messages
 * arrive (zp_ws_emscripten.c). A task therefore never has to give the event
 * loop a turn; it only has to stop holding the (cooperative, single) CPU
 * while it waits, so the other tasks keep running.
 *
 * - In the firmware (FMRB_PLATFORM_WASM): vTaskDelay, i.e. a FreeRTOS wait,
 *   which on the cooperative wasm port is also the yield to other tasks.
 * - Elsewhere (the standalone probe, wasm/zenoh/probe/): usleep, which
 *   blocks only the calling Worker.
 */
#ifndef ZP_EMSCRIPTEN_WAIT_H
#define ZP_EMSCRIPTEN_WAIT_H

#include <stdint.h>

#if defined(FMRB_PLATFORM_WASM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void zp_ems_wait_ms(uint32_t ms) {
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay(t > 0 ? t : 1);
}
#else
#include <unistd.h>

static inline void zp_ems_wait_ms(uint32_t ms) { usleep((useconds_t)(ms > 0 ? ms : 1) * 1000u); }
#endif

#endif /* ZP_EMSCRIPTEN_WAIT_H */
