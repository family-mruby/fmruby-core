/*
 * zenoh-pico build configuration for picoruby-zenoh.
 *
 * zenoh-pico normally generates include/zenoh-pico/config.h from config.h.in
 * with CMake. This gem compiles zenoh-pico with the mruby build instead, so it
 * defines ZENOH_GENERIC and config.h includes this file in place of the
 * CMake-generated block. Nothing under the zenoh-pico checkout is edited.
 *
 * Shape of the build:
 * - Single-threaded (Z_FEATURE_MULTI_THREAD=0): no read/lease tasks. The
 *   application drives the session with Zenoh::Session#poll, which runs
 *   zp_spin_once() a bounded number of times.
 * - Client mode, or peer mode (connect to peers and/or listen for them),
 *   over TCP unicast only. Serial, TLS, WebSocket, Bluetooth, raw ethernet,
 *   UDP and multicast scouting are compiled out. A listening peer accepts
 *   from the polled runtime too (a non-blocking accept, retried every
 *   second), so peer mode needs no task either.
 * - Publication, subscription, query (get), queryable and liveliness.
 * - No automatic reconnection: a lost router closes the session, and the
 *   application decides whether to open a new one.
 *
 * Every value can be overridden from the build (e.g. -DZ_FRAG_MAX_SIZE=2048).
 */
#ifndef PICORUBY_ZENOH_GENERIC_CONFIG_H
#define PICORUBY_ZENOH_GENERIC_CONFIG_H

/* ESP-IDF: platform types without the ESP-IDF headers, so the zenoh-pico core
 * can be compiled outside the ESP-IDF component (see the header). */
#if defined(ZENOH_ESPIDF) && !defined(ZP_SYSTEM_PLATFORM_HEADER)
#define ZP_SYSTEM_PLATFORM_HEADER "zenoh_espidf_platform.h"
#endif

/* Buffer sizes: zenoh-pico's defaults (an rx batch plus a fragment buffer). */
#ifndef Z_FRAG_MAX_SIZE
#define Z_FRAG_MAX_SIZE 4096
#endif
#ifndef Z_BATCH_UNICAST_SIZE
#define Z_BATCH_UNICAST_SIZE 2048
#endif
#ifndef Z_BATCH_MULTICAST_SIZE
#define Z_BATCH_MULTICAST_SIZE 2048
#endif

/* How long a blocking wait on the socket may last (milliseconds). Only the
 * session handshake waits; the polled read path never blocks (see
 * src/zp_tcp_posix.c). */
#ifndef Z_CONFIG_SOCKET_TIMEOUT
#define Z_CONFIG_SOCKET_TIMEOUT 100
#endif
#ifndef Z_TRANSPORT_LEASE
#define Z_TRANSPORT_LEASE 10000
#endif
#ifndef Z_TRANSPORT_LEASE_EXPIRE_FACTOR
#define Z_TRANSPORT_LEASE_EXPIRE_FACTOR 3
#endif
#ifndef Z_RUNTIME_MAX_TASKS
#define Z_RUNTIME_MAX_TASKS 16
#endif
/* 0: the read task stays runnable, so every poll looks at the socket. */
#ifndef Z_RUNTIME_IDLE_READ_TASK_SLEEP
#define Z_RUNTIME_IDLE_READ_TASK_SLEEP 0
#endif
#ifndef Z_TRANSPORT_ACCEPT_TIMEOUT
#define Z_TRANSPORT_ACCEPT_TIMEOUT 1000
#endif
#ifndef Z_TRANSPORT_CONNECT_TIMEOUT
#define Z_TRANSPORT_CONNECT_TIMEOUT 10000
#endif

