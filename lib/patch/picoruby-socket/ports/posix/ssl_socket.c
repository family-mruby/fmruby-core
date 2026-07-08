/*
 * SSL stub for the POSIX (Linux dev) build of picoruby-socket.
 *
 * Family mruby patch: the build container has no OpenSSL headers, so the
 * upstream OpenSSL-based implementation cannot be compiled there. Every
 * operation fails, which surfaces in Ruby as RuntimeError
 * ("failed to create SSL context"). Use the esp32p4 target (mbedTLS +
 * esp_crt_bundle) for TLS testing. Installing libssl-dev in the build
 * container and reverting this patch restores the real implementation.
 */

#include "../../include/socket.h"

#include <stddef.h>

picorb_ssl_context_t*
SSLContext_create(void)
{
  return NULL;
}

void
SSLContext_free(picorb_ssl_context_t *ctx)
{
  (void)ctx;
}

bool
SSLContext_set_ca_file(picorb_ssl_context_t *ctx, const char *ca_file)
{
  (void)ctx;
  (void)ca_file;
  return false;
}

bool
SSLContext_set_ca(picorb_ssl_context_t *ctx, const void *addr, size_t size)
{
  (void)ctx;
  (void)addr;
  (void)size;
  return false;
}

bool
SSLContext_set_cert_file(picorb_ssl_context_t *ctx, const char *cert_file)
{
  (void)ctx;
  (void)cert_file;
  return false;
}

bool
SSLContext_set_cert(picorb_ssl_context_t *ctx, const void *addr, size_t size)
{
  (void)ctx;
  (void)addr;
  (void)size;
  return false;
}

bool
SSLContext_set_key_file(picorb_ssl_context_t *ctx, const char *key_file)
{
  (void)ctx;
  (void)key_file;
  return false;
}

bool
SSLContext_set_key(picorb_ssl_context_t *ctx, const void *addr, size_t size)
{
  (void)ctx;
  (void)addr;
  (void)size;
  return false;
}

bool
SSLContext_set_verify_mode(picorb_ssl_context_t *ctx, int mode)
{
  (void)ctx;
  (void)mode;
  return false;
}

int
SSLContext_get_verify_mode(picorb_ssl_context_t *ctx)
{
  (void)ctx;
  return -1;
}

picorb_ssl_socket_t*
SSLSocket_create(picorb_ssl_context_t *ssl_ctx)
{
  (void)ssl_ctx;
  return NULL;
}

bool
SSLSocket_set_hostname(picorb_ssl_socket_t *ssl_sock, const char *hostname)
{
  (void)ssl_sock;
  (void)hostname;
  return false;
}

bool
SSLSocket_set_port(picorb_ssl_socket_t *ssl_sock, int port)
{
  (void)ssl_sock;
  (void)port;
  return false;
}

bool
SSLSocket_connect(picorb_ssl_socket_t *ssl_sock)
{
  (void)ssl_sock;
  return false;
}

ssize_t
SSLSocket_send(picorb_ssl_socket_t *ssl_sock, const void *data, size_t len)
{
  (void)ssl_sock;
  (void)data;
  (void)len;
  return -1;
}

ssize_t
SSLSocket_recv(picorb_ssl_socket_t *ssl_sock, void *buf, size_t len)
{
  (void)ssl_sock;
  (void)buf;
  (void)len;
  return -1;
}

bool
SSLSocket_close(picorb_ssl_socket_t *ssl_sock)
{
  (void)ssl_sock;
  return false;
}

bool
SSLSocket_closed(picorb_ssl_socket_t *ssl_sock)
{
  (void)ssl_sock;
  return true;
}

bool
SSLSocket_ready(picorb_ssl_socket_t *ssl_sock)
{
  (void)ssl_sock;
  return false;
}

const char*
SSLSocket_remote_host(picorb_ssl_socket_t *ssl_sock)
{
  (void)ssl_sock;
  return NULL;
}

int
SSLSocket_remote_port(picorb_ssl_socket_t *ssl_sock)
{
  (void)ssl_sock;
  return -1;
}
