#define PICORB_PLATFORM_POSIX 1

#include "../../include/socket.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <string.h>
#include <errno.h>
#include <fcntl.h>
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

static void
socket_set_default_timeouts(int fd)
{
  struct timeval tv;
  tv.tv_sec = FMRB_SOCKET_IO_TIMEOUT_MS / 1000;
  tv.tv_usec = (FMRB_SOCKET_IO_TIMEOUT_MS % 1000) * 1000;
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
TCPSocket_create(picorb_socket_t *sock)
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
TCPSocket_connect(picorb_socket_t *sock, const char *host, int port)
{
  if (!sock || !host || port <= 0 || port > 65535) {
    return false;
  }

  if (sock->family != AF_INET || sock->fd < 0) {
    if (!TCPSocket_create(sock)) {
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
                             FMRB_SOCKET_CONNECT_TIMEOUT_MS) < 0) {
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
TCPSocket_send(picorb_socket_t *sock, const void *data, size_t len)
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
TCPSocket_recv(picorb_socket_t *sock, void *buf, size_t len)
{
  if (!sock || !buf || sock->fd < 0 || sock->closed) {
    return -1;
  }

  ssize_t received = recv(sock->fd, buf, len, 0);
  if (received < 0) {
    return -1;
  }

  if (received == 0) {
    sock->connected = false;
  }

  return received;
}

/* Check if data is ready to read */
bool
Socket_ready(picorb_socket_t *sock)
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
TCPSocket_close(picorb_socket_t *sock)
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
TCPSocket_remote_host(picorb_socket_t *sock)
{
  if (!sock) return NULL;
  return sock->remote_host;
}

int
TCPSocket_remote_port(picorb_socket_t *sock)
{
  if (!sock) return -1;
  return sock->remote_port;
}

bool
TCPSocket_closed(picorb_socket_t *sock)
{
  if (!sock) return true;
  return sock->closed || sock->fd < 0;
}
