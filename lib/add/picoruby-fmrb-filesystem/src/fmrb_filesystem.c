/*
 * FMRuby Filesystem - File class C extensions
 *
 * Provides File test methods using fmrb_hal_file API
 */

#include <mruby.h>
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
  struct RClass *file_class = mrb_define_class(mrb, "File", mrb->object_class);

  mrb_define_class_method(mrb, file_class, "file?", mrb_file_s_file_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "exist?", mrb_file_s_exist_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "exists?", mrb_file_s_exist_p, MRB_ARGS_REQ(1));  // Alias
  mrb_define_class_method(mrb, file_class, "directory?", mrb_file_s_directory_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file_class, "size", mrb_file_s_size, MRB_ARGS_REQ(1));
}

void
mrb_picoruby_fmrb_filesystem_gem_final(mrb_state *mrb)
{
  // Nothing to clean up
}
