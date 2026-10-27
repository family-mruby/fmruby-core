/*
 * net_wrap.c - the waiting network calls of the native program, through the
 * blocking-call gate (hosted/gate/fmrb_gate.h; doc/linux_native/report/l2n.md).
 *
 * The networking gems (picoruby-socket's posix port, the OpenSSL TLS of its
 * SSLSocket, zenoh-pico's unix platform and the Zenoh gem's TCP link) are
 * written for an operating system that runs every thread at once: they call
 * recv, connect, getaddrinfo, SSL_connect and friends and wait in them. On
 * the cooperative port that wait would stop every task. The native link
 * wraps those calls (-Wl,--wrap=..., native/CMakeLists.txt), so every
 * reference to, say, recv in the program's own objects and in libmruby.a
 * lands in __wrap_recv here, and __real_recv is the C library's. Nothing in
 * the gems is changed; the simulation and the devices do not link this file.
 *
 * Each wrapper first decides whether the call can wait at all:
 *   - not on a FreeRTOS task (the main thread, a backend's thread, a gate
 *     helper itself), or the gate cannot take it: the call is made as is;
 *   - a descriptor in non-blocking mode, MSG_DONTWAIT, a zero timeout: it
 *     cannot wait, made as is;
 *   - otherwise it is tried without waiting first (recv with MSG_DONTWAIT,
 *     poll and select with a zero timeout, SSL_read with data already
 *     decrypted), and only a call that would really wait goes to a helper
 *     thread with its original arguments, while the task sleeps.
 * errno, and for TLS SSL_get_error, read the same afterwards as if the call
 * had been made on the task's own thread.
 *
 * Sockets are also remembered by the task that opened them (socket, accept;
 * forgotten on close), so that a task deleted by force -- an app that did
 * not answer a kill, whose VM is dropped without running its finalizers --
 * does not leave them open: the gate tells native_net_task_deleted, which
 * closes what is still recorded for that task.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "fmrb_gate.h"
#include "fmrb_log.h"
#include "net_wrap.h"

static const char *TAG = "net";

/* The C library's (and libssl's) own, reached through the linker. */
int __real_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                       struct addrinfo **res);
int __real_connect(int fd, const struct sockaddr *addr, socklen_t len);
int __real_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t __real_recv(int fd, void *buf, size_t len, int flags);
ssize_t __real_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from,
                        socklen_t *fromlen);
ssize_t __real_send(int fd, const void *buf, size_t len, int flags);
ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
                      socklen_t tolen);
int __real_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv);
int __real_poll(struct pollfd *fds, nfds_t n, int timeout);
int __real_SSL_connect(SSL *ssl);
int __real_SSL_read(SSL *ssl, void *buf, int num);
int __real_SSL_write(SSL *ssl, const void *buf, int num);
int __real_SSL_shutdown(SSL *ssl);
int __real_SSL_get_error(const SSL *ssl, int ret);
int __real_socket(int domain, int type, int protocol);
int __real_close(int fd);

/* ---------------------------------------------- sockets by their task */

/* Descriptors above this are not recorded (the program opens a few dozen). */
#define WRAP_MAX_FD 4096

/* Written on task threads only (one runs at a time) and cleared by
 * __wrap_close on whichever thread closes. The inode guards against a
 * number that was closed behind our back (by the C library itself) and
 * reused by something else. */
static struct {
    void *owner;
    ino_t ino;
} s_fds[WRAP_MAX_FD];

static void *current_task(void)
{
    return fmrb_gate_usable() ? (void *)xTaskGetCurrentTaskHandle() : NULL;
}

static void remember(int fd)
{
    void *task = current_task();
    struct stat st;
    if (fd < 0 || fd >= WRAP_MAX_FD || task == NULL || fstat(fd, &st) != 0) {
        return;
    }
    s_fds[fd].ino = st.st_ino;
    __atomic_store_n(&s_fds[fd].owner, task, __ATOMIC_RELEASE);
}

int __wrap_socket(int domain, int type, int protocol)
{
    int fd = __real_socket(domain, type, protocol);
    remember(fd);
    return fd;
}

