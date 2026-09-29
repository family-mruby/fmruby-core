/* sp_io.c -- File / IO handle ops in libspinel_rt.a.
 *
 * The allocation-free handle ops (open / pipe / fdopen / write / close /
 * closed? / puts / print / flush / eof?); the string-returning readers
 * (gets / read / read_n / path) stay inline in spinel_rt.h.
 *
 * Self-contained: includes sp_io.h (the sp_File layout) + sp_gc.h
 * (sp_mark_string), not spinel_rt.h.
 *
 * SP_MULTI_CTX: a file the program opens by path goes through the
 * per-instance I/O backend (sp_ctx io_* -> a host VFS, e.g. fmruby's HAL).
 * The backend's opaque handle is wrapped in a stdio stream with
 * fopencookie(3), so every handle op below -- gets, read, seek, eof, close --
 * stays the same stdio code as the default build. Pipes, sockets and the
 * standard streams keep their real descriptors. The default build is
 * unchanged. */
#if defined(SP_MULTI_CTX) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE   /* fopencookie, for the per-instance VFS backend below */
#endif
#include "sp_io.h"
#include "sp_gc.h"   /* sp_mark_string; also pulls sp_ctx.h for the VFS backend */
#include <stdlib.h>
#include <stddef.h>   /* offsetof, for the static-stream layout assertion */
#include <string.h>
#include <unistd.h>   /* pipe, isatty */
#include <sys/stat.h> /* stat() for the File predicates */
/* <sys/ioctl.h> provides TIOCGWINSZ for File#winsize. On bare-metal / RTOS
   newlib (ESP-IDF) the header may still EXIST as a stub that does not define
   TIOCGWINSZ or struct winsize, so header presence is not enough -- key the
   feature on the TIOCGWINSZ macro itself. Absent -> report a 0x0 winsize. */
#if defined(__has_include) && __has_include(<sys/ioctl.h>)
#  include <sys/ioctl.h>
#endif
#if defined(TIOCGWINSZ)
#  define SP_HAVE_IOCTL 1
#else
#  define SP_HAVE_IOCTL 0
#endif
#include <sys/socket.h> /* the Socket:: constants */
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/time.h> /* utimes() for File.utime */
#include <errno.h>
#include <fcntl.h>   /* fcntl flags for #close_on_exec?, #fcntl */
/* poll.h: POLLIN for the socket read park */
#if defined(__has_include)
#  if __has_include(<poll.h>)
#    include <poll.h>
#  else
#    include <sys/poll.h>  /* ESP-IDF newlib ships only the sys/ spelling */
#  endif
#else
#  include <poll.h>
#endif

/* Per-call "do not block" for send(2). Linux honours it; Darwin and the BSDs
   accept the flag and sleep anyway once the send buffer is full, which is why
   sp_sock_room below exists. Where the flag is missing entirely it is 0 and
   the room cap carries the socket on its own. */
#ifdef MSG_DONTWAIT
#define SP_MSG_DONTWAIT MSG_DONTWAIT
#else
#define SP_MSG_DONTWAIT 0
#endif

/* Bytes the send buffer can take right now, or -1 when the kernel will not
   say. Capping a send at this is what keeps a blocking socket's write out of
   the kernel's sleep: poll(POLLOUT) answers "there is room", not "there is
   room for all of n", so a 64 KB write into a nearly full buffer blocks even
   though the descriptor was reported writable. */
static long sp_sock_room(int fd) {
  int capacity = 0, queued = 0;
  socklen_t len = sizeof capacity;
  if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &capacity, &len) != 0 || capacity <= 0) return -1;
#if defined(SO_NWRITE)          /* Darwin: bytes not yet handed to the peer */
  len = sizeof queued;
  if (getsockopt(fd, SOL_SOCKET, SO_NWRITE, &queued, &len) != 0) return -1;
#elif defined(TIOCOUTQ)         /* Linux (SIOCOUTQ): same question, via ioctl */
  if (ioctl(fd, TIOCOUTQ, &queued) != 0) return -1;
#else
  return -1;
#endif
  long room = (long)capacity - (long)queued;
  return room > 0 ? room : 0;
}
#if !defined(__APPLE__) && !defined(__GLIBC__)
#include <stdio_ext.h>  /* musl __freadahead: pending stdio read-buffer bytes */
#endif

/* A path's stat(2): under SP_MULTI_CTX the instance's backend answers it
   (sp_vfs_stat), so a path is never looked up behind the backend's back. */
#ifdef SP_MULTI_CTX
#define SP_PATH_STAT(p, st) sp_vfs_stat((p), (st))
#else
#define SP_PATH_STAT(p, st) stat((p), (st))
#endif

/* Provided by the generated TU / libspinel_rt.a. */
extern void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *));
#ifndef SP_MULTI_CTX  /* T4-0: per-ctx macro under SP_MULTI_CTX */
extern SP_NORETURN void sp_raise_cls(const char *cls, const char *msg);
#endif
extern const char *sp_errno_class_name(int e);   /* lib/sp_exc.c: the Errno:: class for a C errno */
#ifndef SP_MULTI_CTX  /* T4-0: per-ctx macro under SP_MULTI_CTX */
extern const char *sp_sprintf(const char *fmt, ...);
#endif

/* The finalizer honors autoclose: an IO whose fd the program disowned
   (io.autoclose = false after wrapping the fd itself) flushes and abandons
   the FILE rather than closing the caller's descriptor -- the FILE structure
   leaks, which is the price of libc having no fdclose(3); the for_fd
   autoclose:false path wraps a dup(2) instead and never pays it (#4208). */
static void sp_File_fin(void *p) {
  sp_File *f = (sp_File *)p;
  if (!f->fp || f->closed) return;   /* closed: the FILE is the shared sentinel */
  if (f->no_autoclose && !f->fno_plus1) { fflush(f->fp); f->fp = NULL; return; }
  fclose(f->fp); f->fp = NULL;
}
static void sp_File_scan(void *p) { sp_File *f = (sp_File *)p; if (f->path) sp_mark_string(f->path); if (f->mode) sp_mark_string(f->mode); }

/* The two-argument form is the permission form with CRuby's default bits:
   the same mode scan (which refuses "rx"), O_CLOEXEC, and the errno-named
   error, rather than fopen's looser mode and a flat ENOENT. */
sp_File *sp_File_open(const char *path, const char *mode) {
  return sp_File_open_perm(path, mode ? mode : "r", SP_INT_NIL);
}

/* Returns 0 on success, -1 on error. */
int sp_io_make_pipe(int fds[2]) {
  return pipe(fds);
}

/* IO.pipe end: wrap a raw pipe fd in a GC-managed sp_File so the
   sp_File_* I/O ops work on it. Same finalizer/scan as sp_File_open. */
/* owns_fd: the descriptor is this handle's to release when fdopen fails --
   true for one open(2) or pipe(2) just made, false for IO.for_fd, whose fd
   belongs to the program (closing its stdout on a bad mode lost the output). */
sp_File *sp_io_fdopen_ex(int fd, const char *mode, int owns_fd) {SP_GC_ROOT_STR(mode);
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
  f->fp = fdopen(fd, mode ? mode : "r");
  if (!f->fp) {
    if (owns_fd) close(fd);
    sp_raise_cls("IOError", "fdopen failed");
    return NULL;
  }
  f->path = NULL;
  f->mode = mode;
  f->lineno = 0;
  return f;
}
sp_File *sp_io_fdopen(int fd, const char *mode) { return sp_io_fdopen_ex(fd, mode, 1); }

/* Wrap a socket fd (#2922). The FILE* serves the buffered READ side (gets and
   friends need lookahead); every write bypasses stdio straight to write(2) --
   see sp_sock_write below -- so a response is on the wire immediately, like
   CRuby sockets' sync = true default. `kind` lands in ->mode for #class. */
sp_File *sp_io_fdopen_sock(int fd, const char *kind) {SP_GC_ROOT_STR(kind);
  if (fd < 0) sp_raise_cls("Errno::ECONNREFUSED", "Connection refused");
  /* The scheduler-aware accept/connect helpers may hand us a non-blocking
     fd; the stdio read side must BLOCK (fgets treats EAGAIN as EOF), so
     clear O_NONBLOCK. Green threads still yield: every read entry point
     parks on sp_io_wait_readable below before touching the stream. */
  { int fl = fcntl(fd, F_GETFL); if (fl >= 0) fcntl(fd, F_SETFL, fl & ~O_NONBLOCK); }
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
  f->fp = fdopen(fd, "r+");
  if (!f->fp) { sp_raise_cls("IOError", "fdopen failed"); return NULL; }
  f->path = NULL;
  f->mode = kind;
  f->lineno = 0;
  f->is_sock = 1;
  return f;
}

/* Park the calling green thread until the handle has readable data, so a
   blocking fgets/fread does not pin its worker while the peer is idle. A
   no-op when stdio already buffered data (waiting then would stall on the
   WIRE while the answer sits in the buffer), and in the single-threaded
   build.

   WHICH handles: the ones whose read can block indefinitely. This was the
   socket path and asked `is_sock`, so a PIPE went straight into fread and
   blocked its OS worker in the kernel -- and the thread that would write to
   that pipe is often a green thread on the very same worker, so it had
   nowhere to run: two threads passing a byte back and forth never completed
   on SPINEL_WORKERS=1 (#4307). A character device (a tty, /dev/urandom) is
   the same shape. A regular file is always ready and pays only the one
   fstat, once per handle. */
/* Bytes stdio has already buffered for this stream. They are readable without
   a read(2), so a drain of exactly this many cannot block -- which is what
   lets a slurp fill from a pipe without ever sitting in the kernel. */
size_t sp_io_stdio_buffered(FILE *fp) {
  if (!fp) return 0;
#if defined(__GLIBC__)
  return fp->_IO_read_end > fp->_IO_read_ptr ? (size_t)(fp->_IO_read_end - fp->_IO_read_ptr) : 0;
#elif defined(__APPLE__) || defined(__NEWLIB__)
  return fp->_r > 0 ? (size_t)fp->_r : 0;   /* BSD stdio; newlib shares it */
#else
  return __freadahead(fp);   /* musl */
#endif
}

/* Can this handle's I/O block indefinitely? Asked once per handle, from an
   fstat on first use: a FIFO, a socket and a character device can; a regular
   file is always ready. */
static int sp_io_parkable(sp_File *f) {
  if (!f->park) {
    struct stat st;
    int pfd = fileno(f->fp);
    f->park = (f->is_sock ||
               (pfd >= 0 && fstat(pfd, &st) == 0 &&
                (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode) || S_ISCHR(st.st_mode)))) ? 2 : 1;
  }
  return f->park == 2;
}

/* Park until the descriptor can take bytes. A write into a full pipe or socket
   buffer sits in the kernel holding the OS worker, and the READER that would
   drain it is often a green thread on that same worker -- so it never runs and
   the two deadlock, the write-side twin of the read park. Only for the handles
   whose peer is that reader: a socket, and the write end of an IO.pipe (which
   spinel marks sync and leaves unbuffered). A file and the standard streams
   keep their plain path, and their write cost with it. */
