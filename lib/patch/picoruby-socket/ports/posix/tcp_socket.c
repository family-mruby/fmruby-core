/*
 * Family mruby patch of ports/posix/tcp_socket.c: default socket timeouts,
 * matching the esp32 port. Upstream has none, so a dead peer blocks the
 * mruby VM for the OS default (minutes). connect() gives up after
 * FMRB_SOCKET_CONNECT_TIMEOUT_MS, recv()/send() after FMRB_SOCKET_IO_TIMEOUT_MS.
 *
 * All blocking calls retry on EINTR: the FreeRTOS Linux port drives the
 * scheduler with a 1 ms SIGALRM (ITIMER_REAL), which constantly interrupts
 * blocking syscalls in this process.
 */

#include "../../include/socket.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

/* Prevent name collision with embedded Ruby bytecode */
#ifdef socket
#undef socket
#endif

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
  /* EINTR on a non-blocking connect: the connection attempt continues in
   * the background, so treat it like EINPROGRESS and wait with select. */
  if (ret < 0 && (errno == EINPROGRESS || errno == EINTR)) {
    fd_set wfds;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    do {
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);
      /* On Linux, select() updates tv to the remaining time, so retrying
       * after EINTR keeps the total wait bounded by timeout_ms. */
      ret = select(fd + 1, NULL, &wfds, NULL, &tv);
    } while (ret < 0 && errno == EINTR);
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

/* Create a new TCP socket */
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

/* Connect to remote host */
bool
TCPSocket_connect(picorb_socket_t *sock, const char *host, int port)
{
  if (!sock || !host || port <= 0 || port > 65535) {
    return false;
  }

  /* Create socket if not already created */
  if (sock->fd < 0) {
    if (!TCPSocket_create(sock)) {
      return false;
    }
  }

  /* Resolve hostname */
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    /* Not an IP address, try DNS resolution */
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, NULL, &hints, &res);
    if (err != 0 || !res) {
      snprintf(sock->errmsg, sizeof(sock->errmsg),
               "getaddrinfo(\"%s\"): %s", host, gai_strerror(err));
      close(sock->fd);
      sock->fd = -1;
      return false;
    }

    struct sockaddr_in *ai_addr = (struct sockaddr_in *)res->ai_addr;
    addr.sin_addr = ai_addr->sin_addr;
    freeaddrinfo(res);
  }

  /* Connect */
  if (socket_connect_timeout(sock->fd, (struct sockaddr *)&addr, sizeof(addr),
                             FMRB_SOCKET_CONNECT_TIMEOUT_MS) < 0) {
    snprintf(sock->errmsg, sizeof(sock->errmsg),
             "connect(\"%s\":%d): %s", host, port, strerror(errno));
    close(sock->fd);
    sock->fd = -1;
    return false;
  }

  /* Save connection info */
  strncpy(sock->remote_host, host, sizeof(sock->remote_host) - 1);
  sock->remote_host[sizeof(sock->remote_host) - 1] = '\0';
  sock->remote_port = port;
  sock->connected = true;

  return true;
}

/* Send data */
ssize_t
TCPSocket_send(picorb_socket_t *sock, const void *data, size_t len)
{
  if (!sock || !data || sock->fd < 0 || sock->closed) {
    return -1;
  }

  ssize_t sent;
  do {
    sent = send(sock->fd, data, len, 0);
  } while (sent < 0 && errno == EINTR);
  if (sent < 0) {
    return -1;
  }

  return sent;
}

/* Receive data - blocks until len bytes are read or EOF/error.
 * MSG_WAITALL tells the kernel to wait until the full request is satisfied,
 * which avoids partial-read issues without requiring application-level loops
 * or setsockopt calls between recv() invocations. With SO_RCVTIMEO set, a
 * silent peer makes recv return the partial data (or -1/EAGAIN) after the
 * timeout instead of blocking forever. */
ssize_t
TCPSocket_recv(picorb_socket_t *sock, void *buf, size_t len)
{
  if (!sock || !buf || sock->fd < 0 || sock->closed) {
    return -1;
  }

#ifdef MSG_WAITALL
  /* EINTR with no data received returns -1; with partial data MSG_WAITALL
   * returns the partial count, which is fine for the callers. */
  ssize_t received;
  do {
    received = recv(sock->fd, buf, len, MSG_WAITALL);
  } while (received < 0 && errno == EINTR);
#else
  /* Fallback: loop until len bytes received or EOF/error. */
  size_t total = 0;
  char *p = (char *)buf;
  while (total < len) {
    ssize_t r = recv(sock->fd, p + total, len - total, 0);
    if (r < 0) {
      return -1;
    }
    if (r == 0) {
      sock->connected = false;
      return (ssize_t)total;
    }
    total += (size_t)r;
  }
  ssize_t received = (ssize_t)total;
#endif

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

/* Close socket */
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

/* Get remote host */
const char*
TCPSocket_remote_host(picorb_socket_t *sock)
{
  if (!sock) return NULL;
  return sock->remote_host;
}

/* Get remote port */
int
TCPSocket_remote_port(picorb_socket_t *sock)
{
  if (!sock) return -1;
  return sock->remote_port;
}

/* Check if socket is closed */
bool
TCPSocket_closed(picorb_socket_t *sock)
{
  if (!sock) return true;
  return sock->closed || sock->fd < 0;
}
