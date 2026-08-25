/*
 * SSL Socket implementation for esp32 using mbedtls
 *
 * Family mruby patch of ports/esp32/ssl_socket.c:
 * - Certificate verification uses the ESP-IDF certificate bundle
 *   (esp_crt_bundle, enabled in sdkconfig) by default, so HTTPS to public
 *   servers works out of the box. SSLContext#set_ca replaces the bundle
 *   with a user-supplied CA (self-signed servers).
 * - SSLSocket_ready no longer depends on the never-allocated base_socket;
 *   it checks buffered TLS bytes and polls the underlying fd.
 * - Read timeout (FMRB_SOCKET_IO_TIMEOUT_MS) so a silent peer cannot stall
 *   the mruby VM task forever.
 * - Failures are logged with the mbedtls error code.
 * Allocations use upstream's vm-threaded picorb_alloc(vm, ...): the old
 * picorb_alloc(NULL) crash is fixed upstream by passing the VM through the
 * ports API, so the former fmrb_sys_malloc replacement is gone.
 */

#include "../../include/socket.h"
#include "picoruby.h"

#include <fcntl.h>
#include <netdb.h>

/* mbedtls includes */
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <sys/select.h>

/* Set by TCPSocket.timeout_ms= (ports/esp32/tcp_socket.c); the TLS path
 * honours the same value so a caller's deadline covers both. */
extern int TCPSocket_get_timeout_ms(void);
#include <sys/time.h>

static const char *TAG = "ssl_socket";

#ifndef FMRB_SOCKET_IO_TIMEOUT_MS
#define FMRB_SOCKET_IO_TIMEOUT_MS 10000
#endif

/* SSL connection states */
#define SSL_STATE_NONE           0
#define SSL_STATE_CONNECTING     1
#define SSL_STATE_CONNECTED      2
#define SSL_STATE_ERROR          3

/* SSL Context structure */
struct picorb_ssl_context {
  mbedtls_ssl_config ssl_config;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_x509_crt cacert;
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  bool client_cert_loaded;
  bool client_key_loaded;
  bool crt_bundle_attached;
  int verify_mode;
};

/* SSL socket structure */
struct picorb_ssl_socket {
  picorb_socket_t *base_socket;   /* For buffer compatibility */
  picorb_ssl_context_t *ssl_ctx;
  mbedtls_net_context net_ctx;
  mbedtls_ssl_context ssl;
  int state;
  char *hostname;
  int port;
};

/* ========================================================================
 * SSLContext Functions
 * ======================================================================== */