void sp_io_wait_writable(sp_File *f) {SP_GC_ROOT(f);
  if (!f || !f->fp || !sp_io_parkable(f)) return;
  int wfd = fileno(f->fp);
  if (wfd < 0) return;
  { struct pollfd wp; wp.fd = wfd; wp.events = POLLOUT; wp.revents = 0;
    if (poll(&wp, 1, 0) > 0) return; }   /* room already: no park */
#ifdef SP_THREADS
  {
    extern int sp_sched_wait_io_unless(int fd, short events, const unsigned char *cancel);
    sp_sched_wait_io_unless(wfd, POLLOUT, &f->closed);
    if (f->closed) sp_raise_cls("IOError", "stream closed in another thread");   /* see sp_io_wait_readable */
  }
#else
  {
    extern void sp_Thread_pass(void);
    struct pollfd p;
    p.fd = wfd; p.events = POLLOUT;
    for (;;) {
      p.revents = 0;
      int r = poll(&p, 1, 1);
      if (r > 0 || (r < 0 && errno != EINTR && errno != EAGAIN)) return;
      sp_Thread_pass();
    }
  }
#endif
}

/* The park on a bare descriptor. `closed` is the owning handle's closed flag
   when there is one: a close from another thread between the poll below and
   the park's registration is then seen under the scheduler lock, instead of
   the registration going in after the close dropped it (a read with no
   deadline waited forever, one macOS run in three of the #4546 test). */
static void sp_io_park_fd_readable(int fd, const unsigned char *closed) {
  if (fd < 0) return;
  /* Already readable: answer from one poll rather than a park. The park is a
     monitor round trip -- register the waiter, write the self-pipe to break
     the monitor out of ITS poll, wait to be requeued and rescheduled -- and
     paying that when the bytes are already there is most of the cost in the
     shape that reads right after an IO.select says the fd is ready. */
  { struct pollfd rp; rp.fd = fd; rp.events = POLLIN; rp.revents = 0;
    if (poll(&rp, 1, 0) > 0) return; }
#ifdef SP_THREADS
  {
    extern int sp_sched_wait_io_unless(int fd, short events, const unsigned char *cancel);
    sp_sched_wait_io_unless(fd, POLLIN, closed);
  }
#else
  (void)closed;
  /* Cooperative build: a blocking read here would stall EVERY green thread
     (the peer that will produce our data included). Poll with a short
     timeout and hand the scheduler to the other threads until readable. */
  {
    extern void sp_Thread_pass(void);
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    for (;;) {
      p.revents = 0;
      int r = poll(&p, 1, 1);
      if (r > 0 || (r < 0 && errno != EINTR && errno != EAGAIN)) return;
      sp_Thread_pass();
    }
  }
#endif
}
/* The descriptor form, for a pipe the runtime holds without a handle (a
   backtick's child): the same park, on the bare fd. */
void sp_io_wait_fd_readable(int fd) { sp_io_park_fd_readable(fd, NULL); }
void sp_io_wait_readable(sp_File *f) {SP_GC_ROOT(f);
  if (!f || !f->fp) return;
  if (!sp_io_parkable(f)) return;
  if (sp_io_stdio_buffered(f->fp) > 0) return;
  sp_io_park_fd_readable(fileno(f->fp), &f->closed);
  /* woken by a close from another thread: the handle is gone, and a read
     would touch a descriptor that is closed or already someone else's */
  if (f->closed) sp_raise_cls("IOError", "stream closed in another thread");
}

/* The socket write path: straight to the descriptor, looping over short
   writes. The stdio stream is never written through, so there is nothing to
   flush and no read/write switching hazard on the shared FILE*. */
static sp_int sp_sock_write(sp_File *f, const char *s, size_t n) {
  int fd = fileno(f->fp);
  size_t off = 0;
  /* A pipe's WRITE end is write-only -- nothing reads through its stream -- so
     it can be O_NONBLOCK, and then a write that would block says so instead of
     blocking. That is the cheap shape: no probe on the way in, and the park
     only on the attempt that actually could not proceed. A socket shares its
     descriptor with the stdio READ side, where fgets treats EAGAIN as EOF
     (see sp_io_fdopen_sock), so it cannot carry O_NONBLOCK -- it takes
     MSG_DONTWAIT per send(2) instead, which says the same thing for this one
     call and leaves the descriptor's flags, and the read side, untouched.
     The probe alone was not enough: it answers "there is room", not "there is
     room for all of n", so a 64 KB write into a nearly full buffer still slept
     in the kernel with the collector waiting on this worker (the #4528 shape,
     on the write side). */
  int nb = 0;
  if (!f->is_sock) {
    if (!f->wnonblock) {
      int fl = fcntl(fd, F_GETFL);
      f->wnonblock = (fl >= 0 && (fl & O_NONBLOCK ? 1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0)) ? 1 : 2;
    }
    nb = f->wnonblock == 1;
  }
  while (off < n) {
    ssize_t put;
    size_t want = n - off;
    if (!nb) sp_io_wait_writable(f);   /* frees the worker while the buffer is full */
    if (f->is_sock) {
      /* A datagram or seqpacket send is one message: shortening it would
         deliver two where the caller wrote one, and would hide the EMSGSIZE
         the kernel owes an over-long payload. Only a byte stream is capped. */
      if (!f->wstream) {
        int sotype = 0; socklen_t solen = sizeof sotype;
        f->wstream = (getsockopt(fd, SOL_SOCKET, SO_TYPE, &sotype, &solen) == 0 &&
                      sotype == SOCK_STREAM) ? 1 : 2;
      }
      /* Linux honours MSG_DONTWAIT: the send returns what fit, or EAGAIN,
         and the park below takes it from there, so the two extra syscalls
         of the room query are not paid on the hot path there. Darwin and
         the BSDs sleep despite the flag, and the query is what keeps them
         out of the kernel. */
#if defined(__linux__)
      long room = -1;
#else
      long room = f->wstream == 1 ? sp_sock_room(fd) : -1;
#endif
      /* No room: go back to the park above, which is where waiting belongs.
         It cannot spin -- POLLOUT is raised only once the free space reaches
         SO_SNDLOWAT (2048 by default), so a buffer this full does not report
         the descriptor writable and the park really parks. */
      if (room == 0) continue;
      if (room > 0 && (size_t)room < want) want = (size_t)room;
      put = send(fd, s + off, want, SP_MSG_DONTWAIT);
    } else {
      put = write(fd, s + off, want);
    }
    if (put < 0) {
      if (errno == EINTR) continue;
      if ((nb || f->is_sock) && (errno == EAGAIN || errno == EWOULDBLOCK)) { sp_io_wait_writable(f); continue; }
      sp_file_raise_errno("write", f->is_sock ? "socket" : "pipe");
    }
    off += (size_t)put;
  }
  return (sp_int)n;
}

/* Direct write to a regular file's descriptor, bypassing the stdio
   buffer. IO#syswrite is unbuffered: the bytes must reach the file
   before the next call returns, so there is nothing to flush and no
   read/write switching hazard on the shared FILE*. Sockets and sync
   parkable handles keep sp_sock_write, which already writes straight
   to the descriptor. */
static sp_int sp_io_write_raw(sp_File *f, const char *s, size_t n) {
  int fd = fileno(f->fp);
  size_t off = 0;
  /* Flush whatever stdio still holds: a previous buffered write would
     otherwise land after this direct write and reorder the bytes. A
     flush failure is the same error path as a write failure. */
  if (fflush(f->fp) != 0) sp_file_raise_errno("write", "file");
  while (off < n) {
    ssize_t put = write(fd, s + off, n - off);
    if (put < 0) {
      if (errno == EINTR) continue;
      sp_file_raise_errno("write", "file");
    }
    off += (size_t)put;
  }
  /* Sync the FILE* position with the descriptor: the raw write loop moved
     the descriptor past where stdio thinks it is, and a zero-length write
     is still a valid sync point. Without this, a subsequent ftello,
     buffered read, or buffered write on the same stream would use the
     stale stdio offset. */
  if (fseeko(f->fp, 0, SEEK_CUR) != 0) sp_file_raise_errno("write", "file");
  return (sp_int)n;
}

/* Shared write core: `n` is the operand byte length (strlen for the
   bare-literal-safe entry, sp_str_byte_len for the binary one). */
SP_NORETURN SP_COLD void sp_io_raise_closed(void) {
  sp_raise_cls("IOError", "closed stream");
}

static sp_int sp_File_write_len(sp_File *f, const char *s, size_t n) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  if (f->is_sock) return sp_sock_write(f, s, n);
  /* An IO.pipe write end is sync, so setvbuf left it unbuffered and stdio
     holds nothing between calls: the raw loop is what fwrite would do anyway,
     and it can park between attempts. Every other handle keeps stdio. */
  if (f->sync_on && sp_io_parkable(f)) return sp_sock_write(f, s, n);
  return (sp_int)fwrite(s, 1, n, f->fp);
}

sp_int sp_File_write(sp_File *f, const char *s) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  SP_IO_OPEN(f);
  if (!s) return 0;
  return sp_File_write_len(f, s, strlen(s));
}

/* Binary-safe write: sizes the operand with the header length, so an embedded
   NUL reaches the descriptor instead of truncating the write (IO#read and
   File.read already return such bytes; only the write side dropped them).
   Reads s[-1], so it is for CODEGEN-emitted String values only -- the same
   value class String#<< / #replace already size this way. Runtime-internal
   callers and codegen's synthesized "" / "\n" literals are bare C literals
   with no marker byte and must use the plain entry above. */
sp_int sp_File_write_bin(sp_File *f, const char *s) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  SP_IO_OPEN(f);
  if (!s) return 0;
  return sp_File_write_len(f, s, sp_str_byte_len(s));
}

/* IO#syswrite: the same write core as sp_File_write_bin, but the byte
   length is passed in by the caller rather than read off the marker byte.
   The codegen syswrite arm sizes the operand itself -- a String source's
   length (sp_str_byte_len, so an embedded NUL reaches the descriptor) or a
   converted value's length (sp_poly_to_s + strlen) -- and hands both the
   pointer and the count to this entry, which is the single place the
   socket/pipe/stream routing decision is made. A regular file writes
   straight to the descriptor (sp_io_write_raw), bypassing the stdio
   buffer: syswrite is unbuffered, so the bytes are on disk before the
   call returns and a read back through the same or another handle sees
   them without a flush. */
sp_int sp_File_syswrite(sp_File *f, const char *s, size_t n) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  SP_IO_OPEN(f);
  if (!s) return 0;
  if (f->is_sock) return sp_sock_write(f, s, n);
  if (f->sync_on && sp_io_parkable(f)) return sp_sock_write(f, s, n);
  return sp_io_write_raw(f, s, n);
}

sp_bool sp_File_tty_p(sp_File *f) {
  SP_IO_OPEN(f);
  return isatty(fileno(f->fp)) ? 1 : 0;
}

sp_int sp_File_fileno(sp_File *f) {
  SP_IO_OPEN(f);
  if (f->fno_plus1) return (sp_int)(f->fno_plus1 - 1);
  return (sp_int)fileno(f->fp);
}

/* IO#winsize -> [rows, cols]. Queries the terminal; an OPEN non-tty
   (pipe/file) has no size, so CRuby raises there, but returning [0, 0] keeps
   the common "STDOUT.winsize" probe compiling and running without an
   exception path. A closed handle is the macro's IOError, as everywhere. */
sp_IntArray *sp_File_winsize(sp_File *f) {
  SP_IO_OPEN(f);
  sp_int rows = 0, cols = 0;
#if SP_HAVE_IOCTL
  struct winsize ws;
  if (ioctl(fileno(f->fp), TIOCGWINSZ, &ws) == 0) { rows = ws.ws_row; cols = ws.ws_col; }
#endif
  sp_IntArray *a = sp_IntArray_new();
  sp_IntArray_push(a, rows);
  sp_IntArray_push(a, cols);
  return a;
}

