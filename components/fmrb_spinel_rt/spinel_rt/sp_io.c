/* sp_io.c -- File / IO handle ops in libspinel_rt.a.
 *
 * The allocation-free handle ops (open / pipe / fdopen / write / close /
 * closed? / puts / print / flush / eof?); the string-returning readers
 * (gets / read / read_n / path) stay inline in sp_runtime.h.
 *
 * Self-contained: includes sp_io.h (the sp_File layout) + sp_gc.h
 * (sp_mark_string; also pulls sp_ctx.h for the SP_MULTI_CTX I/O backend).
 *
 * SP_MULTI_CTX: regular-file byte I/O routes through the per-instance I/O
 * backend (sp_ctx io_* -> a host VFS, e.g. fmruby's fmrb HAL). sp_File.fp then
 * holds the backend's opaque handle instead of a FILE*. Console streams
 * (stdout/stderr/stdin) keep their real FILE* and bypass the backend, detected
 * by `fp == std stream`. The default (non-MC) build is unchanged (direct
 * stdio/POSIX), so it stays byte-for-byte compatible. */
#include "sp_io.h"
#include "sp_gc.h"   /* sp_mark_string */
#include <stdlib.h>
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

/* Provided by the generated TU / libspinel_rt.a. */
extern void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *));
#ifndef SP_MULTI_CTX  /* T4-0: per-ctx macro under SP_MULTI_CTX */
extern SP_NORETURN void sp_raise_cls(const char *cls, const char *msg);
#endif

static void sp_File_fin(void *p) {
  sp_File *f = (sp_File *)p;
  if (!f->fp) return;
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) SP_CTX()->io_close(SP_CTX()->io_ud, f->fp);
#else
  fclose(f->fp);
#endif
  f->fp = NULL;
}
static void sp_File_scan(void *p) { sp_File *f = (sp_File *)p; if (f->path) sp_mark_string(f->path); if (f->mode) sp_mark_string(f->mode); }

sp_File *sp_File_open(const char *path, const char *mode) {
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
#ifdef SP_MULTI_CTX
  f->fp = (FILE *)SP_CTX()->io_open(SP_CTX()->io_ud, path ? path : "", mode ? mode : "r");
#else
  f->fp = fopen(path ? path : "", mode ? mode : "r");
#endif
  if (!f->fp) { sp_raise_cls("Errno::ENOENT", "No such file or directory"); return NULL; }
  f->path = path;
  f->mode = mode;
  f->lineno = 0;
  return f;
}

/* Returns 0 on success, -1 on error. */
int sp_io_make_pipe(int fds[2]) {
  return pipe(fds);
}

/* IO.pipe end: wrap a raw pipe fd in a GC-managed sp_File so the
   sp_File_* I/O ops work on it. Pipes are a POSIX-only construct, so this
   stays on stdio in both builds; a pipe end is never a VFS-backed regular
   file (the VFS backend is for the filesystem, not pipes). */
sp_File *sp_io_fdopen(int fd, const char *mode) {
  sp_File *f = (sp_File *)sp_gc_alloc(sizeof(sp_File), sp_File_fin, sp_File_scan);
  f->fp = fdopen(fd, mode ? mode : "r");
  if (!f->fp) { sp_raise_cls("IOError", "fdopen failed"); return NULL; }
  f->path = NULL;
  f->mode = mode;
  f->lineno = 0;
  return f;
}

mrb_int sp_File_write(sp_File *f, const char *s) {
  if (!f || !f->fp || !s) return 0;
  size_t n = strlen(s);
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) return (mrb_int)SP_CTX()->io_write(SP_CTX()->io_ud, f->fp, s, (long)n);
#endif
  return (mrb_int)fwrite(s, 1, n, f->fp);
}

mrb_bool sp_File_tty_p(sp_File *f) {
#ifdef SP_MULTI_CTX
  if (f && f->fp && !SP_IS_STD(f)) return 0;  /* VFS-backed files have no tty */
#endif
  return (f && f->fp && isatty(fileno(f->fp))) ? 1 : 0;
}

mrb_int sp_File_fileno(sp_File *f) {
#ifdef SP_MULTI_CTX
  if (f && f->fp && !SP_IS_STD(f)) return -1;  /* backend handle has no fd */
#endif
  return (f && f->fp) ? (mrb_int)fileno(f->fp) : -1;
}

/* IO#winsize -> [rows, cols]. Queries the terminal; a non-tty (pipe/file) has
   no size, so CRuby raises there, but returning [0, 0] keeps the common
   "STDOUT.winsize" probe compiling and running without an exception path. */
sp_IntArray *sp_File_winsize(sp_File *f) {
  mrb_int rows = 0, cols = 0;
#ifdef SP_MULTI_CTX
  if (f && f->fp && SP_IS_STD(f)) {
#else
  if (f && f->fp) {
#endif
#if SP_HAVE_IOCTL
    struct winsize ws;
    if (ioctl(fileno(f->fp), TIOCGWINSZ, &ws) == 0) { rows = ws.ws_row; cols = ws.ws_col; }
#endif
  }
  sp_IntArray *a = sp_IntArray_new();
  sp_IntArray_push(a, rows);
  sp_IntArray_push(a, cols);
  return a;
}

sp_File *sp_io_stdout(void) {
  static sp_File f = { NULL, "<STDOUT>", "w" };
  if (!f.fp) f.fp = stdout;
  return &f;
}

sp_File *sp_io_stderr(void) {
  static sp_File f = { NULL, "<STDERR>", "w" };
  if (!f.fp) f.fp = stderr;
  return &f;
}

sp_File *sp_io_stdin(void) {
  static sp_File f = { NULL, "<STDIN>", "r" };
  if (!f.fp) f.fp = stdin;
  return &f;
}

mrb_int sp_File_close(sp_File *f) {
  /* never close the shared stdout/stderr/stdin handles: closing the process's
     standard streams would corrupt the singleton and any later write. */
  if (!f || !f->fp) return 0;
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) { SP_CTX()->io_close(SP_CTX()->io_ud, f->fp); f->fp = NULL; }
#else
  if (f->fp != stdout && f->fp != stderr && f->fp != stdin) { fclose(f->fp); f->fp = NULL; }
#endif
  return 0;
}