int __wrap_close(int fd)
{
    if (fd >= 0 && fd < WRAP_MAX_FD) {
        __atomic_store_n(&s_fds[fd].owner, NULL, __ATOMIC_RELEASE);
    }
    return __real_close(fd);
}

static void task_deleted(void *task)
{
    unsigned closed = 0;
    for (int fd = 0; fd < WRAP_MAX_FD; fd++) {
        if (__atomic_load_n(&s_fds[fd].owner, __ATOMIC_ACQUIRE) != task) {
            continue;
        }
        struct stat st;
        s_fds[fd].owner = NULL;
        if (fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_ino == s_fds[fd].ino) {
            (void)shutdown(fd, SHUT_RDWR);
            (void)__real_close(fd);
            closed++;
        }
    }
    if (closed > 0) {
        FMRB_LOGW(TAG, "closed %u socket(s) a deleted task left open", closed);
    }
}

void native_net_init(void)
{
    fmrb_gate_add_task_deleted_listener(task_deleted);
}

/* ------------------------------------------------------------ helpers */

static bool fd_blocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    return fl >= 0 && (fl & O_NONBLOCK) == 0;
}

static bool would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

static fmrb_gate_job_t job_for(void (*run)(void *), void *arg, int fd)
{
    fmrb_gate_job_t job;
    memset(&job, 0, sizeof(job));
    job.run = run;
    job.arg = arg;
    if (fd >= 0) {
        job.fds[0] = fd;
        job.nfds = 1;
    }
    return job;
}

/* ------------------------------------------------------------ sockets */

typedef struct {
    int fd;
    void *buf;
    const void *cbuf;
    size_t len;
    int flags;
    struct sockaddr *addr;
    const struct sockaddr *caddr;
    socklen_t clen;
    socklen_t *plen;
    ssize_t ret;
    int err;
} sock_call_t;

static void run_recv(void *a)
{
    sock_call_t *c = a;
    c->ret = __real_recv(c->fd, c->buf, c->len, c->flags);
    c->err = errno;
}

static void run_recvfrom(void *a)
{
    sock_call_t *c = a;
    c->ret = __real_recvfrom(c->fd, c->buf, c->len, c->flags, c->addr, c->plen);
    c->err = errno;
}

static void run_send(void *a)
{
    sock_call_t *c = a;
    c->ret = __real_send(c->fd, c->cbuf, c->len, c->flags);
    c->err = errno;
}

static void run_sendto(void *a)
{
    sock_call_t *c = a;
    c->ret = __real_sendto(c->fd, c->cbuf, c->len, c->flags, c->caddr, c->clen);
    c->err = errno;
}

static void run_connect(void *a)
{
    sock_call_t *c = a;
    c->ret = __real_connect(c->fd, c->caddr, c->clen);
    c->err = errno;
}

static void run_accept(void *a)
{
    sock_call_t *c = a;
    c->ret = __real_accept(c->fd, c->addr, c->plen);
    c->err = errno;
}

/* Run c through the gate. false when the gate did not take it (the caller
 * makes the call itself). */
static bool gated(void (*run)(void *), sock_call_t *c)
{
    fmrb_gate_job_t job = job_for(run, c, c->fd);
    if (fmrb_gate_call(&job) != FMRB_GATE_DONE) {
        return false;  /* sock_call_t jobs are never abandoned */
    }
    errno = c->err;
    return true;
}

ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags)
{
    if (!fmrb_gate_usable() || (flags & MSG_DONTWAIT) || !fd_blocking(fd)) {
        return __real_recv(fd, buf, len, flags);
    }
    ssize_t n = __real_recv(fd, buf, len, flags | MSG_DONTWAIT);
    if (n >= 0 || !would_block()) {
        return n;
    }
    sock_call_t c = { .fd = fd, .buf = buf, .len = len, .flags = flags };
    if (!gated(run_recv, &c)) {
        return __real_recv(fd, buf, len, flags);
    }
    return c.ret;
}