/* The standard streams are singletons in static storage, not GC allocations.
   Reads of $stdout / $stderr compile straight to these calls and only ever
   dereference the result, so nothing scanned ever held one -- until
   `$stderr = $stdout` stored it into a real global slot, which the generated
   globals hook marks. sp_gc_mark decides what a pointer is from the byte
   before it, finds whatever .bss happens to sit there, fabricates a header at
   obj - sizeof(sp_gc_hdr) and calls the scan pointer read out of it (#3410).

   Give them the tag the collector already reads as "static storage, leave it
   alone", the same one a rodata string literal carries. The mark then returns
   before it can walk a header that was never written. The eight-byte lead-in
   puts that byte immediately before the struct; sp_File begins with a pointer,
   so the compiler adds no padding of its own, and the assertion pins it. */
typedef struct { unsigned char lead[8]; sp_File f; } sp_StaticFile;
_Static_assert(offsetof(sp_StaticFile, f) == 8, "the skip tag must sit immediately before the sp_File");
#define SP_STATIC_FILE_LEAD { 0, 0, 0, 0, 0, 0, 0, 0xff }

sp_File *sp_io_stdout(void) {
  static sp_StaticFile s = { SP_STATIC_FILE_LEAD, { NULL, "<STDOUT>", "w" } };
  if (!s.f.fp) s.f.fp = stdout;
  return &s.f;
}

sp_File *sp_io_stderr(void) {
  static sp_StaticFile s = { SP_STATIC_FILE_LEAD, { NULL, "<STDERR>", "w" } };
  if (!s.f.fp) s.f.fp = stderr;
  return &s.f;
}

sp_File *sp_io_stdin(void) {
  static sp_StaticFile s = { SP_STATIC_FILE_LEAD, { NULL, "<STDIN>", "r" } };
  if (!s.f.fp) s.f.fp = stdin;
  return &s.f;
}

sp_bool sp_io_frozen(sp_File *f) { return f ? (sp_bool)f->frozen : TRUE; }
sp_File *sp_io_freeze(sp_File *f) { if (f) f->frozen = 1; return f; }

/* Declared in sp_sched.h, which sp_io.c does not include (it is deliberately
   self-contained); the single-threaded build gets the no-op definition. */
void sp_sched_ev_forget(int fd);

sp_int sp_File_close(sp_File *f) {
  /* never fclose the shared stdout/stderr handles (sp_io_stdout/sp_io_stderr):
     closing the process's standard streams would corrupt the singleton and any
     later write through it. Closing them is a no-op. */
  /* Take the descriptor out of the scheduler's readiness set BEFORE it stops
     being ours. The registration is persistent, so this is the one place a
     handle's fd dies where we can be exact; everything else that could strand
     one -- a raw close behind our back, exec, a dup we did not make -- is left
     to the self-healing arm, which re-adds on ENOENT (#4306). */
  /* The handle reads as closed BEFORE the parked readers are woken and the
     descriptor is closed, and what it carries from then on is the /dev/null
     sentinel, not the freed FILE: a reader readied by the forget below
     retries on another worker and raises on the flag; one already past its
     check reads EOF from the sentinel instead of dereferencing NULL or the
     connection the next accept gave that number (#4546). */
  if (f && f->fp && !f->closed && f->fp != stdout && f->fp != stderr && f->fp != stdin) {
    FILE *fp = f->fp;
    int fd = fileno(fp);
    f->closed = 1;
    f->fp = sp_io_closed_sentinel();
    sp_sched_ev_forget(fd);
    /* autoclose=false on an IO that wraps the fd itself: flush and abandon
       the FILE (see sp_File_fin); a for_fd wrapper holds a dup, so its
       fclose never reaches the caller's descriptor (#4208) */
    if (f->no_autoclose && !f->fno_plus1) { fflush(fp); return 0; }
    fclose(fp);
    return 0;
  }
  if (f && f->fp && !f->closed) sp_sched_ev_forget(fileno(f->fp));
  return 0;
}

