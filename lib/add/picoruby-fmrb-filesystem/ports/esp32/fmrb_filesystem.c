/*
 * FMRuby Filesystem - File class C extensions
 *
 * Provides File test methods and I/O using fmrb_hal_file API
 * File class inherits from IO class for CRuby compatibility
 */

#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/data.h>
#include "fmrb_hal_file.h"
#include "../../include/picoruby_fmrb_filesystem.h"

// File object data structure (same as mrb_io_data_t in fmrb_io.c)
typedef struct {
  fmrb_file_t handle;  // File handle from HAL
  int flags;           // Open flags
  int closed;          // Closed flag
} mrb_file_data_t;

// File data type (must match mrb_io_type name from fmrb_io.c for inheritance)
static const struct mrb_data_type mrb_file_type = {
  "IO", mrb_free,  // Use "IO" name for inheritance compatibility
};

/*
 * call-seq:
 *   File.file?(path) -> true or false
 *
 * Returns true if the named file exists and is a regular file.
 */
static mrb_value
mrb_file_s_file_p(mrb_state *mrb, mrb_value self)
{
  char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_file_info_t info;
  if (fmrb_hal_file_stat(path, &info) != FMRB_OK) {
    return mrb_false_value();
  }

  return mrb_bool_value(FMRB_S_ISREG(info.mode));
}

/*
 * call-seq:
 *   File.exist?(path) -> true or false
 *   File.exists?(path) -> true or false
 *
 * Returns true if the named file exists.
 */
static mrb_value
mrb_file_s_exist_p(mrb_state *mrb, mrb_value self)
{
  char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_file_info_t info;
  return mrb_bool_value(fmrb_hal_file_stat(path, &info) == FMRB_OK);
}

/*
 * call-seq:
 *   File.directory?(path) -> true or false
 *
 * Returns true if the named file is a directory.
 */
static mrb_value
mrb_file_s_directory_p(mrb_state *mrb, mrb_value self)
{
  char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_file_info_t info;
  if (fmrb_hal_file_stat(path, &info) != FMRB_OK) {
    return mrb_false_value();
  }

  return mrb_bool_value(FMRB_S_ISDIR(info.mode));
}

/*
 * call-seq:
 *   File.size(path) -> integer
 *
 * Returns the size of the file in bytes.
 */
static mrb_value
mrb_file_s_size(mrb_state *mrb, mrb_value self)
{
  char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_file_info_t info;
  if (fmrb_hal_file_stat(path, &info) != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "stat failed for %s", path);
  }

  return mrb_int_value(mrb, (mrb_int)info.size);
}

// Helper: Convert mode string to HAL flags
static uint32_t mode_to_flags(const char *mode)
{
  uint32_t flags = 0;

  if (!mode || !*mode) {
    return FMRB_O_RDONLY;  // Default to read-only
  }

  switch (mode[0]) {
    case 'r':
      flags = FMRB_O_RDONLY;
      if (mode[1] == '+') flags = FMRB_O_RDWR;
      break;
    case 'w':
      flags = FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC;
      if (mode[1] == '+') flags = FMRB_O_RDWR | FMRB_O_CREAT | FMRB_O_TRUNC;
      break;
    case 'a':
      flags = FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_APPEND;
      if (mode[1] == '+') flags = FMRB_O_RDWR | FMRB_O_CREAT | FMRB_O_APPEND;
      break;
    default:
      flags = FMRB_O_RDONLY;
  }

  return flags;
}

/*
 * call-seq:
 *   file._open(path, mode = "r") -> file
 *
 * Internal method to open a file with C data.
 * Called from Ruby's initialize method.
 */