ssize_t __wrap_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from,
                        socklen_t *fromlen)
{
    if (!fmrb_gate_usable() || (flags & MSG_DONTWAIT) || !fd_blocking(fd)) {
        return __real_recvfrom(fd, buf, len, flags, from, fromlen);
    }
    ssize_t n = __real_recvfrom(fd, buf, len, flags | MSG_DONTWAIT, from, fromlen);
    if (n >= 0 || !would_block()) {
        return n;
    }
    sock_call_t c = { .fd = fd, .buf = buf, .len = len, .flags = flags, .addr = from, .plen = fromlen };
    if (!gated(run_recvfrom, &c)) {
        return __real_recvfrom(fd, buf, len, flags, from, fromlen);
    }
    return c.ret;
}

/* A blocking send returns when all of it is queued (or on an error), and
 * callers count on that: what the first, non-waiting try could not queue is
 * sent through the gate. */
ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags)
{
    if (!fmrb_gate_usable() || (flags & MSG_DONTWAIT) || !fd_blocking(fd)) {
        return __real_send(fd, buf, len, flags);
    }
    ssize_t n = __real_send(fd, buf, len, flags | MSG_DONTWAIT);
    if (n < 0 && !would_block()) {
        return n;
    }
    size_t done = n > 0 ? (size_t)n : 0;
    if (done == len) {
        return (ssize_t)done;
    }
    sock_call_t c = { .fd = fd, .cbuf = (const char *)buf + done, .len = len - done, .flags = flags };
    if (!gated(run_send, &c)) {
        c.ret = __real_send(fd, c.cbuf, c.len, flags);
    }
    if (c.ret < 0) {
        return done > 0 ? (ssize_t)done : c.ret;
    }
    return (ssize_t)(done + (size_t)c.ret);
}

ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to,
                      socklen_t tolen)
{
    if (!fmrb_gate_usable() || (flags & MSG_DONTWAIT) || !fd_blocking(fd)) {
        return __real_sendto(fd, buf, len, flags, to, tolen);
    }
    ssize_t n = __real_sendto(fd, buf, len, flags | MSG_DONTWAIT, to, tolen);
    if (n >= 0 || !would_block()) {
        return n;
    }
    sock_call_t c = { .fd = fd, .cbuf = buf, .len = len, .flags = flags, .caddr = to, .clen = tolen };
    if (!gated(run_sendto, &c)) {
        return __real_sendto(fd, buf, len, flags, to, tolen);
    }
    return c.ret;
}

int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (!fmrb_gate_usable() || !fd_blocking(fd)) {
        return __real_connect(fd, addr, len);
    }
    sock_call_t c = { .fd = fd, .caddr = addr, .clen = len };
    if (!gated(run_connect, &c)) {
        return __real_connect(fd, addr, len);
    }
    return (int)c.ret;
}

static int accept_now(int fd, struct sockaddr *addr, socklen_t *len)
{
    if (!fmrb_gate_usable() || !fd_blocking(fd)) {
        return __real_accept(fd, addr, len);
    }
    struct pollfd p = { .fd = fd, .events = POLLIN };
    if (__real_poll(&p, 1, 0) > 0) {
        return __real_accept(fd, addr, len);
    }
    sock_call_t c = { .fd = fd, .addr = addr, .plen = len };
    if (!gated(run_accept, &c)) {
        return __real_accept(fd, addr, len);
    }
    return (int)c.ret;
}

int __wrap_accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    int s = accept_now(fd, addr, len);
    remember(s);
    return s;
}

/* ------------------------------------------------------ poll / select */

/* These wait on copies inside the gate (the job's scratch), so a cancelled
 * one can be left to time out on its own: a pipe does not wake on
 * shutdown. */
#define WRAP_POLL_MAX 16

typedef struct {
    struct pollfd fds[WRAP_POLL_MAX];
    nfds_t n;
    int timeout;
    int ret;
    int err;
} poll_call_t;

_Static_assert(sizeof(poll_call_t) <= FMRB_GATE_SCRATCH, "poll record fits the gate's scratch");

static void run_poll(void *a)
{
    poll_call_t *c = a;
    c->ret = __real_poll(c->fds, c->n, c->timeout);
    c->err = errno;
}

