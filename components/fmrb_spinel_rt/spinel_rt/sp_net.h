/* sp_net.h -- POSIX TCP / poll / process / shell primitives for
 * single-host, HTTP-shaped Spinel runtimes.
 *
 * Pure C, no spinel-runtime dependency. Archived into libspinel_rt.a
 * alongside sp_crypto / sp_bigint -- a vendored, audit-sized helper
 * that ships with spinel so frameworks (tep, roundhouse, ...) build
 * their HTTP layer on top instead of fork-and-modifying. Same
 * precedent + spirit as sp_crypto (#514): small enough to read in one
 * sitting, no OpenSSL / libsodium. (TLS is deliberately NOT here --
 * an optional sp_net_tls unit will layer over these fds so the core
 * stays dependency-light; see matz/spinel#1054.)
 *
 * Conventions
 * -----------
 * - fd-based: every call takes/returns a raw socket fd; the framework
 *   owns lifetime.
 * - Static return buffers (per-function), single-threaded server model
 *   -- copy on the caller side if a value must outlive the next call,
 *   same as sp_crypto.
 * - String inputs are NUL-terminated.
 *
 * Naming: all exported symbols use the `sp_net_` prefix.
 */
#ifndef SP_NET_H
#define SP_NET_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- graceful shutdown (prefork servers) ----
 * install_term_handlers arms SIGTERM/SIGINT to set a flag (SA_RESETHAND
 * so a second signal kills immediately); shutdown_requested reads it.
 * sp_net_accept honors the flag so a blocking accept loop can break
 * cleanly. */
int sp_net_install_term_handlers(void);
int sp_net_shutdown_requested(void);

/* ---- TCP socket lifecycle ----
 * listen: bind+listen on `port` (INADDR_ANY); reuseport!=0 sets
 *   SO_REUSEPORT for kernel accept load-balancing across prefork
 *   workers. Disables Nagle + ignores SIGPIPE. Returns the listen fd.
 * accept: blocking accept; returns -1 if a term signal arrived
 *   (checked before blocking and on EINTR), else the connection fd.
 * accept_nb: non-blocking accept; -1 with errno EAGAIN/EWOULDBLOCK if
 *   nothing pending (listen fd must be sp_net_set_nonblock'd first).
 *   accept / accept_nb set TCP_NODELAY on the returned connection fd.
 * connect: outbound TCP to host:port via getaddrinfo (IP or DNS),
 *   Nagle off. Returns the connected fd or -1.
 * close: close(fd). set_nonblock: flip O_NONBLOCK on.
 * set_nodelay: disable Nagle on a connection fd (called for you by
 *   accept/accept_nb/connect; exposed for fds obtained elsewhere). */
int sp_net_listen(int port, int reuseport);
int sp_net_accept(int sfd);
int sp_net_accept_nb(int sfd);
int sp_net_connect(const char *host, int port);
int sp_net_close(int fd);
int sp_net_set_nonblock(int fd);
void sp_net_set_nodelay(int fd);

/* ---- TCP I/O ----
 * recv_some: up to maxlen bytes from one read. recv_all: read until
 * EOF or max_bytes. Both return a static, NUL-terminated buffer
 * (empty on error/EOF). write_str: write the full NUL-terminated
 * string; write_bytes: binary variant (explicit length, NUL-safe).
 * Return 0 on success, -1 on failure.
 *
 * Binary-safe recv: a NUL-terminated buffer can't carry an embedded
 * 0x00 (WebSocket frames do). Declaring recv_some/recv_all with the
 * FFI `:binstr` return mode builds the result String from the exact
 * byte count published in sp_net_bin_len instead of strlen, so binary
 * payloads survive. The same C functions serve both `:str` and
 * `:binstr` callers; sp_net_bin_len holds the last recv's byte count. */
extern int  sp_net_bin_len;
const char *sp_net_recv_some(int fd, int maxlen);
const char *sp_net_recv_all(int fd, int max_bytes);
int         sp_net_write_str(int fd, const char *s);
int         sp_net_write_bytes(int fd, const char *data, int n);

/* ---- poll(2) ----
 * reset clears the slot table; add registers (fd, mode_bits) where
 * 1=READ, 2=WRITE and returns the slot index (or -1 if full); run
 * blocks up to timeout_ms (-1 = forever, 0 = peek) and returns the
 * ready count; ready(slot) returns the mode bits that fired (POLLHUP/
 * POLLERR fold into READ). */
int sp_net_poll_reset(void);
int sp_net_poll_add(int fd, int mode_bits);
int sp_net_poll_run(int timeout_ms);
int sp_net_poll_ready(int slot);

/* ---- process (prefork) ----
 * fork: 0 in child, pid>0 in parent, -1 on failure. exit: _exit(status)
 * (never returns; int for FFI symmetry). getpid. wait_any: reap one
 * child, returns its pid or -1 when none remain. */
int sp_net_fork(void);
int sp_net_exit(int status);
int sp_net_getpid(void);
int sp_net_wait_any(void);

/* ---- shell ----
 * Run `cmd` via popen("r"), capture stdout up to max_bytes (capped at
 * the internal buffer). Returns a static NUL-terminated buffer. */
const char *sp_net_shell_capture(const char *cmd, int max_bytes);

#ifdef __cplusplus
}
#endif

#endif /* SP_NET_H */