static mrb_value
mrb_file__open(mrb_state *mrb, mrb_value self)
{
  char *path;
  const char *mode = "r";
  mrb_file_data_t *file;
  fmrb_err_t err;

  mrb_get_args(mrb, "z|z", &path, &mode);

  file = (mrb_file_data_t *)mrb_malloc(mrb, sizeof(mrb_file_data_t));
  file->flags = mode_to_flags(mode);
  file->closed = 0;

  // Open file using HAL
  err = fmrb_hal_file_open(path, file->flags, &file->handle);
  if (err != FMRB_OK) {
    mrb_free(mrb, file);
    mrb_raisef(mrb, E_RUNTIME_ERROR, "failed to open file: %s", path);
  }

  mrb_data_init(self, file, &mrb_file_type);
  return self;
}

/*
 * call-seq:
 *   file.read(length = nil) -> string or nil
 *
 * Reads length bytes from file, or all remaining bytes if length is nil.
 * This overrides IO#read to provide file-specific behavior.
 */
static mrb_value
mrb_file_read(mrb_state *mrb, mrb_value self)
{
  mrb_file_data_t *file;
  mrb_int length = -1;
  mrb_value str;
  char *buf;
  size_t bytes_read;
  fmrb_err_t err;

  file = (mrb_file_data_t *)DATA_PTR(self);
  if (!file || file->closed) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "closed stream");
  }

  mrb_get_args(mrb, "|i", &length);

  // If length not specified, read entire file
  if (length < 0) {
    uint32_t size;
    err = fmrb_hal_file_size(file->handle, &size);
    if (err != FMRB_OK) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "failed to get file size");
    }

    // Get current position
    uint32_t pos;
    err = fmrb_hal_file_tell(file->handle, &pos);
    if (err != FMRB_OK) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "failed to get file position");
    }

    length = size - pos;
  }

  if (length == 0) {
    return mrb_str_new_cstr(mrb, "");
  }

  // Allocate buffer
  buf = (char *)mrb_malloc(mrb, length + 1);

  // Read from file
  err = fmrb_hal_file_read(file->handle, buf, length, &bytes_read);
  if (err != FMRB_OK) {
    mrb_free(mrb, buf);
    mrb_raise(mrb, E_RUNTIME_ERROR, "failed to read from file");
  }

  buf[bytes_read] = '\0';
  str = mrb_str_new(mrb, buf, bytes_read);
  mrb_free(mrb, buf);

  return str;
}

// Note: write, close, and closed? methods are inherited from IO class
// No need to redefine them here

/*
 * Implementation functions
 */
