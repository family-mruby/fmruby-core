#include "../../include/socket.h"
#include "mruby/presym.h"
#include "mruby/data.h"

/* Socket type constants (matching socket.h) */
/* Family mruby patch: the ESP-IDF cross toolchain (bare-metal newlib) has no
 * <sys/socket.h>; the socket constants come from LwIP at runtime and match
 * the fallback values below, so use them when ESP32_PLATFORM is defined. */
#if defined(PICORB_PLATFORM_POSIX) && !defined(ESP32_PLATFORM)
#include <sys/socket.h>
#else
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#endif

const struct mrb_data_type mrb_socket_type = {
  "Socket", mrb_socket_free,
};

/* Data type for sockets (shared by TCP and UDP) */
void
mrb_socket_free(mrb_state *mrb, void *ptr)
{
  if (ptr) {
    picorb_socket_t *sock = (picorb_socket_t *)ptr;
    if (!sock->closed) {
      /* Close socket based on socket type */
      if (sock->socktype == SOCK_DGRAM) {
        UDPSocket_close(mrb, sock);
      } else {
        TCPSocket_close(mrb, sock);
      }
    }
    mrb_free(mrb, sock);
  }
}

/* Family mruby patch: TCP connect/recv/send timeouts, settable from Ruby.
 *
 * The port sets both from one value (each port's tcp_socket.c). It is a default
 * for sockets opened afterwards rather than a property of one socket, because
 * connect() is the call that most needs bounding and it happens inside
 * TCPSocket.new, before there is an object to configure. A caller with a
 * deadline sets it, does its work, and puts it back:
 *
 *   old = TCPSocket.timeout_ms
 *   TCPSocket.timeout_ms = 3000
 *   ...
 *   TCPSocket.timeout_ms = old
 *
 * Without this the only bound is the port's 10 s backstop, which is a long
 * time to hold a cooperative VM that runs other people's work (the service
 * host runs every service in turn). UDP is untouched. */
extern void TCPSocket_set_timeout_ms(int ms);
extern int TCPSocket_get_timeout_ms(void);

static mrb_value
mrb_tcp_socket_s_timeout_ms_set(mrb_state *mrb, mrb_value self)
{
  mrb_int ms;
  mrb_get_args(mrb, "i", &ms);
  TCPSocket_set_timeout_ms((int)ms);
  return mrb_fixnum_value(TCPSocket_get_timeout_ms());
}

static mrb_value
mrb_tcp_socket_s_timeout_ms_get(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(TCPSocket_get_timeout_ms());
}

/* Initialize gem */
void
mrb_picoruby_socket_gem_init(mrb_state *mrb)
{
  struct RClass *basic_socket_class;

  /* BasicSocket class */
  basic_socket_class = mrb_define_class_id(mrb, MRB_SYM(BasicSocket), mrb->object_class);

  /* Initialize each socket type */
  tcp_socket_init(mrb, basic_socket_class);
  udp_socket_init(mrb, basic_socket_class);
  tcp_server_init(mrb, basic_socket_class);
  ssl_socket_init(mrb, basic_socket_class);

  /* Registered here rather than in tcp_socket.c so the upstream file stays
     unpatched; the class exists by now. */
  struct RClass *tcp_socket_class = mrb_class_get_id(mrb, MRB_SYM(TCPSocket));
  mrb_define_class_method_id(mrb, tcp_socket_class, MRB_SYM_E(timeout_ms),
                             mrb_tcp_socket_s_timeout_ms_set, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, tcp_socket_class, MRB_SYM(timeout_ms),
                             mrb_tcp_socket_s_timeout_ms_get, MRB_ARGS_NONE());
}

void
mrb_picoruby_socket_gem_final(mrb_state *mrb)
{
  /* Nothing to do */
}