static void discard_nothing(void *a)
{
    (void)a;
}

int __wrap_poll(struct pollfd *fds, nfds_t n, int timeout)
{
    if (timeout == 0 || n > WRAP_POLL_MAX || !fmrb_gate_usable()) {
        return __real_poll(fds, n, timeout);
    }
    int ready = __real_poll(fds, n, 0);
    if (ready != 0) {
        return ready;
    }
    fmrb_gate_job_t job = job_for(run_poll, NULL, -1);
    job.discard = discard_nothing;
    poll_call_t *c = (poll_call_t *)job.scratch;
    memcpy(c->fds, fds, n * sizeof(*fds));
    c->n = n;
    c->timeout = timeout;
    for (nfds_t i = 0; i < n && job.nfds < FMRB_GATE_MAX_FDS; i++) {
        job.fds[job.nfds++] = fds[i].fd;
    }
    fmrb_gate_result_t r = fmrb_gate_call(&job);
    if (r == FMRB_GATE_UNAVAILABLE) {
        return __real_poll(fds, n, timeout);
    }
    if (r == FMRB_GATE_ABANDONED) {
        errno = EINTR;
        return -1;
    }
    for (nfds_t i = 0; i < n; i++) {
        fds[i].revents = c->fds[i].revents;
    }
    errno = c->err;
    return c->ret;
}

typedef struct {
    int nfds;
    fd_set r, w, e;
    bool has_r, has_w, has_e, has_tv;
    struct timeval tv;
    int ret;
    int err;
} select_call_t;

_Static_assert(sizeof(select_call_t) <= FMRB_GATE_SCRATCH, "select record fits the gate's scratch");

static void run_select(void *a)
{
    select_call_t *c = a;
    c->ret = __real_select(c->nfds, c->has_r ? &c->r : NULL, c->has_w ? &c->w : NULL,
                           c->has_e ? &c->e : NULL, c->has_tv ? &c->tv : NULL);
    c->err = errno;
}

int __wrap_select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    if ((tv != NULL && tv->tv_sec == 0 && tv->tv_usec == 0) || !fmrb_gate_usable() ||
        nfds < 0 || nfds > FD_SETSIZE) {
        return __real_select(nfds, r, w, e, tv);
    }
    /* Look first, without waiting, on copies (select overwrites the sets). */
    fd_set pr, pw, pe;
    if (r) pr = *r;
    if (w) pw = *w;
    if (e) pe = *e;
    struct timeval zero = { 0, 0 };
    int ready = __real_select(nfds, r ? &pr : NULL, w ? &pw : NULL, e ? &pe : NULL, &zero);
    if (ready != 0) {
        if (ready > 0) {
            if (r) *r = pr;
            if (w) *w = pw;
            if (e) *e = pe;
        }
        return ready;
    }
    fmrb_gate_job_t job = job_for(run_select, NULL, -1);
    job.discard = discard_nothing;
    select_call_t *c = (select_call_t *)job.scratch;
    memset(c, 0, sizeof(*c));
    c->nfds = nfds;
    c->has_r = r != NULL;
    c->has_w = w != NULL;
    c->has_e = e != NULL;
    c->has_tv = tv != NULL;
    if (r) c->r = *r;
    if (w) c->w = *w;
    if (e) c->e = *e;
    if (tv) c->tv = *tv;
    for (int fd = 0; fd < nfds && job.nfds < FMRB_GATE_MAX_FDS; fd++) {
        if ((r && FD_ISSET(fd, r)) || (w && FD_ISSET(fd, w)) || (e && FD_ISSET(fd, e))) {
            job.fds[job.nfds++] = fd;
        }
    }
    fmrb_gate_result_t res = fmrb_gate_call(&job);
    if (res == FMRB_GATE_UNAVAILABLE) {
        return __real_select(nfds, r, w, e, tv);
    }
    if (res == FMRB_GATE_ABANDONED) {
        errno = EINTR;
        return -1;
    }
    if (r) *r = c->r;
    if (w) *w = c->w;
    if (e) *e = c->e;
    if (tv) *tv = c->tv;
    errno = c->err;
    return c->ret;
}

