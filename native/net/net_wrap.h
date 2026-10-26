/*
 * net_wrap.h - the native program's waiting network calls through the
 * blocking-call gate (net_wrap.c; doc/linux_native/report/l2n.md).
 *
 * The calls themselves need no setup: the link redirects them
 * (-Wl,--wrap, native/CMakeLists.txt). This starts the part that closes the
 * sockets a task deleted by force left open. Call after fmrb_gate_init.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void native_net_init(void);

#ifdef __cplusplus
}
#endif
