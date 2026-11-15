/* launch_wasm.c - open an app by its path from the page (a driven page only).
 *
 * The sim and the native program have the debug server's spawn, the boards
 * devctl's /app/launch; the browser build had no such door, so a tool that
 * drove the page (tools/fmrb_web.rb, rake regress:screens) had to open an
 * app through the launcher by its position, which moved whenever an app was
 * added. This is that door: one slot in shared memory, written by the page
 * when it is driven (?drive=1, the `launch` command) and taken by the input
 * poller task, which calls fmrb_app_spawn_app as debugd and devctl do.
 *
 * The slot is a path, a request sequence the page bumps after writing it and
 * an answer (the sequence it answers, the pid and the error) the task writes
 * back. One request at a time: the page waits for the answer before the next.
 */
#include <stdint.h>
#include <string.h>

#include <emscripten/emscripten.h>

#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "launch_wasm.h"

static const char *TAG = "launch_wasm";

#define LAUNCH_PATH_MAX 256

static char s_path[LAUNCH_PATH_MAX];
static volatile uint32_t s_req_seq;    /* the page bumps it after writing s_path */
static volatile uint32_t s_done_seq;   /* the request last answered */
static volatile int32_t s_pid;
static volatile int32_t s_err;

EMSCRIPTEN_KEEPALIVE char *fmrb_wasm_launch_path(void) { return s_path; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_launch_cap(void) { return LAUNCH_PATH_MAX; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_launch_req_ptr(void) { return &s_req_seq; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_launch_done_ptr(void) { return &s_done_seq; }
EMSCRIPTEN_KEEPALIVE volatile int32_t *fmrb_wasm_launch_pid_ptr(void) { return &s_pid; }
EMSCRIPTEN_KEEPALIVE volatile int32_t *fmrb_wasm_launch_err_ptr(void) { return &s_err; }

void fmrb_wasm_launch_poll(void)
{
    uint32_t req = __atomic_load_n(&s_req_seq, __ATOMIC_SEQ_CST);
    if (req == __atomic_load_n(&s_done_seq, __ATOMIC_SEQ_CST)) return;

    char path[LAUNCH_PATH_MAX];
    memcpy(path, s_path, sizeof(path));
    path[sizeof(path) - 1] = '\0';

    int32_t pid = -1;
    fmrb_err_t err = path[0] ? fmrb_app_spawn_app(path, &pid) : FMRB_ERR_INVALID_PARAM;
    if (err != FMRB_OK) {
        FMRB_LOGW(TAG, "launch %s failed: %d", path, (int)err);
    } else {
        FMRB_LOGI(TAG, "launch %s: pid %d", path, (int)pid);
    }
    __atomic_store_n(&s_pid, pid, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s_err, (int32_t)err, __ATOMIC_SEQ_CST);
    __atomic_store_n(&s_done_seq, req, __ATOMIC_SEQ_CST);
}
