#define PICORB_PLATFORM_POSIX 1

#include "../../include/socket.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>

/* Prevent name collision with embedded Ruby bytecode */
#ifdef socket
#undef socket
#endif

/* Family mruby patch: default timeouts so a dead peer cannot stall the
 * cooperative mruby VM task forever. recv()/send() give up after
 * FMRB_SOCKET_IO_TIMEOUT_MS, connect() after FMRB_SOCKET_CONNECT_TIMEOUT_MS. */
#ifndef FMRB_SOCKET_IO_TIMEOUT_MS
#define FMRB_SOCKET_IO_TIMEOUT_MS      10000
#endif
#ifndef FMRB_SOCKET_CONNECT_TIMEOUT_MS
#define FMRB_SOCKET_CONNECT_TIMEOUT_MS 10000
#endif

/* The defaults above are a backstop against a dead peer; a caller that has a
 * deadline of its own wants a shorter one. TCPSocket.timeout_ms= sets both,
 * for sockets opened after it -- there is no per-socket handle to hang it on
 * before connect() has happened, and connect is exactly what needs bounding.
 * Only TCP is affected; UDP (SNTP) keeps its own behaviour. */
static int s_io_timeout_ms      = FMRB_SOCKET_IO_TIMEOUT_MS;
static int s_connect_timeout_ms = FMRB_SOCKET_CONNECT_TIMEOUT_MS;

void
TCPSocket_set_timeout_ms(int ms)
{
  if (ms <= 0) {
    s_io_timeout_ms = FMRB_SOCKET_IO_TIMEOUT_MS;
    s_connect_timeout_ms = FMRB_SOCKET_CONNECT_TIMEOUT_MS;
    return;
  }
  s_io_timeout_ms = ms;
  s_connect_timeout_ms = ms;
}

int
TCPSocket_get_timeout_ms(void)
{
  return s_io_timeout_ms;
}

/* Milliseconds from a clock that does not jump; used to bound the EINTR
 * retry loop below. */
static int64_t
monotonic_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
socket_set_default_timeouts(int fd)
{
  struct timeval tv;
  tv.tv_sec = s_io_timeout_ms / 1000;
  tv.tv_usec = (s_io_timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* connect() with a timeout: temporarily non-blocking + select() */
static int
socket_connect_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen,
                       int timeout_ms)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) flags = 0;
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int ret = connect(fd, addr, addrlen);
  if (ret < 0 && errno == EINPROGRESS) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ret = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (ret <= 0) {
      /* timeout (0) or select error (<0) */
      if (ret == 0) errno = ETIMEDOUT;
      ret = -1;
    }
    else {
      int so_error = 0;
      socklen_t len = sizeof(so_error);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
      if (so_error != 0) {
        errno = so_error;
        ret = -1;
      }
      else {
        ret = 0;
      }
    }
  }

  fcntl(fd, F_SETFL, flags);
  return ret;
}

bool
TCPSocket_create(picorb_state *vm, picorb_socket_t *sock)
{
  if (!sock) return false;

  memset(sock, 0, sizeof(picorb_socket_t));

  sock->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock->fd < 0) {
    return false;
  }

  socket_set_default_timeouts(sock->fd);

  sock->family = AF_INET;
  sock->socktype = SOCK_STREAM;
  sock->protocol = IPPROTO_TCP;
  sock->connected = false;
  sock->closed = false;

  return true;
}