/* ------------------------------------------------------ name lookup */

/* A lookup cannot be woken, so it is the one call that is abandoned when its
 * app ends: everything it reads and writes is in this record, inside the
 * gate. */
typedef struct {
    struct addrinfo hints;
    struct addrinfo *res;
    int ret;
    int err;
    bool has_hints;
    bool has_node;
    bool has_service;
    char node[1025];      /* NI_MAXHOST */
    char service[64];
} gai_call_t;

_Static_assert(sizeof(gai_call_t) <= FMRB_GATE_SCRATCH, "lookup record fits the gate's scratch");

static void run_getaddrinfo(void *a)
{
    gai_call_t *c = a;
    c->res = NULL;
    c->ret = __real_getaddrinfo(c->has_node ? c->node : NULL, c->has_service ? c->service : NULL,
                                c->has_hints ? &c->hints : NULL, &c->res);
    c->err = errno;
}

/* An abandoned lookup's answer (freeaddrinfo is the C library's free, not
 * the fmrb allocator, so it may run in the poll hook). */
static void discard_getaddrinfo(void *a)
{
    gai_call_t *c = a;
    if (c->ret == 0 && c->res != NULL) {
        freeaddrinfo(c->res);
    }
}

static bool numeric_host(const char *node)
{
    unsigned char buf[sizeof(struct in6_addr)];
    return node == NULL || inet_pton(AF_INET, node, buf) == 1 || inet_pton(AF_INET6, node, buf) == 1;
}

static bool copy_str(char *dst, size_t cap, const char *src, bool *has)
{
    *has = src != NULL;
    if (src == NULL) {
        return true;
    }
    size_t n = strlen(src);
    if (n >= cap) {
        return false;
    }
    memcpy(dst, src, n + 1);
    return true;
}

int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                       struct addrinfo **res)
{
    if (!fmrb_gate_usable() || numeric_host(node) ||
        (hints != NULL && (hints->ai_flags & AI_NUMERICHOST))) {
        return __real_getaddrinfo(node, service, hints, res);
    }
    fmrb_gate_job_t job = job_for(run_getaddrinfo, NULL, -1);
    job.discard = discard_getaddrinfo;
    gai_call_t *c = (gai_call_t *)job.scratch;
    memset(c, 0, sizeof(*c));
    if (!copy_str(c->node, sizeof(c->node), node, &c->has_node) ||
        !copy_str(c->service, sizeof(c->service), service, &c->has_service)) {
        return __real_getaddrinfo(node, service, hints, res);
    }
    if (hints != NULL) {
        /* Only the four fields a caller may set in hints. */
        c->hints.ai_flags = hints->ai_flags;
        c->hints.ai_family = hints->ai_family;
        c->hints.ai_socktype = hints->ai_socktype;
        c->hints.ai_protocol = hints->ai_protocol;
        c->has_hints = true;
    }
    fmrb_gate_result_t r = fmrb_gate_call(&job);
    if (r == FMRB_GATE_UNAVAILABLE) {
        return __real_getaddrinfo(node, service, hints, res);
    }
    if (r == FMRB_GATE_ABANDONED) {
        /* The app is ending; the lookup finishes on its own and is freed. */
        errno = EINTR;
        return EAI_SYSTEM;
    }
    *res = c->res;
    errno = c->err;
    return c->ret;
}

/* ------------------------------------------------------------- TLS */

/* SSL_get_error reads the calling thread's OpenSSL error queue, and a gated
 * call left its errors on the helper's. So the helper asks SSL_get_error
 * itself, and the answer is kept here for the task's next SSL_get_error on
 * the same SSL and return value. Every task is its own thread. */
static __thread struct {
    const SSL *ssl;
    int ret;
    int err;
    bool valid;
} t_ssl_last;

typedef enum { SSL_OP_CONNECT, SSL_OP_READ, SSL_OP_WRITE, SSL_OP_SHUTDOWN } ssl_op_t;

