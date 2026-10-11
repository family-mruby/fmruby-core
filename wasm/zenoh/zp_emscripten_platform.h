/*
 * zp_emscripten_platform.h - zenoh-pico's platform types for the Family mruby
 * browser build, given to zenoh-pico as ZP_SYSTEM_PLATFORM_HEADER in place of
 * its include/zenoh-pico/system/platform/emscripten.h
 * (doc/ruby_asterism/report/b1.md).
 *
 * Two differences from that header:
 * - <stddef.h> is included. zenoh-pico 1.10.1's header includes only
 *   <stdint.h>, while the declarations after it in system/common/platform.h
 *   use size_t, so src/system/common/platform.c does not compile with it.
 * - The WebSocket endpoint is the URL (ws://host:port/) rather than an
 *   addrinfo: the link of this build (zp_ws_emscripten.c) opens a browser
 *   WebSocket itself instead of going through Emscripten's socket emulation.
 *   The socket is a slot number in that file's table.
 *
 * Single-threaded, WebSocket only (the configuration lib/add/
 * family_mruby_wasm.rb builds); anything else is refused here so the types
 * cannot silently disagree with the build.
 */
#ifndef ZP_EMSCRIPTEN_PLATFORM_H
#define ZP_EMSCRIPTEN_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "zenoh-pico/config.h"

#if Z_FEATURE_MULTI_THREAD == 1
#error "zp_emscripten_platform.h: single-threaded builds only"
#endif
#if Z_FEATURE_LINK_TCP == 1 || Z_FEATURE_LINK_TLS == 1 || Z_FEATURE_LINK_UDP_UNICAST == 1 || \
    Z_FEATURE_LINK_UDP_MULTICAST == 1 || Z_FEATURE_LINK_SERIAL == 1 || Z_FEATURE_LINK_BLUETOOTH == 1
#error "zp_emscripten_platform.h: the WebSocket link is the only one in a page"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef double z_clock_t;
typedef double z_time_t;

typedef struct {
    union {
#if Z_FEATURE_LINK_WS == 1
        struct {
            int _fd;        /* slot in zp_ws_emscripten.c's table, -1 when closed */
            uint32_t _tout; /* socket timeout given at open (ms) */
        } _ws;
#endif
    };
} _z_sys_net_socket_t;

typedef struct {
    union {
#if Z_FEATURE_LINK_WS == 1
        char *_url; /* "ws://host:port/", z_malloc'd */
#endif
    };
} _z_sys_net_endpoint_t;

#ifdef __cplusplus
}
#endif

#endif /* ZP_EMSCRIPTEN_PLATFORM_H */