void
mrb_picoruby_fmrb_filesystem_init_impl(mrb_state *mrb)
{
  struct RClass *file_class = NULL;
  struct RClass *io_class = NULL;

  // Get IO class (must be defined by picoruby-fmrb-io gem first)
  if (!mrb_class_defined_id(mrb, MRB_SYM(IO))) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "IO class not found - picoruby-fmrb-io must be loaded first");
    return;
  }
  io_class = mrb_class_get_id(mrb, MRB_SYM(IO));

  // Define File class inheriting from IO
  // Ruby's mrblib/file.rb will reopen this class and add Ruby methods
  file_class = mrb_define_class(mrb, "File", io_class);

  // Set instance type to CDATA to allow C data attachment
  MRB_SET_INSTANCE_TT(file_class, MRB_TT_CDATA);

  // Class methods (File test methods)
  mrb_define_class_method(mrb, file_class, "file?", mrb_file_s_file_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "exist?", mrb_file_s_exist_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "exists?", mrb_file_s_exist_p, MRB_ARGS_REQ(1));  // Alias
  mrb_define_class_method(mrb, file_class, "directory?", mrb_file_s_directory_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "size", mrb_file_s_size, MRB_ARGS_REQ(1));

  // Instance methods (File-specific methods only)
  // write, close, closed? are inherited from IO
  mrb_define_method(mrb, file_class, "_open", mrb_file__open, MRB_ARGS_ARG(1, 1));
  mrb_define_method(mrb, file_class, "read", mrb_file_read, MRB_ARGS_OPT(1));  // Override IO#read

  // Define File::Constants module
  struct RClass *constants_module = mrb_define_module_under(mrb, file_class, "Constants");

  // File open mode flags (POSIX-compatible values)
  mrb_define_const(mrb, constants_module, "RDONLY",    mrb_int_value(mrb, 0x0000));
  mrb_define_const(mrb, constants_module, "WRONLY",    mrb_int_value(mrb, 0x0001));
  mrb_define_const(mrb, constants_module, "RDWR",      mrb_int_value(mrb, 0x0002));
  mrb_define_const(mrb, constants_module, "APPEND",    mrb_int_value(mrb, 0x0008));
  mrb_define_const(mrb, constants_module, "CREAT",     mrb_int_value(mrb, 0x0040));
  mrb_define_const(mrb, constants_module, "EXCL",      mrb_int_value(mrb, 0x0100));
  mrb_define_const(mrb, constants_module, "TRUNC",     mrb_int_value(mrb, 0x0080));
  mrb_define_const(mrb, constants_module, "NONBLOCK",  mrb_int_value(mrb, 0x0004));
  mrb_define_const(mrb, constants_module, "NOCTTY",    mrb_int_value(mrb, 0x0200));
  mrb_define_const(mrb, constants_module, "BINARY",    mrb_int_value(mrb, 0x0800));
  mrb_define_const(mrb, constants_module, "SHARE_DELETE", mrb_int_value(mrb, 0x1000));
  mrb_define_const(mrb, constants_module, "SYNC",      mrb_int_value(mrb, 0x0010));
  mrb_define_const(mrb, constants_module, "DSYNC",     mrb_int_value(mrb, 0x00008000));
  mrb_define_const(mrb, constants_module, "RSYNC",     mrb_int_value(mrb, 0x00010000));
  mrb_define_const(mrb, constants_module, "NOFOLLOW",  mrb_int_value(mrb, 0x0020));
  mrb_define_const(mrb, constants_module, "NOATIME",   mrb_int_value(mrb, 0x4000));
  mrb_define_const(mrb, constants_module, "DIRECT",    mrb_int_value(mrb, 0x0400));
  mrb_define_const(mrb, constants_module, "TMPFILE",   mrb_int_value(mrb, 0x2000));

  // File lock flags
  mrb_define_const(mrb, constants_module, "LOCK_SH",   mrb_int_value(mrb, 1));
  mrb_define_const(mrb, constants_module, "LOCK_EX",   mrb_int_value(mrb, 2));
  mrb_define_const(mrb, constants_module, "LOCK_UN",   mrb_int_value(mrb, 8));
  mrb_define_const(mrb, constants_module, "LOCK_NB",   mrb_int_value(mrb, 4));

  // Path separator constants
  mrb_define_const(mrb, constants_module, "SEPARATOR", mrb_str_new_cstr(mrb, "/"));
  mrb_define_const(mrb, constants_module, "PATH_SEPARATOR", mrb_str_new_cstr(mrb, ":"));
  mrb_define_const(mrb, constants_module, "ALT_SEPARATOR", mrb_nil_value());
  mrb_define_const(mrb, constants_module, "NULL", mrb_str_new_cstr(mrb, "\0"));

  // Filename matching constants (FNM_*)
  mrb_define_const(mrb, constants_module, "FNM_SYSCASE",  mrb_int_value(mrb, 0));
  mrb_define_const(mrb, constants_module, "FNM_NOESCAPE", mrb_int_value(mrb, 1));
  mrb_define_const(mrb, constants_module, "FNM_PATHNAME", mrb_int_value(mrb, 2));
  mrb_define_const(mrb, constants_module, "FNM_DOTMATCH", mrb_int_value(mrb, 4));
  mrb_define_const(mrb, constants_module, "FNM_CASEFOLD", mrb_int_value(mrb, 8));
}

void
mrb_picoruby_fmrb_filesystem_final_impl(mrb_state *mrb)
{
  // Nothing to clean up
}