picorb_ssl_context_t*
SSLContext_create(picorb_state *vm)
{
  picorb_ssl_context_t *ctx = (picorb_ssl_context_t *)picorb_alloc(vm, sizeof(picorb_ssl_context_t));
  if (!ctx) return NULL;

  mbedtls_ssl_config_init(&ctx->ssl_config);
  mbedtls_entropy_init(&ctx->entropy);
  mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
  mbedtls_x509_crt_init(&ctx->cacert);
  mbedtls_x509_crt_init(&ctx->cert);
  mbedtls_pk_init(&ctx->key);
  ctx->client_cert_loaded = false;
  ctx->client_key_loaded = false;
  ctx->crt_bundle_attached = false;
  ctx->verify_mode = SSL_VERIFY_PEER;

  /* Seed the random number generator */
  if (mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy, NULL, 0) != 0) {
    SSLContext_free(vm, ctx);
    return NULL;
  }

  /* Setup SSL/TLS configuration */
  if (mbedtls_ssl_config_defaults(&ctx->ssl_config,
                                MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    SSLContext_free(vm, ctx);
    return NULL;
  }

  mbedtls_ssl_conf_rng(&ctx->ssl_config, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

  /* Default to verifying peer certificate */
  mbedtls_ssl_conf_authmode(&ctx->ssl_config, MBEDTLS_SSL_VERIFY_REQUIRED);

  /* Verify against the ESP-IDF certificate bundle by default; replaced by
   * a user CA when SSLContext#set_ca is called. */
  if (esp_crt_bundle_attach(&ctx->ssl_config) == ESP_OK) {
    ctx->crt_bundle_attached = true;
  }
  else {
    ESP_LOGW(TAG, "esp_crt_bundle_attach failed; set a CA with SSLContext#set_ca");
  }

  /* Give up on a silent peer instead of blocking the VM task forever.
   * Requires the recv_timeout BIO installed in SSLSocket_connect.
   *
   * Follows TCPSocket.timeout_ms rather than the compiled-in default, so a
   * caller with a deadline gets it here too. Read at context-creation time,
   * which is when the caller has just set it. */
  mbedtls_ssl_conf_read_timeout(&ctx->ssl_config, (uint32_t)TCPSocket_get_timeout_ms());

  return ctx;
}

void
SSLContext_free(picorb_state *vm, picorb_ssl_context_t *ctx)
{
  if (!ctx) return;
  if (ctx->crt_bundle_attached) {
    esp_crt_bundle_detach(&ctx->ssl_config);
  }
  mbedtls_x509_crt_free(&ctx->cacert);
  mbedtls_x509_crt_free(&ctx->cert);
  mbedtls_pk_free(&ctx->key);
  mbedtls_ssl_config_free(&ctx->ssl_config);
  mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  mbedtls_entropy_free(&ctx->entropy);
  picorb_free(vm, ctx);
}

bool
SSLContext_set_ca_file(picorb_state *vm, picorb_ssl_context_t *ctx, const char *ca_file)
{
  (void)ctx;
  (void)ca_file;
  return false;  /* Not supported on ESP32 */
}

bool
SSLContext_set_ca(picorb_state *vm, picorb_ssl_context_t *ctx, const void *addr, size_t size)
{
  if (!ctx || !addr || size == 0) return false;

  int ret = mbedtls_x509_crt_parse(&ctx->cacert, (const unsigned char *)addr, size + 1);
  if (ret != 0) {
    ESP_LOGW(TAG, "x509_crt_parse failed: -0x%04x", -ret);
    return false;
  }
  /* A user CA replaces the default certificate bundle */
  if (ctx->crt_bundle_attached) {
    esp_crt_bundle_detach(&ctx->ssl_config);
    ctx->crt_bundle_attached = false;
  }
  mbedtls_ssl_conf_ca_chain(&ctx->ssl_config, &ctx->cacert, NULL);
  return true;
}

bool
SSLContext_set_cert_file(picorb_state *vm, picorb_ssl_context_t *ctx, const char *cert_file)
{
  (void)ctx;
  (void)cert_file;
  return false;  /* Not supported on ESP32 */
}

bool
SSLContext_set_cert(picorb_state *vm, picorb_ssl_context_t *ctx, const void *addr, size_t size)
{
  if (!ctx || !addr || size == 0) return false;

  int ret = mbedtls_x509_crt_parse(&ctx->cert, (const unsigned char *)addr, size + 1);
  if (ret != 0) {
    return false;
  }
  if (ctx->client_key_loaded) {
    ret = mbedtls_ssl_conf_own_cert(&ctx->ssl_config, &ctx->cert, &ctx->key);
    if (ret != 0) {
      return false;
    }
  }
  ctx->client_cert_loaded = true;
  return true;
}

bool
SSLContext_set_key_file(picorb_state *vm, picorb_ssl_context_t *ctx, const char *key_file)
{
  (void)ctx;
  (void)key_file;
  return false;  /* Not supported on ESP32 */
}

bool
SSLContext_set_key(picorb_state *vm, picorb_ssl_context_t *ctx, const void *addr, size_t size)
{
  if (!ctx || !addr || size == 0) return false;

  int ret = mbedtls_pk_parse_key(&ctx->key, (const unsigned char *)addr, size + 1, NULL, 0, NULL, NULL);
  if (ret != 0) {
    return false;
  }
  if (ctx->client_cert_loaded) {
    ret = mbedtls_ssl_conf_own_cert(&ctx->ssl_config, &ctx->cert, &ctx->key);
    if (ret != 0) {
      return false;
    }
  }
  ctx->client_key_loaded = true;
  return true;
}

bool
SSLContext_set_verify_mode(picorb_state *vm, picorb_ssl_context_t *ctx, int mode)
{
  if (!ctx) return false;
  int mbedtls_mode;
  switch (mode) {
    case SSL_VERIFY_NONE:
      mbedtls_mode = MBEDTLS_SSL_VERIFY_NONE;
      break;
    case SSL_VERIFY_PEER:
      mbedtls_mode = MBEDTLS_SSL_VERIFY_REQUIRED;
      break;
    default:
      return false;
  }
  mbedtls_ssl_conf_authmode(&ctx->ssl_config, mbedtls_mode);
  ctx->verify_mode = mode;
  return true;
}

int
SSLContext_get_verify_mode(picorb_state *vm, picorb_ssl_context_t *ctx)
{
  if (!ctx) return -1;
  return ctx->verify_mode;
}

/* ========================================================================
 * SSLSocket Functions
 * ======================================================================== */

picorb_ssl_socket_t*
SSLSocket_create(picorb_state *vm, picorb_ssl_context_t *ssl_ctx)
{
  if (!ssl_ctx) return NULL;

  picorb_ssl_socket_t *ssl_sock = (picorb_ssl_socket_t *)picorb_alloc(vm, sizeof(picorb_ssl_socket_t));
  if (!ssl_sock) return NULL;
  memset(ssl_sock, 0, sizeof(picorb_ssl_socket_t));

  ssl_sock->ssl_ctx = ssl_ctx;
  ssl_sock->state = SSL_STATE_NONE;

  mbedtls_net_init(&ssl_sock->net_ctx);
  mbedtls_ssl_init(&ssl_sock->ssl);

  return ssl_sock;
}

bool
SSLSocket_set_hostname(picorb_state *vm, picorb_ssl_socket_t *ssl_sock, const char *hostname)
{
  if (!ssl_sock || !hostname) return false;
  if (ssl_sock->hostname) picorb_free(vm, ssl_sock->hostname);
  ssl_sock->hostname = (char *)picorb_alloc(vm, strlen(hostname) + 1);
  if (!ssl_sock->hostname) return false;
  strcpy(ssl_sock->hostname, hostname);
  return true;
}

/* mbedtls_net_connect blocks for as long as the stack takes to give up -- a
 * minute or more on a host that is routable but silent, with the VM task held
 * the whole time. This is the same connect TCPSocket does (non-blocking +
 * select with the caller's timeout), writing the result into the mbedtls
 * context so everything after it is unchanged.
 *
 * Returns 0, or MBEDTLS_ERR_NET_CONNECT_FAILED so the caller's existing error
 * path reports it the way it always has. */
static int
ssl_net_connect_timeout(mbedtls_net_context *net_ctx, const char *host,
                        const char *port, int timeout_ms)
{
  struct addrinfo hints;
  struct addrinfo *list = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(host, port, &hints, &list) != 0 || !list) {
    return MBEDTLS_ERR_NET_UNKNOWN_HOST;
  }

  int rc = MBEDTLS_ERR_NET_CONNECT_FAILED;
  for (struct addrinfo *cur = list; cur != NULL; cur = cur->ai_next) {
    int fd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
    if (fd < 0) continue;

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int cr = connect(fd, cur->ai_addr, cur->ai_addrlen);
    if (cr < 0 && errno == EINPROGRESS) {
      fd_set wfds;
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);
      struct timeval tv;
      tv.tv_sec = timeout_ms / 1000;
      tv.tv_usec = (timeout_ms % 1000) * 1000;
      int sel = select(fd + 1, NULL, &wfds, NULL, &tv);
      if (sel > 0) {
        int so_error = 0;
        socklen_t elen = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &elen);
        cr = (so_error == 0) ? 0 : -1;
      }
      else {
        cr = -1;   /* timed out, or select failed */
      }
    }

    if (cr == 0) {
      fcntl(fd, F_SETFL, flags);   /* mbedtls expects it blocking */
      net_ctx->fd = fd;
      rc = 0;
      break;
    }
    close(fd);
  }

  freeaddrinfo(list);
  return rc;
}