bool
TCPSocket_connect(picorb_state *vm, picorb_socket_t *sock, const char *host, int port)
{
  if (!sock || !host || port <= 0 || port > 65535) {
    return false;
  }

  if (sock->family != AF_INET || sock->fd < 0) {
    if (!TCPSocket_create(vm, sock)) {
      return false;
    }
  }

  struct addrinfo hints;
  struct addrinfo *res = NULL;
  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int err = getaddrinfo(host, port_str, &hints, &res);
  if (err != 0 || !res) {
    snprintf(sock->errmsg, sizeof(sock->errmsg), "getaddrinfo(\"%s\"): %d", host, err);
    return false;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  memcpy(&addr.sin_addr, &((struct sockaddr_in *)res->ai_addr)->sin_addr, sizeof(struct in_addr));
  freeaddrinfo(res);

  if (socket_connect_timeout(sock->fd, (struct sockaddr *)&addr, sizeof(addr),
                             s_connect_timeout_ms) < 0) {
    snprintf(sock->errmsg, sizeof(sock->errmsg),
             "connect(\"%s\":%d): %s", host, port, strerror(errno));
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  strncpy(sock->remote_host, host, sizeof(sock->remote_host) - 1);
  sock->remote_host[sizeof(sock->remote_host) - 1] = '\0';
  sock->remote_port = port;
  sock->connected = true;

  return true;
}

ssize_t
TCPSocket_send(picorb_state *vm, picorb_socket_t *sock, const void *data, size_t len)
{
  if (!sock || !data || sock->fd < 0 || sock->closed) {
    return -1;
  }

  ssize_t sent = send(sock->fd, data, len, 0);
  if (sent < 0) {
    return -1;
  }

  return sent;
}

ssize_t
TCPSocket_recv(picorb_state *vm, picorb_socket_t *sock, void *buf, size_t len, bool nonblock)
{
  if (!sock || !buf || sock->fd < 0 || sock->closed) {
    return -1;
  }

  if (nonblock) {
    ssize_t received = recv(sock->fd, buf, len, MSG_DONTWAIT);
    if (received < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return PICORB_RECV_WOULD_BLOCK;
      }
      return -1;
    }
    if (received == 0) {
      sock->connected = false;
    }
    return received;
  }

  /* Return as soon as any data is available (readpartial semantics).
   *
   * Family mruby patch: retry on EINTR and report the timeout as a timeout.
   *
   * EINTR is not an edge case here. The FreeRTOS Linux port drives its tick
   * from a signal, so a blocking recv() on the simulator is interrupted
   * constantly; without the retry, any read that has to wait for the peer
   * fails outright, and the caller sees "read failed" a millisecond after
   * asking. (An HTTP client reading a response the server takes a few ms to
   * produce hits it every time.)
   *
   * And SO_RCVTIMEO expiring shows up as EAGAIN/EWOULDBLOCK on a blocking
   * socket. Mapping it to PICORB_RECV_TIMEOUT is what makes readpartial raise
   * "read timeout" rather than the generic "read failed" -- the distinction
   * the mruby layer already had a branch for, but that nothing could produce. */
  ssize_t received;
  const int64_t started_ms = monotonic_ms();
  while (1) {
    received = recv(sock->fd, buf, len, 0);
    if (received >= 0) break;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return PICORB_RECV_TIMEOUT;   /* SO_RCVTIMEO expired */
    }
    if (errno != EINTR) return -1;
    /* Retrying restarts SO_RCVTIMEO, so the socket's own timer can never
     * expire under a steady signal load -- the caller's timeout would mean
     * nothing. Keep our own clock across the retries. */
    if (monotonic_ms() - started_ms >= s_io_timeout_ms) {
      return PICORB_RECV_TIMEOUT;
    }
  }

  if (received == 0) {
    sock->connected = false;
  }

  return received;
}

/* Check if data is ready to read */
bool
Socket_ready(picorb_state *vm, picorb_socket_t *sock)
{
  if (!sock || sock->fd < 0 || sock->closed) {
    return false;
  }

  int available = 0;
  if (ioctl(sock->fd, FIONREAD, &available) < 0) {
    return false;
  }

  return available > 0;
}

bool
TCPSocket_close(picorb_state *vm, picorb_socket_t *sock)
{
  if (!sock || sock->fd < 0) {
    return false;
  }

  close(sock->fd);
  sock->fd = -1;
  sock->connected = false;
  sock->closed = true;

  return true;
}

const char*
TCPSocket_remote_host(picorb_state *vm, picorb_socket_t *sock)
{
  if (!sock) return NULL;
  return sock->remote_host;
}

int
TCPSocket_remote_port(picorb_state *vm, picorb_socket_t *sock)
{
  if (!sock) return -1;
  return sock->remote_port;
}

bool
TCPSocket_closed(picorb_state *vm, picorb_socket_t *sock)
{
  if (!sock) return true;
  return sock->closed || sock->fd < 0;
}