mrb_bool sp_File_closed_p(sp_File *f) {
  return !f || !f->fp;
}

void sp_File_puts(sp_File *f, const char *s) {
  if (!f || !f->fp || !s) return;
  size_t n = strlen(s);
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) {
    SP_CTX()->io_write(SP_CTX()->io_ud, f->fp, s, (long)n);
    if (n == 0 || s[n - 1] != '\n') SP_CTX()->io_write(SP_CTX()->io_ud, f->fp, "\n", 1);
    return;
  }
#endif
  fputs(s, f->fp);
  if (n == 0 || s[n - 1] != '\n') fputc('\n', f->fp);
}

void sp_File_print(sp_File *f, const char *s) {
  if (!f || !f->fp || !s) return;
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) { SP_CTX()->io_write(SP_CTX()->io_ud, f->fp, s, (long)strlen(s)); return; }
#endif
  fputs(s, f->fp);
}

mrb_int sp_File_flush(sp_File *f) {
#ifdef SP_MULTI_CTX
  if (f && f->fp && !SP_IS_STD(f)) return 0;  /* backend is unbuffered / auto-flush */
#endif
  if (f && f->fp) fflush(f->fp);
  return 0;
}

mrb_bool sp_File_eof_p(sp_File *f) {
  if (!f || !f->fp) return TRUE;
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) {
    /* peek one byte through the backend: read then seek back. */
    char c;
    long got = SP_CTX()->io_read(SP_CTX()->io_ud, f->fp, &c, 1);
    if (got <= 0) return TRUE;
    SP_CTX()->io_seek(SP_CTX()->io_ud, f->fp, -1, 1 /* CUR */);
    return FALSE;
  }
#endif
  int c = fgetc(f->fp);
  if (c == EOF) return TRUE;
  ungetc(c, f->fp);
  return FALSE;
}

mrb_int sp_File_seek(sp_File *f, mrb_int off, mrb_int whence) {
  if (!f || !f->fp) return -1;
  /* whence uses the Ruby IO::SEEK_* values (0/1/2). */
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) return SP_CTX()->io_seek(SP_CTX()->io_ud, f->fp, (long)off, (int)whence) < 0 ? -1 : 0;
#endif
  int w = (whence == 1) ? SEEK_CUR : (whence == 2) ? SEEK_END : SEEK_SET;
  return (mrb_int)fseeko(f->fp, (off_t)off, w);
}

mrb_int sp_File_tell(sp_File *f) {
  if (!f || !f->fp) return -1;
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) return (mrb_int)SP_CTX()->io_tell(SP_CTX()->io_ud, f->fp);
#endif
  return (mrb_int)ftello(f->fp);
}

mrb_int sp_File_rewind(sp_File *f) {
  if (!f || !f->fp) return -1;
#ifdef SP_MULTI_CTX
  if (!SP_IS_STD(f)) { SP_CTX()->io_seek(SP_CTX()->io_ud, f->fp, 0, 0 /* SET */); return 0; }
#endif
  rewind(f->fp);
  return 0;
}

/* ---- File metadata predicates ----
   Path-based (no handle). Route through the backend stat under SP_MULTI_CTX so
   virtual paths resolve; direct libc otherwise. */
mrb_bool sp_file_directory(const char *path) {
  if (!path) return 0;
#ifdef SP_MULTI_CTX
  int is_dir = 0; return SP_CTX()->io_stat(SP_CTX()->io_ud, path, NULL, &is_dir, NULL) == 0 && is_dir;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

mrb_bool sp_file_file(const char *path) {
  if (!path) return 0;
#ifdef SP_MULTI_CTX
  int is_reg = 0; return SP_CTX()->io_stat(SP_CTX()->io_ud, path, NULL, NULL, &is_reg) == 0 && is_reg;
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

mrb_bool sp_file_symlink(const char *path) {
#ifdef SP_MULTI_CTX
  (void)path; return 0;  /* the VFS backend (littlefs/HAL) has no symlinks */
#else
  struct stat st;
  return path && lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

mrb_bool sp_file_exist(const char *path) {
  if (!path) return FALSE;
#ifdef SP_MULTI_CTX
  return SP_CTX()->io_stat(SP_CTX()->io_ud, path, NULL, NULL, NULL) == 0;
#else
  FILE *f = fopen(path, "r"); if (f) { fclose(f); return TRUE; } return FALSE;
#endif
}

/* delete / rename are not in the minimal VFS contract yet (added when the file
   manager needs them); direct libc for now. */
void sp_file_delete(const char *path) { remove(path); }
void sp_file_rename(const char *from, const char *to) { rename(from, to); }

#ifdef SP_MULTI_CTX
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
#endif /* SP_MULTI_CTX */