typedef struct {
    ssl_op_t op;
    SSL *ssl;
    void *buf;
    const void *cbuf;
    int num;
    int ret;
    int ssl_err;
    int err;
} ssl_call_t;

static void run_ssl(void *a)
{
    ssl_call_t *c = a;
    ERR_clear_error();
    errno = 0;
    switch (c->op) {
    case SSL_OP_CONNECT:  c->ret = __real_SSL_connect(c->ssl); break;
    case SSL_OP_READ:     c->ret = __real_SSL_read(c->ssl, c->buf, c->num); break;
    case SSL_OP_WRITE:    c->ret = __real_SSL_write(c->ssl, c->cbuf, c->num); break;
    case SSL_OP_SHUTDOWN: c->ret = __real_SSL_shutdown(c->ssl); break;
    }
    c->err = errno;
    c->ssl_err = __real_SSL_get_error(c->ssl, c->ret);
    if (c->ret <= 0 && ERR_peek_error() != 0) {
        /* The reason (a certificate that does not verify, say) would
         * otherwise be lost with this thread's queue. */
        ERR_print_errors_fp(stderr);
    }
}

static bool ssl_may_wait(SSL *ssl)
{
    if (!fmrb_gate_usable()) {
        return false;
    }
    int fd = SSL_get_fd(ssl);
    return fd >= 0 && fd_blocking(fd);
}

/* false when the gate did not take it (the caller makes the call itself). */
static bool ssl_gated(ssl_call_t *c)
{
    fmrb_gate_job_t job = job_for(run_ssl, c, SSL_get_fd(c->ssl));
    if (fmrb_gate_call(&job) != FMRB_GATE_DONE) {
        return false;
    }
    t_ssl_last.ssl = c->ssl;
    t_ssl_last.ret = c->ret;
    t_ssl_last.err = c->ssl_err;
    t_ssl_last.valid = true;
    errno = c->err;
    return true;
}

int __wrap_SSL_get_error(const SSL *ssl, int ret)
{
    if (t_ssl_last.valid && t_ssl_last.ssl == ssl && t_ssl_last.ret == ret) {
        return t_ssl_last.err;
    }
    return __real_SSL_get_error(ssl, ret);
}

static void ssl_forget(const SSL *ssl)
{
    if (t_ssl_last.ssl == ssl) {
        t_ssl_last.valid = false;
    }
}

int __wrap_SSL_connect(SSL *ssl)
{
    ssl_forget(ssl);
    if (!ssl_may_wait(ssl)) {
        return __real_SSL_connect(ssl);
    }
    ssl_call_t c = { .op = SSL_OP_CONNECT, .ssl = ssl };
    return ssl_gated(&c) ? c.ret : __real_SSL_connect(ssl);
}

int __wrap_SSL_read(SSL *ssl, void *buf, int num)
{
    ssl_forget(ssl);
    if (SSL_pending(ssl) > 0 || !ssl_may_wait(ssl)) {
        return __real_SSL_read(ssl, buf, num);
    }
    ssl_call_t c = { .op = SSL_OP_READ, .ssl = ssl, .buf = buf, .num = num };
    return ssl_gated(&c) ? c.ret : __real_SSL_read(ssl, buf, num);
}

int __wrap_SSL_write(SSL *ssl, const void *buf, int num)
{
    ssl_forget(ssl);
    if (!ssl_may_wait(ssl)) {
        return __real_SSL_write(ssl, buf, num);
    }
    ssl_call_t c = { .op = SSL_OP_WRITE, .ssl = ssl, .cbuf = buf, .num = num };
    return ssl_gated(&c) ? c.ret : __real_SSL_write(ssl, buf, num);
}

int __wrap_SSL_shutdown(SSL *ssl)
{
    ssl_forget(ssl);
    if (!ssl_may_wait(ssl)) {
        return __real_SSL_shutdown(ssl);
    }
    ssl_call_t c = { .op = SSL_OP_SHUTDOWN, .ssl = ssl };
    return ssl_gated(&c) ? c.ret : __real_SSL_shutdown(ssl);
}