bool
SSLSocket_set_port(picorb_state *vm, picorb_ssl_socket_t *ssl_sock, int port)
{
  if (!ssl_sock || port <= 0 || port > 65535) return false;
  ssl_sock->port = port;
  return true;
}

bool
SSLSocket_connect(picorb_state *vm, picorb_ssl_socket_t *ssl_sock)
{
  if (!ssl_sock || !ssl_sock->hostname || ssl_sock->state != SSL_STATE_NONE) return false;

  ssl_sock->state = SSL_STATE_CONNECTING;
  int ret;

  if (ssl_sock->port == 0) ssl_sock->port = 443;
  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", ssl_sock->port);

  /* 1. TCP Connect.
   *
   * mbedtls_net_connect blocks for however long the stack's own connect takes
   * to give up -- over a minute on an unreachable host, with the VM task held
   * the whole time. Bound it the same way TCPSocket does: connect on a
   * non-blocking socket, then wait for writability with the caller's timeout. */
  ret = ssl_net_connect_timeout(&ssl_sock->net_ctx, ssl_sock->hostname, port_str,
                                TCPSocket_get_timeout_ms());
  if (ret != 0) {
    ESP_LOGW(TAG, "net_connect(%s:%s) failed: -0x%04x", ssl_sock->hostname, port_str, -ret);
    ssl_sock->state = SSL_STATE_ERROR;
    return false;
  }

  /* 2. Setup SSL */
  ret = mbedtls_ssl_setup(&ssl_sock->ssl, &ssl_sock->ssl_ctx->ssl_config);
  if (ret != 0) {
    ESP_LOGW(TAG, "ssl_setup failed: -0x%04x", -ret);
    mbedtls_net_free(&ssl_sock->net_ctx);
    ssl_sock->state = SSL_STATE_ERROR;
    return false;
  }

  ret = mbedtls_ssl_set_hostname(&ssl_sock->ssl, ssl_sock->hostname);
  if (ret != 0) {
    mbedtls_net_free(&ssl_sock->net_ctx);
    ssl_sock->state = SSL_STATE_ERROR;
    return false;
  }

  /* recv_timeout BIO so mbedtls_ssl_conf_read_timeout takes effect */
  mbedtls_ssl_set_bio(&ssl_sock->ssl, &ssl_sock->net_ctx,
                      mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

  /* What a TLS session costs, said once per boot.
   *
   * mbedtls holds its buffers for the life of the connection, and on this
   * machine the caller is the service host -- one VM shared by everything
   * resident. "Can this machine afford an HTTPS request" is a question with a
   * number behind it, and the number was never written down. INFO because it
   * happens once; every later connection is silent. */
  static bool s_heap_reported = false;
  size_t heap_before = 0;
  if (!s_heap_reported) {
    heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  }

  /* 3. Handshake */
  while ((ret = mbedtls_ssl_handshake(&ssl_sock->ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      ESP_LOGW(TAG, "handshake with %s failed: -0x%04x (verify result: 0x%08x)",
               ssl_sock->hostname, -ret,
               (unsigned int)mbedtls_ssl_get_verify_result(&ssl_sock->ssl));
      mbedtls_net_free(&ssl_sock->net_ctx);
      ssl_sock->state = SSL_STATE_ERROR;
      return false;
    }
  }

  if (!s_heap_reported) {
    size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "TLS session to %s costs %d bytes internal (free %u -> %u)",
             ssl_sock->hostname, (int)(heap_before - heap_after),
             (unsigned)heap_before, (unsigned)heap_after);
    s_heap_reported = true;
  }

  ssl_sock->state = SSL_STATE_CONNECTED;
  return true;
}

ssize_t
SSLSocket_send(picorb_state *vm, picorb_ssl_socket_t *ssl_sock, const void *data, size_t len)
{
  if (!ssl_sock || ssl_sock->state != SSL_STATE_CONNECTED || !data) return -1;

  int ret = mbedtls_ssl_write(&ssl_sock->ssl, (const unsigned char *)data, len);
  if (ret < 0) {
    if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ) {
      return 0; // Would block
    }
    ssl_sock->state = SSL_STATE_ERROR;
    return -1;
  }
  return (ssize_t)ret;
}

