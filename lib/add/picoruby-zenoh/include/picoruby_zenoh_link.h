/*
 * The contract between picoruby-zenoh and its TCP links (src/zp_tcp_posix.c,
 * ports/esp32/zp_tcp_esp32.c), which replace zenoh-pico's own.
 *
 * zenoh-pico's client read path treats "the peer closed the connection" the
 * same as "nothing to read yet", so on its own a session only notices a lost
 * router when the lease expires or a send fails. The links therefore keep the
 * state of the connection in the socket itself (no extra memory): a send that
 * cannot finish within PICORUBY_ZENOH_SEND_TIMEOUT_MS shuts the socket down
 * (a half-written message would corrupt the stream anyway), and
 * zp_tcp_socket_closed() reports a socket whose peer has closed it, that has
 * failed, or that was shut down that way. The gem asks after every poll and
 * put, and closes the session when it is true.
 */
#ifndef PICORUBY_ZENOH_LINK_H
#define PICORUBY_ZENOH_LINK_H

#include <stdbool.h>

#include "zenoh-pico/system/platform.h"

/* How long connect() may take. */
#ifndef PICORUBY_ZENOH_CONNECT_TIMEOUT_MS
#define PICORUBY_ZENOH_CONNECT_TIMEOUT_MS 3000
#endif

/* How long one send may wait for room in the socket's send buffer, in total,
 * before the link gives up and shuts the socket down. */
#ifndef PICORUBY_ZENOH_SEND_TIMEOUT_MS
#define PICORUBY_ZENOH_SEND_TIMEOUT_MS 3000
#endif

/* A message that has started arriving during the handshake is given this
 * long to complete. */
#ifndef PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS
#define PICORUBY_ZENOH_READ_EXACT_TIMEOUT_MS 5000
#endif

/* True when the connection behind sock can no longer carry the session: the
 * peer closed it, the socket failed, or a send timed out. Never blocks. */
bool zp_tcp_socket_closed(const _z_sys_net_socket_t *sock);

#endif /* PICORUBY_ZENOH_LINK_H */