#ifndef Z_FEATURE_CONNECTIVITY
#define Z_FEATURE_CONNECTIVITY 0
#endif
#ifndef Z_FEATURE_MULTI_THREAD
#define Z_FEATURE_MULTI_THREAD 0
#endif
#ifndef Z_FEATURE_PUBLICATION
#define Z_FEATURE_PUBLICATION 1
#endif
#ifndef Z_FEATURE_ADVANCED_PUBLICATION
#define Z_FEATURE_ADVANCED_PUBLICATION 0
#endif
#ifndef Z_FEATURE_SUBSCRIPTION
#define Z_FEATURE_SUBSCRIPTION 1
#endif
#ifndef Z_FEATURE_ADVANCED_SUBSCRIPTION
#define Z_FEATURE_ADVANCED_SUBSCRIPTION 0
#endif
#ifndef Z_FEATURE_QUERY
#define Z_FEATURE_QUERY 1
#endif
#ifndef Z_FEATURE_QUERYABLE
#define Z_FEATURE_QUERYABLE 1
#endif
#ifndef Z_FEATURE_LIVELINESS
#define Z_FEATURE_LIVELINESS 1
#endif
#ifndef Z_FEATURE_RAWETH_TRANSPORT
#define Z_FEATURE_RAWETH_TRANSPORT 0
#endif
#ifndef Z_FEATURE_INTEREST
#define Z_FEATURE_INTEREST 1
#endif
#ifndef Z_FEATURE_LINK_TCP
#define Z_FEATURE_LINK_TCP 1
#endif
#ifndef Z_FEATURE_LINK_BLUETOOTH
#define Z_FEATURE_LINK_BLUETOOTH 0
#endif
#ifndef Z_FEATURE_LINK_WS
#define Z_FEATURE_LINK_WS 0
#endif
#ifndef Z_FEATURE_LINK_SERIAL
#define Z_FEATURE_LINK_SERIAL 0
#endif
#ifndef Z_FEATURE_LINK_SERIAL_USB
#define Z_FEATURE_LINK_SERIAL_USB 0
#endif
#ifndef Z_FEATURE_LINK_TLS
#define Z_FEATURE_LINK_TLS 0
#endif
#ifndef Z_FEATURE_SCOUTING
#define Z_FEATURE_SCOUTING 0
#endif
#ifndef Z_FEATURE_LINK_UDP_MULTICAST
#define Z_FEATURE_LINK_UDP_MULTICAST 0
#endif
#ifndef Z_FEATURE_LINK_UDP_UNICAST
#define Z_FEATURE_LINK_UDP_UNICAST 0
#endif
#ifndef Z_FEATURE_MULTICAST_TRANSPORT
#define Z_FEATURE_MULTICAST_TRANSPORT 0
#endif
#ifndef Z_FEATURE_UNICAST_TRANSPORT
#define Z_FEATURE_UNICAST_TRANSPORT 1
#endif
#ifndef Z_FEATURE_FRAGMENTATION
#define Z_FEATURE_FRAGMENTATION 1
#endif
#ifndef Z_FEATURE_ENCODING_VALUES
#define Z_FEATURE_ENCODING_VALUES 1
#endif
#ifndef Z_FEATURE_TCP_NODELAY
#define Z_FEATURE_TCP_NODELAY 1
#endif
#ifndef Z_FEATURE_LOCAL_SUBSCRIBER
#define Z_FEATURE_LOCAL_SUBSCRIBER 0
#endif
#ifndef Z_FEATURE_LOCAL_QUERYABLE
#define Z_FEATURE_LOCAL_QUERYABLE 0
#endif
#ifndef Z_FEATURE_SESSION_CHECK
#define Z_FEATURE_SESSION_CHECK 1
#endif
#ifndef Z_FEATURE_BATCHING
#define Z_FEATURE_BATCHING 1
#endif
#ifndef Z_FEATURE_BATCH_TX_MUTEX
#define Z_FEATURE_BATCH_TX_MUTEX 0
#endif
#ifndef Z_FEATURE_BATCH_PEER_MUTEX
#define Z_FEATURE_BATCH_PEER_MUTEX 0
#endif
#ifndef Z_FEATURE_MATCHING
#define Z_FEATURE_MATCHING 0
#endif
#ifndef Z_FEATURE_RX_CACHE
#define Z_FEATURE_RX_CACHE 0
#endif
#ifndef Z_FEATURE_UNICAST_PEER
#define Z_FEATURE_UNICAST_PEER 1
#endif
#ifndef Z_FEATURE_AUTO_RECONNECT
#define Z_FEATURE_AUTO_RECONNECT 0
#endif
#ifndef Z_FEATURE_MULTICAST_DECLARATIONS
#define Z_FEATURE_MULTICAST_DECLARATIONS 0
#endif
#ifndef Z_FEATURE_ADMIN_SPACE
#define Z_FEATURE_ADMIN_SPACE 0
#endif

#endif /* PICORUBY_ZENOH_GENERIC_CONFIG_H */