ssize_t
SSLSocket_recv(picorb_state *vm, picorb_ssl_socket_t *ssl_sock, void *buf, size_t len, bool nonblock)
{
  if (!ssl_sock || ssl_sock->state != SSL_STATE_CONNECTED || !buf) return -1;

  if (nonblock) {
    int fd = ssl_sock->net_ctx.fd;
    int old_flags = fcntl(fd, F_GETFL, 0);
    if (old_flags == -1) return -1;
    if (fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) == -1) return -1;

    int ret = mbedtls_ssl_read(&ssl_sock->ssl, (unsigned char *)buf, len);

    if (fcntl(fd, F_SETFL, old_flags) == -1) {
      ssl_sock->state = SSL_STATE_ERROR;
      return -1;
    }

    if (ret > 0) return (ssize_t)ret;
    if (ret == 0) {
      ssl_sock->state = SSL_STATE_NONE;
      return 0;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      return PICORB_RECV_WOULD_BLOCK;
    }
    if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
      /* Read timeout: report an error but keep the session usable */
      ESP_LOGW(TAG, "recv timeout (%d ms)", FMRB_SOCKET_IO_TIMEOUT_MS);
      return -1;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      /* Clean TLS shutdown from the peer: report EOF, not an error.
       * Servers that close after a Connection: close response (e.g.
       * api.open-meteo.com) send close_notify; upstream treated it as an
       * error and every such request raised "SSL recv failed". */
      ssl_sock->state = SSL_STATE_NONE;
      return 0;
    }
    ESP_LOGW(TAG, "ssl_read failed: -0x%04x", -ret);
    ssl_sock->state = SSL_STATE_ERROR;
    return -1;
  }

  /* Blocking path */
  int ret;
  do {
    ret = mbedtls_ssl_read(&ssl_sock->ssl, (unsigned char *)buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      ssl_sock->state = SSL_STATE_NONE;
      return 0;
    }
    if (ret < 0) {
      ssl_sock->state = SSL_STATE_ERROR;
      return -1;
    }
  } while (ret < 0);
  if (ret == 0) {
    ssl_sock->state = SSL_STATE_NONE;
  }
  return (ssize_t)ret;
}

