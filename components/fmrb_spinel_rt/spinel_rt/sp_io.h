/* sp_io.h -- File / IO handle surface.
 *
 * sp_File is a stdio FILE* plus its (GC-managed) path/mode strings,
 * shared between the generated translation unit and lib/sp_io.c, which
 * holds the allocation-free handle ops. The string-returning readers
 * (sp_File_gets / _read / _read_n / _path) stay inline in sp_runtime.h
 * because they allocate via the hot static sp_str_alloc; moving them
 * would split the per-TU string heap. */
#ifndef SP_IO_H
#define SP_IO_H

#include <stdio.h>
#include "sp_types.h"   /* mrb_int, mrb_bool */
#include "sp_array.h"   /* sp_IntArray (for #winsize) */

typedef struct { FILE *fp; const char *path; const char *mode; mrb_int lineno; } sp_File;

/* File.open(path, mode) -> GC-managed handle (block form is codegen-only). */
sp_File *sp_File_open(const char *path, const char *mode);
/* pipe(2) wrapper. 0 ok, -1 error. */
int sp_io_make_pipe(int fds[2]);
/* IO.pipe end: wrap a raw pipe fd in a GC-managed sp_File. */
sp_File *sp_io_fdopen(int fd, const char *mode);
mrb_int sp_File_write(sp_File *f, const char *s);
mrb_int sp_File_close(sp_File *f);
mrb_bool sp_File_closed_p(sp_File *f);
void sp_File_puts(sp_File *f, const char *s);
void sp_File_print(sp_File *f, const char *s);
mrb_int sp_File_flush(sp_File *f);
mrb_bool sp_File_eof_p(sp_File *f);
mrb_int sp_File_seek(sp_File *f, mrb_int off, mrb_int whence); /* #seek -- whence: 0=SET 1=CUR 2=END */
mrb_int sp_File_tell(sp_File *f);       /* #tell / #pos -- ftello, -1 on closed */
mrb_int sp_File_rewind(sp_File *f);     /* #rewind */
mrb_bool sp_File_tty_p(sp_File *f);     /* #tty? / #isatty -- isatty(fileno) */
mrb_int sp_File_fileno(sp_File *f);     /* #fileno */
sp_IntArray *sp_File_winsize(sp_File *f); /* #winsize -> [rows, cols] (ioctl, or [0,0]) */

/* STDOUT / STDERR as shared IO handles wrapping the C stdout/stderr streams.
   The handle is a function-local static (stdout/stderr are not constant
   initializers) and is never closed. */
sp_File *sp_io_stdout(void);
sp_File *sp_io_stderr(void);
sp_File *sp_io_stdin(void);

/* File metadata predicates (libc/WinAPI only; defined in sp_io.c). */
mrb_bool sp_file_directory(const char *path);
mrb_bool sp_file_file(const char *path);
mrb_bool sp_file_exist(const char *path);
mrb_bool sp_file_symlink(const char *path);
void sp_file_delete(const char *path);
void sp_file_rename(const char *from, const char *to);

#include <dirent.h>
/* Dir handle (Dir.open / Dir.each_child ...): ops live in lib/sp_cold.c.
   dp is `DIR *` in the default build, or the backend's opaque dir handle under
   SP_MULTI_CTX (stored through the same slot; a `void *`-sized pointer). */
typedef struct { DIR *dp; const char *path; } sp_Dir;

#ifdef SP_MULTI_CTX
/* A console stream keeps its real FILE* and bypasses the VFS backend (its
   handle equals the process stdout/stderr/stdin). Shared by sp_io.c and the
   inline readers in sp_runtime.h. */
#define SP_IS_STD(f) ((f)->fp == (void *)stdout || (f)->fp == (void *)stderr || (f)->fp == (void *)stdin)

/* Default libc/POSIX I/O backend (sp_ctx io_* fall back to these when the
   instance config leaves a slot NULL). The opaque handle is a FILE*, the dir
   handle a DIR*. Behaves identically to the default build's direct calls. */
void  *sp_io_posix_open(void *ud, const char *path, const char *mode);
long   sp_io_posix_read(void *ud, void *h, char *buf, long n);
long   sp_io_posix_write(void *ud, void *h, const char *buf, long n);
long   sp_io_posix_seek(void *ud, void *h, long off, int whence);
long   sp_io_posix_tell(void *ud, void *h);
int    sp_io_posix_close(void *ud, void *h);
int    sp_io_posix_stat(void *ud, const char *path, long *size, int *is_dir, int *is_reg);
void  *sp_io_posix_opendir(void *ud, const char *path);
int    sp_io_posix_readdir(void *ud, void *dh, char *namebuf, int cap);
int    sp_io_posix_closedir(void *ud, void *dh);
#endif

#endif
