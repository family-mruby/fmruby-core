/*
 * zenoh-pico platform types for ESP-IDF, without the ESP-IDF headers.
 *
 * zenoh-pico's own ESP-IDF header (zenoh-pico/system/platform/espidf.h)
 * includes FreeRTOS and the UART driver. In a PicoRuby ESP-IDF build the
 * zenoh-pico core is compiled by the mruby build with the bare cross compiler,
 * which has no ESP-IDF include paths; only the platform files (system, TCP,
 * socket) are compiled by the ESP-IDF component. zenoh_generic_config.h points
 * ZP_SYSTEM_PLATFORM_HEADER here for ZENOH_ESPIDF, so both sides see these
 * same types.
 *
 * The layout matches espidf.h for this gem's configuration (single-threaded,
 * TCP only, no serial): a socket is an lwIP file descriptor and an endpoint
 * the getaddrinfo() result. Keep it in step with that header when zenoh-pico
 * is updated.
 */
#ifndef PICORUBY_ZENOH_ESPIDF_PLATFORM_H
#define PICORUBY_ZENOH_ESPIDF_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include "zenoh-pico/config.h"

#if Z_FEATURE_MULTI_THREAD == 1
#error "picoruby-zenoh: the ESP-IDF platform header supports the single-threaded build only"
#endif
#if Z_FEATURE_LINK_SERIAL == 1 || Z_FEATURE_LINK_BLUETOOTH == 1 || Z_FEATURE_RAWETH_TRANSPORT == 1 || \
    Z_FEATURE_LINK_TLS == 1
#error "picoruby-zenoh: the ESP-IDF platform header supports socket links (TCP/UDP) only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo;

typedef struct timespec z_clock_t;
typedef struct timeval z_time_t;

typedef struct {
    union {
#if defined(ZP_PLATFORM_SOCKET_LINKS_ENABLED)
        int _fd;
#endif
    };
} _z_sys_net_socket_t;

typedef struct {
    union {
#if defined(ZP_PLATFORM_SOCKET_LINKS_ENABLED)
        struct addrinfo *_iptcp;
#endif
    };
} _z_sys_net_endpoint_t;

#ifdef __cplusplus
}
#endif

#endif /* PICORUBY_ZENOH_ESPIDF_PLATFORM_H */
