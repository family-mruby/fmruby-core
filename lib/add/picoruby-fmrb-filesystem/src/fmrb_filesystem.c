/*
 * FMRuby Filesystem - File class C extensions
 *
 * Provides File test methods using fmrb_hal_file API
 */

#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include "fmrb_hal_file.h"

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

/*
 * Gem initialization
 */
void
mrb_picoruby_fmrb_filesystem_gem_init(mrb_state *mrb)
{
  // Try to get existing File class first (may be defined by picoruby-fmrb-io or mruby-io)
  struct RClass *file_class = mrb_class_get_id(mrb, MRB_SYM(File));
  if (!file_class) {
    // File class doesn't exist, create it
    // Try to inherit from IO if it exists, otherwise inherit from Object
    struct RClass *io_class = mrb_class_get_id(mrb, MRB_SYM(IO));
    if (io_class) {
      file_class = mrb_define_class_id(mrb, MRB_SYM(File), io_class);
    } else {
      file_class = mrb_define_class(mrb, "File", mrb->object_class);
    }
  }

  mrb_define_class_method(mrb, file_class, "file?", mrb_file_s_file_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "exist?", mrb_file_s_exist_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "exists?", mrb_file_s_exist_p, MRB_ARGS_REQ(1));  // Alias
  mrb_define_class_method(mrb, file_class, "directory?", mrb_file_s_directory_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "size", mrb_file_s_size, MRB_ARGS_REQ(1));

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
mrb_picoruby_fmrb_filesystem_gem_final(mrb_state *mrb)
{
  // Nothing to clean up
}
