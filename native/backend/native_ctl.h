/*
 * native_ctl.h - the native program's control socket (<run dir>/ctl).
 *
 * A Unix stream socket the main thread polls between frames. One request per
 * connection, one line each, answered and closed:
 *
 *   shot   "OK <bytes> <w> <h>\n" then a PNG of the composited frame at its
 *          own size (426x240, or 640x360 in the fullscreen high-resolution
 *          mode) -- the RGB565 the display task made, not the scaled window
 *   info   "OK <w> <h> <frame seq> <pid> <machine>\n"
 *   stats  "OK gate calls=.. busy=.. orphans=.. helpers=.. idle=.. cancels=..
 *          deleted=.. burst_ms=..\n": the blocking-call gate's counters
 *          (hosted/gate/fmrb_gate.h) and the longest stretch without a task
 *          switch (port_hosted.h). "stats reset" answers the same and then
 *          starts that stretch over.
 *   clip   "OK joined=.. out=.. last_out=.. in=.. in_big=.. last_in=.. taken=..\n":
 *          the clipboard's hand-overs with Linux (native_clip.h, L12a)
 *   quit   "OK\n", then the program exits
 *
 * A request the output has words of its own for (the compositor's "comp
 * ...", L8) goes to the output's handler (native_ctl_set_output_handler).
 * Anything else gets "ERR ...\n". tools/fmrb_native.rb is the client.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool native_ctl_open(const char *path);
/* Serve whatever is waiting, without blocking. Returns true when a client
 * asked the program to quit. */
bool native_ctl_poll(void);
void native_ctl_close(void);

/* The output's own requests: answer one line into reply (with its "\n") and
 * return true, or return false when the request is not the output's. Called
 * on the main thread from native_ctl_poll. NULL for none. */
typedef bool (*native_ctl_output_fn)(const char *request, char *reply, size_t cap);
void native_ctl_set_output_handler(native_ctl_output_fn fn);

#ifdef __cplusplus
}
#endif