bool
SSLSocket_close(picorb_state *vm, picorb_ssl_socket_t *ssl_sock)
{
  if (!ssl_sock) return false;

  if (ssl_sock->state == SSL_STATE_CONNECTED) {
    mbedtls_ssl_close_notify(&ssl_sock->ssl);
  }
  mbedtls_net_free(&ssl_sock->net_ctx);
  mbedtls_ssl_free(&ssl_sock->ssl);

  if (ssl_sock->hostname) {
    picorb_free(vm, ssl_sock->hostname);
    ssl_sock->hostname = NULL;
  }

  picorb_free(vm, ssl_sock);
  return true;
}

bool
SSLSocket_closed(picorb_state *vm, picorb_ssl_socket_t *ssl_sock)
{
  return !ssl_sock || ssl_sock->state != SSL_STATE_CONNECTED;
}

bool
SSLSocket_ready(picorb_state *vm, picorb_ssl_socket_t *ssl_sock)
{
  if (!ssl_sock || ssl_sock->state != SSL_STATE_CONNECTED) {
    return false;
  }
  /* TLS records already decrypted and buffered? */
  if (mbedtls_ssl_get_bytes_avail(&ssl_sock->ssl) > 0) {
    return true;
  }
  /* Poll the underlying socket without blocking. The ESP-IDF mbedtls port
   * does not provide mbedtls_net_poll, so use select() via the VFS layer. */
  int fd = ssl_sock->net_ctx.fd;
  if (fd < 0) {
    return false;
  }
  fd_set rfds;
  struct timeval tv = {0, 0};
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  return select(fd + 1, &rfds, NULL, NULL, &tv) > 0;
}

const char*
SSLSocket_remote_host(picorb_state *vm, picorb_ssl_socket_t *ssl_sock)
{
  if (!ssl_sock) return NULL;
  return ssl_sock->hostname;
}

int
SSLSocket_remote_port(picorb_state *vm, picorb_ssl_socket_t *ssl_sock)
{
  if (!ssl_sock) return -1;
  return ssl_sock->port;
}