sp_bool sp_File_closed_p(sp_File *f) {
  return !f || !f->fp || f->closed;
}
FILE *sp_io_closed_sentinel(void) {
  static FILE *sentinel = NULL;
  if (!sentinel) {
    FILE *s = fopen("/dev/null", "r+");
    if (!s) s = fopen("/dev/null", "r");
    if (!s) sp_raise_cls("IOError", "cannot open /dev/null");
    if (!__atomic_compare_exchange_n(&sentinel, &(FILE *){NULL}, s, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) fclose(s);
  }
  return sentinel;
}

/* The Ruby class a handle presents as, for the NoMethodError texts and #inspect.
   The kind label a socket carries in ->mode is what tells the socket classes
   apart; a path-backed handle is a File and everything else is an IO. */
const char *sp_io_kind_name(sp_File *f) {
  /* a NULL handle IS nil (the readiness family answers nil on timeout), so it
     names NilClass -- both for #class and for the NoMethodError texts */
  if (!f) return SPL("NilClass");
  if (f->is_sock && f->mode) {
    if (strcmp(f->mode, "tcpserver") == 0) return SPL("TCPServer");
    if (strcmp(f->mode, "tcp") == 0) return SPL("TCPSocket");
    if (strcmp(f->mode, "udp") == 0) return SPL("UDPSocket");
    if (strcmp(f->mode, "unix") == 0) return SPL("UNIXSocket");
    if (strcmp(f->mode, "unixserver") == 0) return SPL("UNIXServer");
    if (strcmp(f->mode, "socket") == 0) return SPL("Socket");
  }
  if (f->path && f->path[0] && f->path[0] != '<') return SPL("File");
  return SPL("IO");
}

/* The builtin superclass chain for a handle's class, mirroring the one the
   generated TU carries for class VALUES (sp_builtin_superclass). A handle
   answers #is_a? from its kind, which is a runtime property, so the walk lives
   here rather than in the emitted switch. */
static const char *sp_io_super_of(const char *k) {
  if (strcmp(k, "TCPServer") == 0)   return SPL("TCPSocket");
  if (strcmp(k, "TCPSocket") == 0)   return SPL("IPSocket");
  if (strcmp(k, "UDPSocket") == 0)   return SPL("IPSocket");
  if (strcmp(k, "IPSocket") == 0)    return SPL("BasicSocket");
  if (strcmp(k, "UNIXServer") == 0)  return SPL("UNIXSocket");
  if (strcmp(k, "UNIXSocket") == 0)  return SPL("BasicSocket");
  if (strcmp(k, "Socket") == 0)      return SPL("BasicSocket");
  if (strcmp(k, "BasicSocket") == 0) return SPL("IO");
  if (strcmp(k, "File") == 0)        return SPL("IO");
  if (strcmp(k, "IO") == 0)          return SPL("Object");
  if (strcmp(k, "Object") == 0)      return SPL("BasicObject");
  return NULL;
}
sp_bool sp_io_is_a(sp_File *f, const char *cls) {SP_GC_ROOT(f);
  if (!cls) return 0;
  if (strcmp(cls, "Kernel") == 0) return 1;   /* Object includes Kernel */
  for (const char *k = sp_io_kind_name(f); k; k = sp_io_super_of(k))
    if (strcmp(k, cls) == 0) return 1;
  return 0;
}
sp_bool sp_io_instance_of(sp_File *f, const char *cls) {SP_GC_ROOT(f);
  return cls && strcmp(sp_io_kind_name(f), cls) == 0;
}

/* Socket:: numeric constants. Their values differ between Linux and the BSDs
   (SO_REUSEADDR is 2 on Linux, 0x0004 on macOS), so they are resolved here --
   where the system headers are in scope -- rather than baked into the emitted
   C as literals. Unknown names answer -1; the caller raises NameError. */
sp_int sp_sock_const(const char *n) {
  if (!n) return -1;
  struct { const char *n; sp_int v; } T[] = {
    { "SOL_SOCKET", SOL_SOCKET }, { "IPPROTO_TCP", IPPROTO_TCP },
    { "IPPROTO_IP", IPPROTO_IP }, { "IPPROTO_UDP", IPPROTO_UDP },
    { "SO_REUSEADDR", SO_REUSEADDR }, { "SO_KEEPALIVE", SO_KEEPALIVE },
    { "SO_BROADCAST", SO_BROADCAST }, { "SO_RCVBUF", SO_RCVBUF },
    { "SO_SNDBUF", SO_SNDBUF }, { "SO_ERROR", SO_ERROR },
    { "SO_LINGER", SO_LINGER }, { "SO_TYPE", SO_TYPE },
#ifdef SO_REUSEPORT
    { "SO_REUSEPORT", SO_REUSEPORT },
#endif
    { "TCP_NODELAY", TCP_NODELAY },
    /* keepalive tuning and the timeout/credential options a client needs to
       configure a long-lived connection (#3541). Each is guarded: the names
       differ across platforms (macOS spells the idle timer TCP_KEEPALIVE). */
#ifdef TCP_KEEPIDLE
    { "TCP_KEEPIDLE", TCP_KEEPIDLE },
#elif defined(TCP_KEEPALIVE)
    { "TCP_KEEPIDLE", TCP_KEEPALIVE },
#endif
#ifdef TCP_KEEPALIVE
    { "TCP_KEEPALIVE", TCP_KEEPALIVE },
#endif
#ifdef TCP_KEEPINTVL
    { "TCP_KEEPINTVL", TCP_KEEPINTVL },
#endif
#ifdef TCP_KEEPCNT
    { "TCP_KEEPCNT", TCP_KEEPCNT },
#endif
#ifdef TCP_MAXSEG
    { "TCP_MAXSEG", TCP_MAXSEG },
#endif
#ifdef SO_RCVTIMEO
    { "SO_RCVTIMEO", SO_RCVTIMEO },
#endif
#ifdef SO_SNDTIMEO
    { "SO_SNDTIMEO", SO_SNDTIMEO },
#endif
#ifdef SO_RCVLOWAT
    { "SO_RCVLOWAT", SO_RCVLOWAT },
#endif
#ifdef SO_SNDLOWAT
    { "SO_SNDLOWAT", SO_SNDLOWAT },
#endif
#ifdef SO_OOBINLINE
    { "SO_OOBINLINE", SO_OOBINLINE },
#endif
#ifdef SO_DONTROUTE
    { "SO_DONTROUTE", SO_DONTROUTE },
#endif
#ifdef SO_ACCEPTCONN
    { "SO_ACCEPTCONN", SO_ACCEPTCONN },
#endif
#ifdef IPPROTO_IPV6
    { "IPPROTO_IPV6", IPPROTO_IPV6 },
#endif
#ifdef IPV6_V6ONLY
    { "IPV6_V6ONLY", IPV6_V6ONLY },
#endif
#ifdef IP_TTL
    { "IP_TTL", IP_TTL },
#endif
#ifdef SOCK_RAW
    { "SOCK_RAW", SOCK_RAW },
#endif
#ifdef SOCK_SEQPACKET
    { "SOCK_SEQPACKET", SOCK_SEQPACKET },
#endif
#ifdef AF_UNSPEC
    { "AF_UNSPEC", AF_UNSPEC }, { "PF_UNSPEC", PF_UNSPEC },
#endif
#ifdef MSG_PEEK
    { "MSG_PEEK", MSG_PEEK },
#endif
#ifdef MSG_WAITALL
    { "MSG_WAITALL", MSG_WAITALL },
#endif
#ifdef MSG_DONTROUTE
    { "MSG_DONTROUTE", MSG_DONTROUTE },
#endif
#ifdef MSG_OOB
    { "MSG_OOB", MSG_OOB },
#endif
    { "AF_INET", AF_INET }, { "AF_INET6", AF_INET6 },
    { "PF_INET", PF_INET }, { "PF_INET6", PF_INET6 },
#ifdef AF_UNIX   /* absent where there are no local sockets (ESP-IDF newlib) */
    { "AF_UNIX", AF_UNIX },
#endif
#ifdef PF_UNIX
    { "PF_UNIX", PF_UNIX },
#endif
    { "SOCK_STREAM", SOCK_STREAM }, { "SOCK_DGRAM", SOCK_DGRAM },
    { "SHUT_RD", SHUT_RD }, { "SHUT_WR", SHUT_WR }, { "SHUT_RDWR", SHUT_RDWR },
    { NULL, 0 }
  };
  for (int i = 0; T[i].n; i++) if (strcmp(T[i].n, n) == 0) return T[i].v;
  return -1;
}

/* UDPSocket.new / UNIXSocket.new / UNIXServer.new -- each wraps its fd in the
   same handle every other stream uses; the kind label is what tells them
   apart. A UDP handle keeps its own datagram path, so it is not fdopen'd. */
sp_File *sp_sock_udp_new(sp_int family) {
  extern int sp_net_udp_open(int family);
  int fd = sp_net_udp_open((int)family);
  if (fd < 0) sp_raise_cls("SocketError", "cannot create UDP socket");
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
  f->fp = fdopen(fd, "r+");
  if (!f->fp) { close(fd); sp_raise_cls("IOError", "fdopen failed"); return NULL; }
  f->path = NULL; f->mode = "udp"; f->lineno = 0; f->is_sock = 1;
  return f;
}
sp_File *sp_sock_unix_server(const char *path) {SP_GC_ROOT_STR(path);
  extern int sp_net_unix_listen(const char *path, int backlog);
  int fd = sp_net_unix_listen(path, 128);
  if (fd < 0) sp_file_raise_errno("bind", path ? path : "");
  sp_File *f = sp_io_fdopen_sock(fd, "unixserver");
  return f;
}
sp_File *sp_sock_unix_connect(const char *path) {SP_GC_ROOT_STR(path);
  extern int sp_net_unix_connect(const char *path);
  int fd = sp_net_unix_connect(path);
  if (fd < 0) sp_file_raise_errno("connect", path ? path : "");
  return sp_io_fdopen_sock(fd, "unix");
}

const char *sp_sock_gethostname(void) {
  extern int sp_net_gethostname(char *buf, int cap);
  char buf[256];
  if (sp_net_gethostname(buf, (int)sizeof buf) != 0) sp_file_raise_errno("gethostname", "");
  return sp_str_from_bytes(buf, strlen(buf));
}
/* Socket.new(domain, type, protocol) -- a bare socket the bind/listen/connect
   methods then drive. */
sp_File *sp_sock_new(sp_int domain, sp_int type, sp_int proto) {
  extern int sp_net_socket(int domain, int type, int protocol);
  int fd = sp_net_socket((int)domain, (int)type, (int)proto);
  if (fd < 0) sp_file_raise_errno("socket", "");
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
  f->fp = fdopen(fd, "r+");
  if (!f->fp) { close(fd); sp_raise_cls("IOError", "fdopen failed"); return NULL; }
  /* Socket.new answers Socket, not the protocol-specific subclass CRuby uses
     for TCPSocket.new -- the caller asked for the generic class. */
  f->path = NULL; f->mode = "socket"; f->lineno = 0; f->is_sock = 1;
  return f;
}
/* Socket.pair / Socket.socketpair -> the two connected ends. */
sp_File *sp_sock_pair_end(sp_int domain, sp_int type, sp_int proto, sp_int which) {
  extern int sp_net_socketpair(int domain, int type, int protocol, int fds[2]);
  static int cached[2] = { -1, -1 };
  if (which == 0) {
    if (sp_net_socketpair((int)domain, (int)type, (int)proto, cached) != 0)
      sp_file_raise_errno("socketpair", "");
  }
  int fd = cached[which == 0 ? 0 : 1];
  if (fd < 0) sp_raise_cls("IOError", "socketpair");
  (void)domain; (void)type;
  return sp_io_fdopen_sock(fd, "socket");
}

/* Socket.getaddrinfo: one row per resolution, in CRuby's 7-element shape. */
sp_PolyArray *sp_sock_getaddrinfo(const char *host, sp_int port) {SP_GC_ROOT_STR(host);
  extern int sp_net_getaddrinfo_at(const char *host, int port, int socktype, int idx,
                                   int *family, int *stype, int *proto,
                                   char *ipbuf, int ipcap, int *port_out);
  sp_PolyArray *out = sp_PolyArray_new();
  SP_GC_ROOT(out);
  for (int i = 0; i < 64; i++) {
    int fam = 0, stype = 0, proto = 0, p = 0;
    char ip[64];
    if (sp_net_getaddrinfo_at(host, (int)port, 0, i, &fam, &stype, &proto,
                              ip, (int)sizeof ip, &p) != 0) break;
    const char *ips = sp_str_from_bytes(ip, strlen(ip));
    sp_PolyArray *row = sp_PolyArray_new();
    sp_PolyArray_push(row, sp_box_str(fam == AF_INET6 ? "AF_INET6" : "AF_INET"));
    sp_PolyArray_push(row, sp_box_int((sp_int)p));
    sp_PolyArray_push(row, sp_box_str(ips));
    sp_PolyArray_push(row, sp_box_str(ips));
    sp_PolyArray_push(row, sp_box_int((sp_int)fam));
    sp_PolyArray_push(row, sp_box_int((sp_int)stype));
    sp_PolyArray_push(row, sp_box_int((sp_int)proto));
    sp_PolyArray_push(out, sp_box_poly_array(row));
  }
  return out;
}

/* Socket.sockaddr_in(port, host) / Socket.pack_sockaddr_in: the packed
   sockaddr, as a byte string. It carries NUL bytes (a zero port octet, the
   sin_zero padding, an address with a zero octet), so it is built with
   sp_str_from_bytes rather than a C-string constructor -- the string this
   returns is the one connect_nonblock(sockaddr) and bind take. */
const char *sp_sock_pack_sockaddr_in(sp_int port, const char *host) {SP_GC_ROOT_STR(host);
  extern int sp_net_pack_sockaddr_in(const char *host, int port, void *out, int cap);
  char buf[128];
  int n = sp_net_pack_sockaddr_in(host, (int)port, buf, (int)sizeof buf);
  if (n < 0)
    sp_raise_cls("SocketError",
                 sp_sprintf("getaddrinfo: Name or service not known - %s", host ? host : ""));
  return sp_str_from_bytes(buf, (size_t)n);
}

const char *sp_sock_pack_sockaddr_un(const char *path) {SP_GC_ROOT_STR(path);
  extern int sp_net_pack_sockaddr_un(const char *path, void *out, int cap);
  char buf[128 + 108];
  int n = sp_net_pack_sockaddr_un(path, buf, (int)sizeof buf);
  if (n < 0)
    sp_raise_cls("ArgumentError",
                 sp_sprintf("too long unix socket path (%d bytes given)",
                            path ? (int)strlen(path) : 0));
  return sp_str_from_bytes(buf, (size_t)n);
}

/* Addrinfo#to_sockaddr / #to_s -- the same packed form for an endpoint that is
   already resolved, which is the whole reason the 1-arg connect exists: the
   caller does not pay for the name lookup twice. AF_UNIX goes through the path
   form; everything else is already numeric, so the getaddrinfo below is a
   parse rather than a resolution. */
const char *sp_addrinfo_to_sockaddr(sp_Addrinfo *a) {SP_GC_ROOT(a);
  if (!a) sp_raise_cls("SocketError", "no address");
  if (a->afname && strcmp(a->afname, "AF_UNIX") == 0)
    return sp_sock_pack_sockaddr_un(a->ip);
  return sp_sock_pack_sockaddr_in(a->port, a->ip);
}

/* Socket.unpack_sockaddr_in -> [port, host]. The inverse, and what makes a
   packed address inspectable from Ruby without byte hacks. */
sp_PolyArray *sp_sock_unpack_sockaddr_in(const char *sa) {SP_GC_ROOT_STR(sa);
  extern int sp_net_unpack_sockaddr_in(const void *sa, int salen, char *ipbuf, int cap);
  char ip[128];
  int len = sa ? (int)sp_str_byte_len(sa) : 0;
  int port = sp_net_unpack_sockaddr_in(sa, len, ip, (int)sizeof ip);
  if (port < 0) sp_raise_cls("ArgumentError", "not an AF_INET/AF_INET6 sockaddr");
  sp_PolyArray *out = sp_PolyArray_new();
  SP_GC_ROOT(out);
  sp_PolyArray_push(out, sp_box_int((sp_int)port));
  sp_PolyArray_push(out, sp_box_str(sp_str_from_bytes(ip, strlen(ip))));
  return out;
}

/* #local_address / #remote_address -> Addrinfo for this end / the peer. */
sp_Addrinfo *sp_sock_address(sp_File *f, sp_int peer) {SP_GC_ROOT(f);
  extern sp_Addrinfo *sp_addrinfo_new(const char *ip, sp_int port, sp_int stype, sp_int is_unix);
  extern int sp_net_sock_ip(int fd, int peer, char *ipbuf, int cap);
  extern int sp_net_unix_path(int fd, int peer, char *buf, int cap);
  if (!f || !f->is_sock)
    sp_raise_cls("NoMethodError",
                 sp_sprintf("undefined method '%s' for an instance of %s",
                            peer ? "remote_address" : "local_address", sp_io_kind_name(f)));
  SP_IO_OPEN(f);
  int fd = fileno(f->fp);
  const char *k = sp_io_kind_name(f);
  int is_unix = (strcmp(k, "UNIXSocket") == 0 || strcmp(k, "UNIXServer") == 0);
  char buf[256];
  buf[0] = '\0';
  if (is_unix) {
    sp_net_unix_path(fd, (int)peer, buf, (int)sizeof buf);
    return sp_addrinfo_new(buf, 0, SOCK_STREAM, 1);
  }
  int port = sp_net_sock_ip(fd, (int)peer, buf, (int)sizeof buf);
  if (port < 0) { buf[0] = '\0'; port = 0; }
  int stype = (strcmp(k, "UDPSocket") == 0) ? SOCK_DGRAM : SOCK_STREAM;
  return sp_addrinfo_new(buf, (sp_int)port, stype, 0);
}

/* Only a socket answers the socket-specific methods; say which class the
   receiver actually is when it does not. */
static void sp_sock_require(sp_File *f, const char *m) {SP_GC_ROOT(f);SP_GC_ROOT_STR(m);
  if (!f || !f->is_sock)
    sp_raise_cls("NoMethodError",
                 sp_sprintf("undefined method '%s' for an instance of %s", m, sp_io_kind_name(f)));
  SP_IO_OPEN(f);
}
sp_int sp_sock_bind(sp_File *f, const char *host, sp_int port) {SP_GC_ROOT(f);SP_GC_ROOT_STR(host);
  extern int sp_net_udp_bind(int fd, const char *host, int port);
  sp_sock_require(f, "bind");
  if (sp_net_udp_bind(fileno(f->fp), host, (int)port) != 0)
    sp_file_raise_errno("bind", host ? host : "");
  return 0;
}
sp_int sp_sock_connect(sp_File *f, const char *host, sp_int port) {SP_GC_ROOT(f);SP_GC_ROOT_STR(host);
  extern int sp_net_udp_connect(int fd, const char *host, int port);
  sp_sock_require(f, "connect");
  if (sp_net_udp_connect(fileno(f->fp), host, (int)port) != 0)
    sp_file_raise_errno("connect", host ? host : "");
  return 0;
}
sp_int sp_sock_send(sp_File *f, const char *data, sp_int len, const char *host, sp_int port) {SP_GC_ROOT(f);SP_GC_ROOT_STR(data);SP_GC_ROOT_STR(host);
  extern int sp_net_udp_send_to(int fd, const char *data, int len, const char *host, int port);
  sp_sock_require(f, "send");
  int n = sp_net_udp_send_to(fileno(f->fp), data, (int)len, host, (int)port);
  if (n < 0) sp_file_raise_errno("send", host ? host : "");
  return (sp_int)n;
}
/* #recv reads one datagram (or up to `len` stream bytes) as a String;
   #recvfrom pairs it with the sender's address, CRuby's 4-element form. */
const char *sp_sock_recv(sp_File *f, sp_int len) {SP_GC_ROOT(f);
  extern int sp_net_udp_recv_from(int fd, char *buf, int cap, char *ipbuf, int ipcap, int *port_out);
  sp_sock_require(f, "recv");
  if (len <= 0) return sp_str_from_bytes("", 0);
  char *buf = (char *)malloc((size_t)len);
  if (!buf) sp_raise_cls("NoMemoryError", "recv");
  int n = sp_net_udp_recv_from(fileno(f->fp), buf, (int)len, NULL, 0, NULL);
  if (n < 0) { free(buf); sp_file_raise_errno("recv", ""); }
  const char *s = sp_str_from_bytes(buf, (size_t)n);
  free(buf);
  return s;
}
/* Fills the caller's address slots; returns the payload. */
const char *sp_sock_recvfrom(sp_File *f, sp_int len, const char **ip_out, sp_int *port_out) {SP_GC_ROOT(f);
  extern int sp_net_udp_recv_from(int fd, char *buf, int cap, char *ipbuf, int ipcap, int *port_out);
  sp_sock_require(f, "recvfrom");
  char ipbuf[64];
  int port = 0;
  if (len <= 0) { *ip_out = sp_str_from_bytes("", 0); *port_out = 0; return sp_str_from_bytes("", 0); }
  char *buf = (char *)malloc((size_t)len);
  if (!buf) sp_raise_cls("NoMemoryError", "recvfrom");
  int n = sp_net_udp_recv_from(fileno(f->fp), buf, (int)len, ipbuf, (int)sizeof ipbuf, &port);
  if (n < 0) { free(buf); sp_file_raise_errno("recvfrom", ""); }
  const char *s = sp_str_from_bytes(buf, (size_t)n);
  free(buf);
  *ip_out = sp_str_from_bytes(ipbuf, strlen(ipbuf));
  *port_out = (sp_int)port;
  return s;
}
sp_int sp_sock_shutdown(sp_File *f, sp_int how) {SP_GC_ROOT(f);
  extern int sp_net_shutdown(int fd, int how);
  sp_sock_require(f, "shutdown");
  if (sp_net_shutdown(fileno(f->fp), (int)how) != 0) sp_file_raise_errno("shutdown", "");
  return 0;
}
sp_int sp_sock_setsockopt(sp_File *f, sp_int level, sp_int opt, sp_int value) {SP_GC_ROOT(f);
  extern int sp_net_setsockopt_int(int fd, int level, int optname, int value);
  sp_sock_require(f, "setsockopt");
  if (sp_net_setsockopt_int(fileno(f->fp), (int)level, (int)opt, (int)value) != 0)
    sp_file_raise_errno("setsockopt", "");
  return 0;
}
sp_SockOpt *sp_sock_getsockopt(sp_File *f, sp_int level, sp_int opt) {SP_GC_ROOT(f);
  extern int sp_net_getsockopt_int(int fd, int level, int optname);
  extern sp_SockOpt *sp_sockopt_new(sp_int family, sp_int level, sp_int optname, sp_int value);
  extern int sp_net_fd_family(int fd);
  sp_sock_require(f, "getsockopt");
  int v = sp_net_getsockopt_int(fileno(f->fp), (int)level, (int)opt);
  return sp_sockopt_new((sp_int)sp_net_fd_family(fileno(f->fp)), level, opt, (sp_int)v);
}
sp_int sp_sock_listen(sp_File *f, sp_int backlog) {SP_GC_ROOT(f);
  sp_sock_require(f, "listen");
  if (listen(fileno(f->fp), (int)backlog) != 0) sp_file_raise_errno("listen", "");
  return 0;
}

/* ---- the non-blocking readiness family ----
   Each call tries once on a non-blocking descriptor. Would-block is not an
   error: with `exception: false` the caller gets :wait_readable /
   :wait_writable, otherwise the matching IO::*Wait* exception, which carries
   both its Errno parent and the IO::Wait* module (see lib/sp_exc.c). */
/* Bytes stdio already buffered for this handle. A #gets followed by a
   #read_nonblock must see them: the raw read(2) below goes straight to the
   descriptor and would skip the buffer. Same platform probe sp_io_wait_readable
   uses. */
static long sp_io_buffered(sp_File *f) {
#if defined(__GLIBC__)
  return (long)(f->fp->_IO_read_end - f->fp->_IO_read_ptr);
#elif defined(__APPLE__) || defined(__NEWLIB__)
  return (long)f->fp->_r;   /* BSD stdio; newlib shares it */
#else
  return (long)__freadahead(f->fp);
#endif
}
/* O_NONBLOCK is set for the duration of one call and put back: leaving it on
   would make a later blocking #gets treat EAGAIN as EOF. */
static int sp_io_nb_begin(sp_File *f) {
  int fd = fileno(f->fp);
  int fl = fcntl(fd, F_GETFL);
  if (fl >= 0 && !(fl & O_NONBLOCK)) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  return fl;
}
static void sp_io_nb_end(sp_File *f, int saved) {
  if (saved >= 0 && !(saved & O_NONBLOCK)) fcntl(fileno(f->fp), F_SETFL, saved);
}
static void sp_sock_nb_prepare(sp_File *f, const char *m) {SP_GC_ROOT(f);SP_GC_ROOT_STR(m);
  if (!f || !f->is_sock)
    sp_raise_cls("NoMethodError",
                 sp_sprintf("undefined method '%s' for an instance of %s", m, sp_io_kind_name(f)));
  SP_IO_OPEN(f);
}
SP_NORETURN static void sp_sock_raise_wait(int writable, const char *op) {SP_GC_ROOT_STR(op);
  sp_raise_cls(writable ? "IO::EAGAINWaitWritable" : "IO::EAGAINWaitReadable",
               sp_sprintf("Resource temporarily unavailable - %s would block", op));
}
static int sp_sock_would_block(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}
/* accept_nonblock -> the new handle, or nil / the :wait_readable marker. */
sp_File *sp_sock_accept_nb(sp_File *f, sp_bool exc) {SP_GC_ROOT(f);
  extern int sp_net_accept_nb(int sfd);
  sp_sock_nb_prepare(f, "accept_nonblock");
  int saved = sp_io_nb_begin(f);
  int fd = sp_net_accept_nb(fileno(f->fp));
  int e = errno;
  sp_io_nb_end(f, saved);
  errno = e;
  if (fd >= 0) return sp_io_fdopen_sock(fd, "tcp");
  if (sp_sock_would_block()) {
    if (!exc) return NULL;
    sp_sock_raise_wait(0, "accept");
  }
  sp_file_raise_errno("accept", "");
}
/* read_nonblock / recv_nonblock -> the bytes read, or nil when it would block
   and the caller asked for no exception. EOF is nil in CRuby only for
   `exception: false`; otherwise it is EOFError. */
/* IO#readpartial / #sysread: at most n bytes, returning as soon as ANY are
   available. It used to be one fread of the full n, which is a different
   operation: fread keeps calling read(2) until it has n bytes or hits EOF. On
   a socket that blocks until the peer sends n or closes, so the read-then-write
   shape every HTTP server has -- read the request, write the response --
   deadlocks, both ends waiting for the other (#3379). Where libc returns a
   short fread instead of blocking, the same call left the stream in a state
   the following write did not survive.

   Buffer first, exactly as sp_sock_read_nb does: whatever stdio already holds
   is data the peer has sent, and a raw read(2) would step over it, so a #gets
   before a #readpartial would lose bytes. Then a single BLOCKING read -- that
   is the only difference from the nonblocking sibling below.

   Park first, like every other read entry point: the blocking read(2) sits on
   the OS worker, which never reaches a safepoint, so the next stop-the-world
   collection waited on it forever and one green thread idle in readpartial
   on a quiet socket froze every other thread's allocation (#4528). */
const char *sp_File_readpartial(sp_File *f, sp_int n) {SP_GC_ROOT(f);
  SP_IO_OPEN(f);
  if (n < 0) sp_raise_cls("EOFError", "end of file reached");
  if (n == 0) return sp_str_empty_binary();
  sp_io_wait_readable(f);
  char *r = sp_str_alloc((size_t)n);
  ssize_t got;
  long pend = sp_io_buffered(f);
  if (pend > 0) {
    size_t want = (size_t)n < (size_t)pend ? (size_t)n : (size_t)pend;
    got = (ssize_t)fread(r, 1, want, f->fp);
  }
  else {
    do { got = read(fileno(f->fp), r, (size_t)n); } while (got < 0 && errno == EINTR);
    if (got < 0) sp_file_raise_errno("read", "");
  }
  if (got == 0) sp_raise_cls("EOFError", "end of file reached");
  r[got] = 0;
  sp_str_set_len(r, (size_t)got);
  sp_str_mark_binary(r);   /* readpartial / sysread answer ASCII-8BIT, as CRuby */
  return r;
}

const char *sp_sock_read_nb(sp_File *f, sp_int len, sp_bool exc, sp_bool is_recv, sp_bool *eof) {SP_GC_ROOT(f);
  if (eof) *eof = 0;
  if (is_recv) sp_sock_nb_prepare(f, "recv_nonblock");
  else SP_IO_OPEN(f);
  if (len <= 0) return sp_str_from_bytes("", 0);
  char *buf = (char *)malloc((size_t)len);
  if (!buf) sp_raise_cls("NoMemoryError", "read_nonblock");
  ssize_t n;
  long pend = sp_io_buffered(f);
  if (pend > 0) {   /* serve what stdio already holds, as CRuby does */
    size_t want = (size_t)len < (size_t)pend ? (size_t)len : (size_t)pend;
    n = (ssize_t)fread(buf, 1, want, f->fp);
  }
  else {
    int saved = sp_io_nb_begin(f);
    do { n = read(fileno(f->fp), buf, (size_t)len); } while (n < 0 && errno == EINTR);
    int e = errno;
    sp_io_nb_end(f, saved);
    errno = e;
  }
  if (n > 0) { const char *s = sp_str_from_bytes(buf, (size_t)n); free(buf); return s; }
  if (n == 0) {
    free(buf);
    if (eof) *eof = 1;
    /* recv_nonblock answers "" at EOF in BOTH forms -- it does not raise
       EOFError and it does not answer nil. read_nonblock is the one that
       tells them apart: nil for `exception: false`, EOFError otherwise. */
    if (is_recv) return sp_str_from_bytes("", 0);
    if (!exc) return NULL;                     /* CRuby: nil at EOF */
    sp_raise_cls("EOFError", "end of file reached");
  }
  free(buf);
  if (sp_sock_would_block()) {
    if (!exc) return NULL;
    sp_sock_raise_wait(0, "read");
  }
  sp_file_raise_errno("read", "");
}
/* write_nonblock -> the byte count, or SP_INT_NIL when it would block.
   Paired like sp_File_write: the _bin entry sizes with the header length and
   is emitted only for a String value; this one stays strlen for bare
   literals (a poly operand reaches it through sp_poly_to_s, which answers
   static class/symbol names with no marker byte). */
static sp_int sp_sock_write_nb_len(sp_File *f, const char *data, size_t len, sp_bool exc) {SP_GC_ROOT(f);SP_GC_ROOT_STR(data);
  ssize_t n;
  int saved = sp_io_nb_begin(f);
  do { n = write(fileno(f->fp), data ? data : "", len); } while (n < 0 && errno == EINTR);
  int we = errno;
  sp_io_nb_end(f, saved);
  errno = we;
  if (n >= 0) return (sp_int)n;
  if (sp_sock_would_block()) {
    if (!exc) return SP_INT_NIL;
    sp_sock_raise_wait(1, "write");
  }
  sp_file_raise_errno("write", "");
}
sp_int sp_sock_write_nb(sp_File *f, const char *data, sp_bool exc) {SP_GC_ROOT(f);SP_GC_ROOT_STR(data);
  SP_IO_OPEN(f);
  return sp_sock_write_nb_len(f, data, data ? strlen(data) : 0, exc);
}
sp_int sp_sock_write_nb_bin(sp_File *f, const char *data, sp_bool exc) {SP_GC_ROOT(f);SP_GC_ROOT_STR(data);
  SP_IO_OPEN(f);
  return sp_sock_write_nb_len(f, data, data ? sp_str_byte_len(data) : 0, exc);
}
/* connect_nonblock: an in-flight connect is IO::EINPROGRESSWaitWritable. */
sp_int sp_sock_connect_nb(sp_File *f, const char *host, sp_int port, sp_bool exc) {SP_GC_ROOT(f);SP_GC_ROOT_STR(host);
  extern int sp_net_udp_connect(int fd, const char *host, int port);
  sp_sock_nb_prepare(f, "connect_nonblock");
  int saved = sp_io_nb_begin(f);
  int rc = sp_net_udp_connect(fileno(f->fp), host, (int)port);
  int ce = errno;
  sp_io_nb_end(f, saved);
  errno = ce;
  if (rc == 0) return 0;
  if (errno == EINPROGRESS || errno == EALREADY) {
    if (!exc) return SP_INT_NIL;
    sp_raise_cls("IO::EINPROGRESSWaitWritable", "operation in progress - connect(2) would block");
  }
  if (errno == EISCONN) return 0;
  sp_file_raise_errno("connect", host ? host : "");
}
/* connect_nonblock(sockaddr). The peer is already resolved into a packed
   struct sockaddr (the binary String returned by Socket.sockaddr_in or
   #to_sockaddr on an Addrinfo). connect(2) takes the raw bytes; we just
   pass them through. Same nonblocking lifecycle as sp_sock_connect_nb. */
sp_int sp_sock_connect_nb_sa(sp_File *f, const char *sa, sp_int salen, sp_bool exc) {SP_GC_ROOT(f);SP_GC_ROOT_STR(sa);
  /* sp_sock_nb_prepare runs first: it checks the closed-socket and
     wrong-class guards and raises the matching exception (e.g. IOError
     "closed stream") before we touch the address. The sa/salen guard
     comes after, sets errno = EINVAL, and routes through the same
     errno->raise path every other socket op uses. */
  sp_sock_nb_prepare(f, "connect_nonblock");
  if (!sa || salen <= 0) {
    errno = EINVAL;
    sp_file_raise_errno("connect", "");
  }
  int saved = sp_io_nb_begin(f);
  int rc = connect(fileno(f->fp), (const struct sockaddr *)sa, (socklen_t)salen);
  int ce = errno;
  sp_io_nb_end(f, saved);
  errno = ce;
  if (rc == 0) return 0;
  if (errno == EINPROGRESS || errno == EALREADY) {
    if (!exc) return SP_INT_NIL;
    sp_raise_cls("IO::EINPROGRESSWaitWritable", "operation in progress - connect(2) would block");
  }
  /* Already connected answers 0, the same as the two-argument form above and
     the same as CRuby answers here -- measured on both, in both exception
     modes, including a second connect to a different address. */
  if (errno == EISCONN) return 0;
  sp_file_raise_errno("connect", "");
  return -1;
}

/* TCPServer#accept: park cooperatively for a pending connection first -- a
   blocking accept would stall the whole green-thread scheduler -- then wrap the
   new descriptor. Only a socket handle answers it. */
sp_File *sp_sock_accept(sp_File *f) {SP_GC_ROOT(f);
  extern int sp_net_accept(int sfd);
  if (!f || !f->is_sock)
    sp_raise_cls("NoMethodError",
                 sp_sprintf("undefined method 'accept' for an instance of %s", sp_io_kind_name(f)));
  SP_IO_OPEN(f);
  sp_io_wait_readable(f);
  return sp_io_fdopen_sock(sp_net_accept(fileno(f->fp)), "tcp");
}

/* `#<File:/etc/passwd>` for a path-backed handle, `#<IO:<STDOUT>>` for a
   standard stream. The path is what tells the two apart, the same way #class
   renders them (a stream's path is bracketed). */
const char *sp_File_inspect(sp_File *f) {SP_GC_ROOT(f);
  /* A NULL handle IS nil -- the readiness family answers one on timeout -- so
     it inspects as nil, the way sp_io_kind_name already names it NilClass. It
     rendered as a closed IO instead, which reads wrong in a puts even though
     nil? and truthiness were right (#4316). A handle that is merely CLOSED is
     a different thing and keeps its own rendering below. */
  if (!f) return SPL("nil");
  const char *p = f->path ? f->path : "";
  const char *cls = (p[0] && p[0] != '<') ? "File" : "IO";
  if (!f->fp || f->closed) return p[0] ? sp_sprintf("#<%s:%s (closed)>", cls, p)
                          : sp_sprintf("#<IO:(closed)>");
  /* a pipe end or a wrapped descriptor has no path: CRuby names the fd */
  if (!p[0]) return sp_sprintf("#<IO:fd %d>", fileno(f->fp));
  return sp_sprintf("#<%s:%s>", cls, p);
}

static void sp_File_puts_n(sp_File *f, const char *s, size_t n) {
  SP_IO_OPEN(f);
  if (f->is_sock) {
    sp_sock_write(f, s, n);
    if (n == 0 || s[n - 1] != '\n') sp_sock_write(f, "\n", 1);
    return;
  }
  fwrite(s, 1, n, f->fp);
  if (n == 0 || s[n - 1] != '\n') fputc('\n', f->fp);
}
void sp_File_puts(sp_File *f, const char *s) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  if (!s) { SP_IO_OPEN(f); return; }
  sp_File_puts_n(f, s, strlen(s));
}
/* Binary-safe puts, the pair of sp_File_write_bin: sizes the operand with the
   header length, so an embedded NUL reaches the descriptor and the trailing
   newline is decided by the real last byte. Reads s[-1], so it is for
   CODEGEN-emitted String values only -- the emitter picks it exactly where
   the write arm picks sp_File_write_bin. */
