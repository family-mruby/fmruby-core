/*
 * FMRuby IO - IO class for standard input/output and file I/O
 *
 * Provides IO class using fmrb_hal_file API for platform abstraction
 * File class inherits from IO and extends it with file-specific methods
 */

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/data.h>
#include <mruby/variable.h>
#include "fmrb_hal_file.h"
#include "../../include/picoruby_fmrb_io.h"

// Unified IO/File object data structure
// This structure is shared between IO and File classes
typedef struct {
  fmrb_file_t handle;  // HAL file handle (includes standard streams)
  int flags;           // Open flags
  int closed;          // Closed flag
} mrb_io_data_t;

// IO data type (shared with File)
static const struct mrb_data_type mrb_io_type = {
  "IO", mrb_free,
};

/*
 * call-seq:
 *   io._new(fd, mode = "r") -> io
 *
 * Internal method to initialize IO object with C data.
 * Called from Ruby's initialize method.
 * fd: 0 (stdin), 1 (stdout), 2 (stderr)
 */
static mrb_value
mrb_io__new(mrb_state *mrb, mrb_value self)
{
  mrb_int fd;
  const char *mode = "r";
  mrb_io_data_t *io;

  mrb_get_args(mrb, "i|z", &fd, &mode);

  io = (mrb_io_data_t *)mrb_malloc(mrb, sizeof(mrb_io_data_t));
  io->flags = 0;
  io->closed = 0;

  // Map fd to standard stream handle
  switch (fd) {
    case 0:
      io->handle = FMRB_STDIN_HANDLE;
      io->flags |= 2; // readable
      break;
    case 1:
      io->handle = FMRB_STDOUT_HANDLE;
      io->flags |= 1; // writable
      break;
    case 2:
      io->handle = FMRB_STDERR_HANDLE;
      io->flags |= 1; // writable
      break;
    default:
      mrb_free(mrb, io);
      mrb_raisef(mrb, E_ARGUMENT_ERROR, "invalid file descriptor: %d", fd);
      return mrb_nil_value();
  }

  mrb_data_init(self, io, &mrb_io_type);
  return self;
}

/*
 * call-seq:
 *   io.read(length = nil) -> string or nil
 *
 * Reads length bytes from IO, or all available bytes if length is nil.
 * For standard streams, reads up to the specified length.
 */
static mrb_value
mrb_io_read(mrb_state *mrb, mrb_value self)
{
  mrb_io_data_t *io;
  mrb_int length = 1024; // Default read size for streams
  mrb_value str;
  char *buf;
  size_t bytes_read;
  fmrb_err_t err;

  io = (mrb_io_data_t *)DATA_PTR(self);
  if (!io || io->closed) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "closed stream");
  }

  mrb_get_args(mrb, "|i", &length);

  if (length <= 0) {
    return mrb_str_new_cstr(mrb, "");
  }

  // Allocate buffer
  buf = (char *)mrb_malloc(mrb, length + 1);

  // Read from handle using HAL
  err = fmrb_hal_file_read(io->handle, buf, length, &bytes_read);
  if (err != FMRB_OK) {
    mrb_free(mrb, buf);
    mrb_raise(mrb, E_RUNTIME_ERROR, "failed to read from stream");
  }

  buf[bytes_read] = '\0';
  str = mrb_str_new(mrb, buf, bytes_read);
  mrb_free(mrb, buf);

  return str;
}

/*
 * call-seq:
 *   io.write(string) -> integer
 *
 * Writes the given string to IO.
 * Returns the number of bytes written.
 */
static mrb_value
mrb_io_write(mrb_state *mrb, mrb_value self)
{
  mrb_io_data_t *io;
  mrb_value str;
  const char *ptr;
  mrb_int len;
  size_t bytes_written;
  fmrb_err_t err;

  io = (mrb_io_data_t *)DATA_PTR(self);
  if (!io || io->closed) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "closed stream");
  }

  mrb_get_args(mrb, "S", &str);
  ptr = RSTRING_PTR(str);
  len = RSTRING_LEN(str);

  // Write using HAL API
  err = fmrb_hal_file_write(io->handle, ptr, len, &bytes_written);
  if (err != FMRB_OK) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "write failed");
  }

  return mrb_int_value(mrb, bytes_written);
}

/*
 * call-seq:
 *   io.close -> nil
 *
 * Closes io.
 * For standard streams, this is a no-op.
 */
static mrb_value
mrb_io_close(mrb_state *mrb, mrb_value self)
{
  mrb_io_data_t *io;

  io = (mrb_io_data_t *)DATA_PTR(self);
  if (io && !io->closed) {
    // Close using HAL (standard streams will ignore this)
    fmrb_hal_file_close(io->handle);
    io->closed = 1;
  }

  return mrb_nil_value();
}

/*
 * call-seq:
 *   io.closed? -> true or false
 *
 * Returns true if io is completely closed, false otherwise.
 */
static mrb_value
mrb_io_closed_p(mrb_state *mrb, mrb_value self)
{
  mrb_io_data_t *io;

  io = (mrb_io_data_t *)DATA_PTR(self);
  if (!io) {
    return mrb_true_value();
  }

  return mrb_bool_value(io->closed);
}

void
mrb_picoruby_fmrb_io_init_impl(mrb_state *mrb)
{
  struct RClass *io_class;

  // Define IO class
  io_class = mrb_define_class(mrb, "IO", mrb->object_class);
  MRB_SET_INSTANCE_TT(io_class, MRB_TT_CDATA);

  // Instance methods
  // Note: IO.new and initialize are defined in Ruby (mrblib/io.rb)
  // They call _new to perform C-level initialization
  // Note: IO.open is also defined in Ruby (mrblib/io.rb)
  mrb_define_method(mrb, io_class, "_new", mrb_io__new, MRB_ARGS_ARG(1, 1));
  mrb_define_method(mrb, io_class, "read", mrb_io_read, MRB_ARGS_OPT(1));
  mrb_define_method(mrb, io_class, "write", mrb_io_write, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io_class, "close", mrb_io_close, MRB_ARGS_NONE());
  mrb_define_method(mrb, io_class, "closed?", mrb_io_closed_p, MRB_ARGS_NONE());

  // Note: STDIN/STDOUT/STDERR constants and $stdin/$stdout/$stderr global variables
  // are now defined in Ruby code (picoruby-machine/mrblib/kernel.rb) using IO.open
  // This allows for more flexibility and follows Ruby conventions
}

void
mrb_picoruby_fmrb_io_final_impl(mrb_state *mrb)
{
  // Nothing to clean up
}
