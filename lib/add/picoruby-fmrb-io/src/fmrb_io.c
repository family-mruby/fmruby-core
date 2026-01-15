/*
 * FMRuby IO - IO class for standard input/output
 *
 * Provides IO class using fmrb_hal for platform abstraction
 */

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/data.h>
#include <mruby/variable.h>
#include <string.h>
#include <stdio.h>

// IO object data structure
typedef struct {
  int fd;       // File descriptor (0=stdin, 1=stdout, 2=stderr)
  int flags;    // Open flags
  int closed;   // Closed flag
} mrb_io_t;

// IO data type
static const struct mrb_data_type mrb_io_type = {
  "IO", mrb_free,
};

/*
 * call-seq:
 *   IO.new(fd, mode = "r") -> io
 *   IO.open(fd, mode = "r") -> io
 *
 * Creates a new IO object for the given file descriptor.
 * fd: 0 (stdin), 1 (stdout), 2 (stderr)
 */
static mrb_value
mrb_io_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_int fd;
  const char *mode = "r";
  mrb_io_t *io;

  mrb_get_args(mrb, "i|z", &fd, &mode);

  io = (mrb_io_t *)mrb_malloc(mrb, sizeof(mrb_io_t));
  io->fd = fd;
  io->flags = 0;
  io->closed = 0;

  // Parse mode string
  if (strchr(mode, 'w') || strchr(mode, 'a')) {
    io->flags |= 1; // writable
  }
  if (strchr(mode, 'r') || strchr(mode, '+')) {
    io->flags |= 2; // readable
  }

  mrb_data_init(self, io, &mrb_io_type);
  return self;
}

/*
 * call-seq:
 *   IO.open(fd, mode = "r") -> io
 *
 * Creates a new IO object (class method).
 */
static mrb_value
mrb_io_s_open(mrb_state *mrb, mrb_value self)
{
  mrb_value io = mrb_obj_new(mrb, mrb_class_ptr(self), 0, NULL);
  mrb_funcall_argv(mrb, io, mrb_intern_lit(mrb, "initialize"),
                   mrb->c->ci->n, mrb->c->ci->stack + 1);
  return io;
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
  mrb_io_t *io;
  mrb_value str;
  const char *ptr;
  mrb_int len;

  io = (mrb_io_t *)mrb_data_get_ptr(mrb, self, &mrb_io_type);
  if (!io || io->closed) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "closed stream");
  }

  mrb_get_args(mrb, "S", &str);
  ptr = RSTRING_PTR(str);
  len = RSTRING_LEN(str);

  // Write to file descriptor using stdio
  switch (io->fd) {
    case 1: // stdout
      fwrite(ptr, 1, len, stdout);
      fflush(stdout);
      break;
    case 2: // stderr
      fwrite(ptr, 1, len, stderr);
      fflush(stderr);
      break;
    case 0: // stdin (cannot write)
    default:
      mrb_raise(mrb, E_RUNTIME_ERROR, "not opened for writing");
  }

  return mrb_int_value(mrb, len);
}

/*
 * call-seq:
 *   io.close -> nil
 *
 * Closes io.
 */
static mrb_value
mrb_io_close(mrb_state *mrb, mrb_value self)
{
  mrb_io_t *io;

  io = (mrb_io_t *)mrb_data_get_ptr(mrb, self, &mrb_io_type);
  if (io) {
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
  mrb_io_t *io;

  io = (mrb_io_t *)mrb_data_get_ptr(mrb, self, &mrb_io_type);
  if (!io) {
    return mrb_true_value();
  }

  return mrb_bool_value(io->closed);
}

void
mrb_picoruby_fmrb_io_gem_init(mrb_state *mrb)
{
  struct RClass *io_class;

  // Define IO class
  io_class = mrb_define_class(mrb, "IO", mrb->object_class);

  // Class methods
  mrb_define_class_method(mrb, io_class, "new", mrb_io_initialize, MRB_ARGS_ARG(1, 1));
  mrb_define_class_method(mrb, io_class, "open", mrb_io_s_open, MRB_ARGS_ARG(1, 1));

  // Instance methods
  mrb_define_method(mrb, io_class, "initialize", mrb_io_initialize, MRB_ARGS_ARG(1, 1));
  mrb_define_method(mrb, io_class, "write", mrb_io_write, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io_class, "close", mrb_io_close, MRB_ARGS_NONE());
  mrb_define_method(mrb, io_class, "closed?", mrb_io_closed_p, MRB_ARGS_NONE());
}

void
mrb_picoruby_fmrb_io_gem_final(mrb_state *mrb)
{
  // Nothing to clean up
}