void sp_File_puts_bin(sp_File *f, const char *s) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  /* a nil String arrives as NULL and prints as `puts nil` does: a newline */
  if (!s) { sp_File_puts_n(f, "", 0); return; }
  sp_File_puts_n(f, s, sp_str_byte_len(s));
}

void sp_File_print(sp_File *f, const char *s) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  SP_IO_OPEN(f);
  if (!s) return;
  if (f->is_sock) { sp_sock_write(f, s, strlen(s)); return; }
  fputs(s, f->fp);
}

sp_int sp_File_flush(sp_File *f) {
  SP_IO_OPEN(f);
  fflush(f->fp);
  return 0;
}

sp_bool sp_File_eof_p(sp_File *f) {
  SP_IO_OPEN(f);
  sp_io_wait_readable(f);
  int c = fgetc(f->fp);
  if (c == EOF) return TRUE;
  ungetc(c, f->fp);
  return FALSE;
}

sp_int sp_File_seek(sp_File *f, sp_int off, sp_int whence) {
  SP_IO_OPEN(f);
  /* whence uses the Ruby IO::SEEK_* values (0/1/2), mapped explicitly so we
     never depend on the platform's SEEK_SET/CUR/END numbering. fseeko/ftello
     take off_t rather than fseek's long, so offsets past 2GB survive even
     where long is 32-bit. */
  int w = (whence == 1) ? SEEK_CUR : (whence == 2) ? SEEK_END : SEEK_SET;
  /* a refused seek is the descriptor's error, not a -1 return: a negative
     absolute offset is Errno::EINVAL in CRuby, and a caller that rescues
     Errno read the -1 as a successful seek (#4623) */
  if (fseeko(f->fp, (off_t)off, w) != 0) sp_file_raise_errno("seek", f->path ? f->path : "");
  return 0;
}

