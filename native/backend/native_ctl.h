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
 *   quit   "OK\n", then the program exits
 *
 * Anything else gets "ERR ...\n". tools/fmrb_native.rb is the client.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool native_ctl_open(const char *path);
/* Serve whatever is waiting, without blocking. Returns true when a client
 * asked the program to quit. */
bool native_ctl_poll(void);
void native_ctl_close(void);

#ifdef __cplusplus
}
#endif