sp_int sp_File_tell(sp_File *f) {
  SP_IO_OPEN(f);
  return (sp_int)ftello(f->fp);
}

sp_int sp_File_rewind(sp_File *f) {
  SP_IO_OPEN(f);
  rewind(f->fp);
  return 0;
}

/* ---- File metadata predicates ----
   libc / WinAPI only, no spinel-string allocation and no shared mutable
   state, so they live here rather than inline in spinel_rt.h. Under
   SP_MULTI_CTX they ask the instance's backend stat, so virtual paths
   resolve. */
sp_bool sp_file_directory(const char *path) {
#ifdef SP_MULTI_CTX
  int is_dir = 0;
  return path && SP_CTX()->io_stat(SP_CTX()->io_ud, path, NULL, &is_dir, NULL) == 0 && is_dir;
#else
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

sp_bool sp_file_file(const char *path) {
#ifdef SP_MULTI_CTX
  int is_reg = 0;
  return path && SP_CTX()->io_stat(SP_CTX()->io_ud, path, NULL, NULL, &is_reg) == 0 && is_reg;
#else
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

sp_bool sp_file_symlink(const char *path) {
#ifdef SP_MULTI_CTX
  (void)path; return 0;   /* the backend contract has no links */
#else
  struct stat st;
  return path && lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

/* map errno to the matching Errno:: class (see sp_io.h) */
SP_NORETURN void sp_file_raise_errno(const char *op, const char *path) {SP_GC_ROOT_STR(op);SP_GC_ROOT_STR(path);
  int e = errno;   /* read once, before the formatting can touch it */
  sp_raise_cls(sp_errno_class_name(e),
               sp_sprintf("%s @ %s - %s", strerror(e), op, path ? path : ""));
}

sp_bool sp_file_owned(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && st.st_uid == geteuid();
}
sp_bool sp_file_grpowned(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && st.st_gid == getegid();
}
sp_bool sp_file_setuid(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && (st.st_mode & S_ISUID) != 0;
}
sp_bool sp_file_setgid(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && (st.st_mode & S_ISGID) != 0;
}
sp_bool sp_file_sticky(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && (st.st_mode & S_ISVTX) != 0;
}
sp_bool sp_file_socket(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && S_ISSOCK(st.st_mode);
}
sp_bool sp_file_blockdev(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && S_ISBLK(st.st_mode);
}
sp_bool sp_file_chardev(const char *path) {
  struct stat st;
  return path && SP_PATH_STAT(path, &st) == 0 && S_ISCHR(st.st_mode);
}
/* world_readable? / world_writable?: the permission bits (0..0777) when the
   other-read / other-write bit is set, else nil (SP_INT_NIL) (#3005) */
sp_int sp_file_world_readable(const char *path) {
  struct stat st;
  if (!(path && SP_PATH_STAT(path, &st) == 0 && (st.st_mode & S_IROTH))) return SP_INT_NIL;
  return (sp_int)(st.st_mode & 0777);
}
sp_int sp_file_world_writable(const char *path) {
  struct stat st;
  if (!(path && SP_PATH_STAT(path, &st) == 0 && (st.st_mode & S_IWOTH))) return SP_INT_NIL;
  return (sp_int)(st.st_mode & 0777);
}
sp_int sp_file_do_symlink(const char *oldp, const char *newp) {SP_GC_ROOT_STR(newp);
#ifdef SP_MULTI_CTX
  (void)oldp; sp_vfs_unsupported("File.symlink");
#endif
  if (symlink(oldp, newp) != 0) sp_file_raise_errno("symlink", newp);
  return 0;
}
sp_int sp_file_do_link(const char *oldp, const char *newp) {SP_GC_ROOT_STR(newp);
#ifdef SP_MULTI_CTX
  (void)oldp; sp_vfs_unsupported("File.link");
#endif
  if (link(oldp, newp) != 0) sp_file_raise_errno("link", newp);
  return 0;
}
sp_int sp_file_umask(sp_int mask, int have_arg) {
  if (have_arg) return (sp_int)umask((mode_t)mask);
  mode_t cur = umask(0);   /* read is destructive; restore immediately */
  umask(cur);
  return (sp_int)cur;
}
sp_int sp_file_mkfifo(const char *path, sp_int mode) {SP_GC_ROOT_STR(path);
#ifdef SP_MULTI_CTX
  (void)mode; sp_vfs_unsupported("File.mkfifo");
#endif
  if (mkfifo(path, (mode_t)mode) != 0) sp_file_raise_errno("mkfifo", path);
  return 0;
}
/* File.utime / File.lutime. The times arrive as a whole second and a
   nanosecond REMAINDER, not as a double: a Time carries nanoseconds and a
   timestamp near 2^31 seconds leaves a double about 100ns of resolution, so
   the fraction was lost long before utimes' microseconds were (#4635).
   utimensat takes the pair as it stands; the `l` form is the same call with
   AT_SYMLINK_NOFOLLOW, the way lstat is to stat. */
static sp_int sp_file_utime_at(int64_t asec, int32_t ansec, int64_t msec, int32_t mnsec,
                               const char *path, int flags, const char *who) {
#ifdef SP_MULTI_CTX
  (void)asec; (void)ansec; (void)msec; (void)mnsec; (void)path; (void)flags;
  sp_vfs_unsupported(who);
#endif
  struct timespec ts[2];
  ts[0].tv_sec = (time_t)asec; ts[0].tv_nsec = (long)ansec;
  ts[1].tv_sec = (time_t)msec; ts[1].tv_nsec = (long)mnsec;
  if (utimensat(AT_FDCWD, path, ts, flags) != 0) sp_file_raise_errno(who, path);
  return 1;
}
sp_int sp_file_utime_ns(int64_t asec, int32_t ansec, int64_t msec, int32_t mnsec,
                        const char *path) {SP_GC_ROOT_STR(path);
  return sp_file_utime_at(asec, ansec, msec, mnsec, path, 0, "utime");
}
sp_int sp_file_lutime_ns(int64_t asec, int32_t ansec, int64_t msec, int32_t mnsec,
                         const char *path) {SP_GC_ROOT_STR(path);
  return sp_file_utime_at(asec, ansec, msec, mnsec, path, AT_SYMLINK_NOFOLLOW, "lutime");
}

/* stat, not fopen: opening a FIFO for read blocks until a writer appears, so
   the old fopen probe hung File.exist? on a fresh mkfifo path (#3118). stat
   also answers true for directories, matching CRuby. */
sp_bool sp_file_exist(const char *path) {
#ifdef SP_MULTI_CTX
  return path && SP_CTX()->io_stat(SP_CTX()->io_ud, path, NULL, NULL, NULL) == 0;
#else
  struct stat st; return path && stat(path, &st) == 0;
#endif
}
/* unlink(2), not remove(3): remove would take a directory too, which
   File.delete refuses (EPERM here, EISDIR on Linux), as CRuby does. */
void sp_file_path_check(const char *path) {
  if (!path) sp_raise_cls("TypeError", "no implicit conversion of nil into String");
}
void sp_file_delete(const char *path) {SP_GC_ROOT_STR(path);
  sp_file_path_check(path);
#ifdef SP_MULTI_CTX
  if (sp_vfs_remove(path) != 0) sp_file_raise_errno("apply2files", path);
#else
  if (unlink(path) != 0) sp_file_raise_errno("apply2files", path);
#endif
}
void sp_file_rename(const char *from, const char *to) {SP_GC_ROOT_STR(from);SP_GC_ROOT_STR(to);
  sp_file_path_check(from); sp_file_path_check(to);
#ifdef SP_MULTI_CTX
  if (sp_vfs_rename(from, to) != 0) {
#else
  if (rename(from, to) != 0) {
#endif
    int e = errno;
    const char *pair = sp_sprintf("(%s, %s)", from, to);
    errno = e;
    sp_file_raise_errno("rb_file_s_rename", pair);
  }
}

/* --- IO instance methods that ride the underlying fd (#3038) ------------- */

/* IO#readbyte: like #getbyte but EOFError at end of file. */
sp_int sp_File_readbyte(sp_File *f) {
  SP_IO_OPEN(f);
  sp_io_wait_readable(f);
  int ch = fgetc(f->fp);
  if (ch == EOF) sp_raise_cls("EOFError", "end of file reached");
  return (sp_int)(unsigned char)ch;
}
/* IO#ungetbyte: push one byte back onto the read buffer; returns nil. */
void sp_File_ungetbyte(sp_File *f, sp_int byte) {
  SP_IO_OPEN(f);
  ungetc((int)(unsigned char)byte, f->fp);
}
/* IO#binmode?: true after #binmode, or for a handle opened in binary mode. */
sp_bool sp_File_binmode_p(sp_File *f) {
  SP_IO_OPEN(f);
  if (f->bin_flag) return 1;
  return f->mode && strchr(f->mode, 'b') != NULL;
}
void sp_File_set_binmode(sp_File *f) { SP_IO_OPEN(f); f->bin_flag = 1; }
/* The handle flags codegen used to read straight off the struct (#2792,
   #3131): a closed handle answers IOError for them as for everything else.
   #sync= keeps the flush a truthy value implies (#4229). */
sp_int sp_File_lineno(sp_File *f) { SP_IO_OPEN(f); return f->lineno; }
sp_int sp_File_set_lineno(sp_File *f, sp_int n) { SP_IO_OPEN(f); f->lineno = n; return n; }
sp_bool sp_File_sync_p(sp_File *f) { SP_IO_OPEN(f); return (f->is_sock || f->sync_on) ? 1 : 0; }
sp_bool sp_File_set_sync(sp_File *f, sp_bool on) {
  SP_IO_OPEN(f);
  f->sync_on = on ? 1 : 0;
  /* A sync handle is unbuffered, as in CRuby: every write reaches the
     descriptor before the call returns. Kernel#puts writes straight through
     stdio, so the flag alone did nothing for a file or a pipe -- a test that
     set `$stdout.sync = true` and was killed by the CI timeout left an EMPTY
     output, with no clue where it had hung. The stream is flushed first so
     the buffer swap loses nothing. */
  fflush(f->fp);
  if (f->fp != sp_io_closed_sentinel())
    setvbuf(f->fp, NULL, on ? _IONBF : (isatty(fileno(f->fp)) ? _IOLBF : _IOFBF), 0);
  return on;
}
sp_bool sp_File_autoclose_p(sp_File *f) { SP_IO_OPEN(f); return !f->no_autoclose; }
void sp_File_set_autoclose(sp_File *f, sp_bool on) { SP_IO_OPEN(f); f->no_autoclose = !on; }
/* IO#reopen(io): rebind this handle's descriptor onto the other stream. */
sp_File *sp_File_reopen_io(sp_File *f, sp_File *other) {SP_GC_ROOT(f);SP_GC_ROOT(other); sp_gc_wb((void*)f);
  if (!f || !f->fp || f->closed || !other || !other->fp || other->closed) return f;
  fflush(f->fp);
  fflush(other->fp);
  if (dup2(fileno(other->fp), fileno(f->fp)) < 0)
    sp_file_raise_errno("reopen", other->path ? other->path : "");
  f->path = other->path;
  f->mode = other->mode;
  f->lineno = 0;
  return f;
}
/* IO#close_on_exec? / #close_on_exec= via the FD_CLOEXEC descriptor flag. */
sp_bool sp_File_close_on_exec_p(sp_File *f) {
  SP_IO_OPEN(f);
  int fl = fcntl(fileno(f->fp), F_GETFD);
  return fl >= 0 && (fl & FD_CLOEXEC) != 0;
}
void sp_File_set_close_on_exec(sp_File *f, sp_bool on) {
  SP_IO_OPEN(f);
  int fd = fileno(f->fp);
  int fl = fcntl(fd, F_GETFD);
  if (fl < 0) return;
  fcntl(fd, F_SETFD, on ? (fl | FD_CLOEXEC) : (fl & ~FD_CLOEXEC));
}
/* IO#fcntl(cmd, arg=0): the raw descriptor command. */
sp_int sp_File_fcntl(sp_File *f, sp_int cmd, sp_int arg) {SP_GC_ROOT(f);
  SP_IO_OPEN(f);
  int r = fcntl(fileno(f->fp), (int)cmd, (long)arg);
  if (r < 0) sp_file_raise_errno("fcntl", f->path ? f->path : "");
  return (sp_int)r;
}
/* IO#pwrite(str, offset): write without moving the file position. */
static sp_int sp_File_pwrite_n(sp_File *f, const char *s, size_t n, sp_int off) {
  SP_IO_OPEN(f);
  fflush(f->fp);
  ssize_t put = pwrite(fileno(f->fp), s ? s : "", n, (off_t)off);
  if (put < 0) sp_file_raise_errno("pwrite", f->path ? f->path : "");
  return (sp_int)put;
}
sp_int sp_File_pwrite(sp_File *f, const char *s, sp_int off) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  return sp_File_pwrite_n(f, s, s ? strlen(s) : 0, off);
}
/* Binary-safe pwrite, the pair of sp_File_write_bin: sizes the operand with
   the header length, so an embedded NUL reaches the descriptor instead of
   cutting the write short. Reads s[-1], so it is for CODEGEN-emitted String
   values only -- the emitter picks it exactly where the write arm picks
   sp_File_write_bin. */
sp_int sp_File_pwrite_bin(sp_File *f, const char *s, sp_int off) {SP_GC_ROOT(f);SP_GC_ROOT_STR(s);
  return sp_File_pwrite_n(f, s, s ? sp_str_byte_len(s) : 0, off);
}
/* IO#advise(sym, offset=0, len=0): a hint, and nil either way. POSIX
   fadvise is Linux-ish; where it is absent the hint is simply dropped. */
void sp_File_advise(sp_File *f, const char *kind, sp_int off, sp_int len) {
  SP_IO_OPEN(f);
#ifdef POSIX_FADV_NORMAL
  int fd = fileno(f->fp);
  int a = POSIX_FADV_NORMAL;
  if (!kind) return;
  if (!strcmp(kind, "sequential")) a = POSIX_FADV_SEQUENTIAL;
  else if (!strcmp(kind, "random")) a = POSIX_FADV_RANDOM;
  else if (!strcmp(kind, "willneed")) a = POSIX_FADV_WILLNEED;
  else if (!strcmp(kind, "dontneed")) a = POSIX_FADV_DONTNEED;
  else if (!strcmp(kind, "noreuse")) a = POSIX_FADV_NOREUSE;
  posix_fadvise(fd, (off_t)off, (off_t)len, a);
#else
  (void)kind; (void)off; (void)len;
#endif
}
/* IO#close_read / #close_write. A plain file is not duplex, so half-closing
   the side it does not have raises; half-closing the side it IS just closes
   the handle, which is what CRuby does. */
void sp_File_close_half(sp_File *f, sp_bool reading) {SP_GC_ROOT(f);
  const char *m = (f && f->mode) ? f->mode : "r";
  sp_bool writable = strchr(m, 'w') || strchr(m, 'a') || strchr(m, '+');
  sp_bool readable = strchr(m, 'r') || strchr(m, '+');
  if (reading ? !readable : !writable)
    sp_raise_cls("IOError", reading ? "closing non-duplex IO for reading"
                                    : "closing non-duplex IO for writing");
  if (reading ? writable : readable)
    sp_raise_cls("IOError", reading ? "closing non-duplex IO for reading"
                                    : "closing non-duplex IO for writing");
  if (f && f->fp && !f->closed) { FILE *fp = f->fp; f->closed = 1; f->fp = sp_io_closed_sentinel(); fclose(fp); }
}
/* IO#reopen(path, mode): rebind the handle to another file. */
sp_File *sp_File_reopen(sp_File *f, const char *path, const char *mode) {SP_GC_ROOT(f);SP_GC_ROOT_STR(path);SP_GC_ROOT_STR(mode); sp_gc_wb((void*)f);
  if (!f) return f;
#ifdef SP_MULTI_CTX
  /* the backend has no freopen: open the new stream first (a failed reopen
     leaves the handle as it was, as freopen's caller sees it), then close the
     old one unless it is the closed sentinel or a standard stream */
  {
    const char *m = mode && mode[0] ? mode : "r";
    FILE *nf = sp_vfs_fopen(path ? path : "", m, m);
    if (!nf) sp_file_raise_errno("reopen", path ? path : "");
    FILE *old = f->fp;
    if (old && !f->closed && old != stdin && old != stdout && old != stderr) fclose(old);
    f->fp = nf;
    f->closed = 0;
    f->path = path;
    f->mode = m;
    f->lineno = 0;
    return f;
  }
#endif
  /* a closed handle carries the shared sentinel, which must not be
     reopened: it gets a fresh stream */
  FILE *nf = (f->closed || !f->fp) ? fopen(path ? path : "", mode && mode[0] ? mode : "r")
                                   : freopen(path ? path : "", mode && mode[0] ? mode : "r", f->fp);
  if (!nf) sp_file_raise_errno("reopen", path ? path : "");
  f->fp = nf;
  f->closed = 0;
  f->path = path;
  f->mode = mode && mode[0] ? mode : "r";
  f->lineno = 0;
  return f;
}

#ifdef SP_MULTI_CTX
/* ---- per-instance VFS backend (SP_MULTI_CTX) ---- */

/* A path-opened file under SP_MULTI_CTX: the backend handle behind a stdio
   stream. The cookie remembers the instance that opened it, so a finalizer
   that runs later still closes through the same backend. */
typedef struct { sp_ctx *c; void *h; } sp_vfs_cookie;
#if defined(__GLIBC__)
typedef off64_t sp_vfs_off;
#else
typedef off_t sp_vfs_off;   /* newlib's cookie_seek_function_t */
#endif
static ssize_t sp_vfs_cread(void *ck, char *buf, size_t n) {
  sp_vfs_cookie *k = (sp_vfs_cookie *)ck;
  long got = k->c->io_read(k->c->io_ud, k->h, buf, (long)n);
  return got < 0 ? -1 : (ssize_t)got;
}
static ssize_t sp_vfs_cwrite(void *ck, const char *buf, size_t n) {
  sp_vfs_cookie *k = (sp_vfs_cookie *)ck;
  long put = k->c->io_write(k->c->io_ud, k->h, buf, (long)n);
  return put < 0 ? -1 : (ssize_t)put;
}
static int sp_vfs_cseek(void *ck, sp_vfs_off *off, int whence) {
  sp_vfs_cookie *k = (sp_vfs_cookie *)ck;
  int w = whence == SEEK_CUR ? 1 : whence == SEEK_END ? 2 : 0;   /* the backend's 0/1/2 */
  long r = k->c->io_seek(k->c->io_ud, k->h, (long)*off, w);
  if (r < 0) return -1;
  *off = (sp_vfs_off)r;
  return 0;
}
static int sp_vfs_cclose(void *ck) {
  sp_vfs_cookie *k = (sp_vfs_cookie *)ck;
  int r = k->c->io_close(k->c->io_ud, k->h);
  free(k);
  return r;
}
/* Open `path` through the current instance's backend as a stdio stream, or
   NULL with errno set (ENOENT when the backend does not say). `bmode` goes to
   the backend as the program wrote it; `smode` is the plain stdio access mode
   the stream is opened with. */
FILE *sp_vfs_fopen(const char *path, const char *bmode, const char *smode) {
  sp_ctx *c = SP_CTX();
  errno = 0;
  void *h = c->io_open(c->io_ud, path ? path : "", bmode ? bmode : "r");
  if (!h) { if (!errno) errno = ENOENT; return NULL; }
  sp_vfs_cookie *k = (sp_vfs_cookie *)malloc(sizeof *k);
  k->c = c; k->h = h;
  cookie_io_functions_t fns = { sp_vfs_cread, sp_vfs_cwrite, sp_vfs_cseek, sp_vfs_cclose };
  FILE *fp = fopencookie(k, smode ? smode : "r", fns);
  if (!fp) { c->io_close(c->io_ud, h); free(k); errno = ENOMEM; }
  return fp;
}
/* stat(2) for a path, from the backend's io_stat. */
int sp_vfs_stat(const char *path, struct stat *st) {
  sp_ctx *c = SP_CTX();
  long size = 0; int is_dir = 0, is_reg = 0;
  memset(st, 0, sizeof *st);
  errno = 0;
  if (!path || c->io_stat(c->io_ud, path, &size, &is_dir, &is_reg) != 0) {
    if (!errno) errno = ENOENT;
    return -1;
  }
  st->st_mode = is_dir ? (S_IFDIR | 0755) : is_reg ? (S_IFREG | 0644) : 0;
  st->st_size = (off_t)size;
  return 0;
}
/* The path ops that open nothing. The backend may leave errno alone on
   failure; ENOENT is the likely reason and the one CRuby would name. */
#define SP_VFS_PATH_OP(call) do { errno = 0; if ((call) != 0) { if (!errno) errno = ENOENT; return -1; } return 0; } while (0)
int sp_vfs_remove(const char *path) { sp_ctx *c = SP_CTX(); SP_VFS_PATH_OP(c->io_remove(c->io_ud, path)); }
int sp_vfs_rename(const char *from, const char *to) { sp_ctx *c = SP_CTX(); SP_VFS_PATH_OP(c->io_rename(c->io_ud, from, to)); }
int sp_vfs_mkdir(const char *path) { sp_ctx *c = SP_CTX(); SP_VFS_PATH_OP(c->io_mkdir(c->io_ud, path)); }
int sp_vfs_rmdir(const char *path) { sp_ctx *c = SP_CTX(); SP_VFS_PATH_OP(c->io_rmdir(c->io_ud, path)); }
SP_NORETURN SP_COLD void sp_vfs_unsupported(const char *what) {
  sp_raise_cls("NotImplementedError",
               sp_sprintf("%s is not supported by this port's file backend", what ? what : "this call"));
}
/* The sp_File for such a stream: what sp_io_fdopen builds for a descriptor. */
sp_File *sp_io_vfs_wrap(FILE *fp, const char *mode) {SP_GC_ROOT_STR(mode);
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
  f->fp = fp;
  f->path = NULL;
  f->mode = mode;
  f->lineno = 0;
  return f;
}

/* ---- default libc/POSIX I/O backend (sp_ctx io_* fall back to these) ---- */
void *sp_io_posix_open(void *ud, const char *path, const char *mode) {
  (void)ud; return (void *)fopen(path ? path : "", mode ? mode : "r");
}
long sp_io_posix_read(void *ud, void *h, char *buf, long n) {
  (void)ud; return h ? (long)fread(buf, 1, (size_t)n, (FILE *)h) : -1;
}
long sp_io_posix_write(void *ud, void *h, const char *buf, long n) {
  (void)ud; return h ? (long)fwrite(buf, 1, (size_t)n, (FILE *)h) : -1;
}
long sp_io_posix_seek(void *ud, void *h, long off, int whence) {
  (void)ud;
  if (!h) return -1;
  int w = (whence == 1) ? SEEK_CUR : (whence == 2) ? SEEK_END : SEEK_SET;
  if (fseeko((FILE *)h, (off_t)off, w) != 0) return -1;
  return (long)ftello((FILE *)h);
}
long sp_io_posix_tell(void *ud, void *h) { (void)ud; return h ? (long)ftello((FILE *)h) : -1; }
int sp_io_posix_close(void *ud, void *h) { (void)ud; return h ? fclose((FILE *)h) : 0; }
int sp_io_posix_stat(void *ud, const char *path, long *size, int *is_dir, int *is_reg) {
  (void)ud;
  struct stat st;
  if (!path || stat(path, &st) != 0) return -1;
  if (size) *size = (long)st.st_size;
  if (is_dir) *is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
  if (is_reg) *is_reg = S_ISREG(st.st_mode) ? 1 : 0;
  return 0;
}
void *sp_io_posix_opendir(void *ud, const char *path) { (void)ud; return (void *)opendir(path ? path : ""); }
int sp_io_posix_readdir(void *ud, void *dh, char *namebuf, int cap) {
  (void)ud;
  if (!dh || cap <= 0) return 0;
  struct dirent *e = readdir((DIR *)dh);
  if (!e) return 0;
  strncpy(namebuf, e->d_name, (size_t)cap - 1);
  namebuf[cap - 1] = 0;
  return 1;
}
int sp_io_posix_closedir(void *ud, void *dh) { (void)ud; return dh ? closedir((DIR *)dh) : 0; }
int sp_io_posix_remove(void *ud, const char *path) { (void)ud; return unlink(path ? path : ""); }
int sp_io_posix_rename(void *ud, const char *from, const char *to) { (void)ud; return rename(from ? from : "", to ? to : ""); }
int sp_io_posix_mkdir(void *ud, const char *path) { (void)ud; return mkdir(path ? path : "", 0777); }
int sp_io_posix_rmdir(void *ud, const char *path) { (void)ud; return rmdir(path ? path : ""); }
#endif /* SP_MULTI_CTX */
