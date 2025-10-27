#include <string.h>
#include "mruby/data.h"
#include "mruby/class.h"
#include "mruby/hash.h"
#include "mruby/string.h"
#include "mruby/presym.h"
#include "mruby/variable.h"
#include "fmrb_hal_file.h"
#include "fmrb_err.h"

// Global unixtime offset for time conversion (used by FAT implementations)
static time_t unixtime_offset = 0;

static mrb_value
mrb_unixtime_offset_e(mrb_state *mrb, mrb_value klass)
{
  mrb_int offset;
  mrb_get_args(mrb, "i", &offset);
  unixtime_offset = (time_t)offset;
  return mrb_fixnum_value(0);
}

/*
 * Usage: FAT._erase(volume)
 * params
 *   - volume: Volume name in String (e.g., "0:")
 */
static mrb_value
mrb__erase(mrb_state *mrb, mrb_value self)
{
  char *volume;
  mrb_get_args(mrb, "z", &volume);

  fmrb_err_t err = fmrb_hal_file_erase(volume);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Erase operation not supported on this platform");
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "Volume erase failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__mkfs(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_err_t err = fmrb_hal_file_mkfs(path);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Format operation not supported on this platform");
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "mkfs failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb_getfree(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  uint64_t total_bytes = 0, free_bytes = 0;
  fmrb_err_t err = fmrb_hal_file_statfs(path, &total_bytes, &free_bytes);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "statfs failed: %d", err);
  }

  // Convert to sectors (assuming 512 byte sectors for compatibility)
  uint32_t tot_sect = (uint32_t)(total_bytes / 512);
  uint32_t fre_sect = (uint32_t)(free_bytes / 512);

  return mrb_fixnum_value((tot_sect << 16) | fre_sect);
}

static mrb_value
mrb__mount(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_err_t err = fmrb_hal_file_mount(path);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    // Auto-mounted, return success
    return mrb_fixnum_value(0);
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "mount failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__unmount(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  fmrb_err_t err = fmrb_hal_file_unmount(path);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    // Auto-mounted, cannot unmount manually
    return mrb_fixnum_value(0);
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "unmount failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__chdir(mrb_state *mrb, mrb_value self)
{
  const char *name;
  mrb_get_args(mrb, "z", &name);

  fmrb_err_t err = fmrb_hal_file_chdir(name);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "chdir failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__utime(mrb_state *mrb, mrb_value self)
{
  mrb_int unixtime;
  const char *name;
  mrb_get_args(mrb, "iz", &unixtime, &name);

  fmrb_err_t err = fmrb_hal_file_utime(name, (uint32_t)unixtime);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "utime failed: %d", err);
  }
  return mrb_fixnum_value(1);
}

static mrb_value
mrb__chmod(mrb_state *mrb, mrb_value self)
{
  mrb_int attr;
  const char *path;
  mrb_get_args(mrb, "iz", &attr, &path);

  fmrb_err_t err = fmrb_hal_file_chmod(path, (uint32_t)attr);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "chmod failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__setlabel(mrb_state *mrb, mrb_value self)
{
  const char *label;
  mrb_get_args(mrb, "z", &label);

  // Use root path as default
  fmrb_err_t err = fmrb_hal_file_setlabel("/", label);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Set label not supported on this platform");
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "setlabel failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__getlabel(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  char label[12];
  fmrb_err_t err = fmrb_hal_file_getlabel(path, label);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Get label not supported on this platform");
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "getlabel failed: %d", err);
  }
  return mrb_str_new_cstr(mrb, label);
}

/*
 * Check if file is contiguous in the storage sectors
 */
static mrb_value
mrb__contiguous_p(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  // First check if it's a directory
  fmrb_file_info_t info;
  fmrb_err_t err = fmrb_hal_file_stat(path, &info);
  if (err == FMRB_OK && info.is_dir) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Is a directory");
  }

  bool is_contiguous = false;
  err = fmrb_hal_file_is_contiguous(path, &is_contiguous);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    // Not supported, assume true for simplicity
    return mrb_true_value();
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "contiguous check failed: %d", err);
  }

  return is_contiguous ? mrb_true_value() : mrb_false_value();
}

// Common functions available on all platforms using fmrb_hal_file

mrb_value
mrb__exist_p(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);
  fmrb_file_info_t info;
  fmrb_err_t err = fmrb_hal_file_stat(path, &info);
  if (err == FMRB_OK) {
    return mrb_true_value();
  } else {
    return mrb_false_value();
  }
}

mrb_value
mrb__unlink(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);
  fmrb_err_t err = fmrb_hal_file_remove(path);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "unlink failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

mrb_value
mrb__rename(mrb_state *mrb, mrb_value self)
{
  const char *from, *to;
  mrb_get_args(mrb, "zz", &from, &to);
  fmrb_err_t err = fmrb_hal_file_rename(from, to);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "rename failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb__stat(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);
  fmrb_file_info_t info;
  fmrb_err_t err = fmrb_hal_file_stat(path, &info);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "stat failed: %d", err);
  }

  mrb_value stat = mrb_hash_new_capa(mrb, 3);

  mrb_hash_set(mrb,
    stat,
    mrb_symbol_value(MRB_SYM(size)),
    mrb_fixnum_value(info.size)
  );
  mrb_hash_set(mrb,
    stat,
    mrb_symbol_value(MRB_SYM(unixtime)),
    mrb_fixnum_value((mrb_int)info.mtime)
  );
  mrb_hash_set(mrb,
    stat,
    mrb_symbol_value(MRB_SYM(mode)),
    mrb_fixnum_value(info.is_dir ? 0x10 : 0)  // Simple mode: directory flag
  );
  return stat;
}

static mrb_value
mrb__directory_p(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);
  fmrb_file_info_t info;
  fmrb_err_t err = fmrb_hal_file_stat(path, &info);
  if (err == FMRB_OK && info.is_dir) {
    return mrb_true_value();
  } else {
    return mrb_false_value();
  }
}

static mrb_value
mrb__mkdir(mrb_state *mrb, mrb_value self)
{
  const char *name;
  mrb_int mode = 0;
  mrb_get_args(mrb, "z|i", &name, &mode);
  (void)mode;
  fmrb_err_t err = fmrb_hal_file_mkdir(name);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "mkdir failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

#ifdef USE_FAT_SD_DISK
void
mrb_FAT_init_spi(mrb_state *mrb, mrb_value self)
{
  const char *unit_name;
  mrb_int sck, cipo, copi, cs;
  mrb_get_args(mrb, "ziiii", &unit_name, &sck, &cipo, &copi, &cs);
  // This function would need to be implemented in HAL if needed
  mrb_raise(mrb, E_RUNTIME_ERROR, "init_spi not yet implemented in HAL");
  return mrb_fixnum_value(0);
}
#endif


void
mrb_picoruby_filesystem_fat_gem_init(mrb_state* mrb)
{
  struct RClass *class_FAT = mrb_define_class_id(mrb, MRB_SYM(FAT), mrb->object_class);

  MRB_SET_INSTANCE_TT(class_FAT, MRB_TT_CDATA);

  // Common methods available on all platforms using fmrb_hal_file
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_mkdir), mrb__mkdir, MRB_ARGS_ARG(1, 1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_unlink), mrb__unlink, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_rename), mrb__rename, MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM_Q(_exist), mrb__exist_p, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM_Q(_directory), mrb__directory_p, MRB_ARGS_REQ(1));

  // All methods now available on all platforms (may return NOT_SUPPORTED error)
  mrb_define_class_method_id(mrb, class_FAT, MRB_SYM_E(unixtime_offset), mrb_unixtime_offset_e, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_erase), mrb__erase, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_mkfs), mrb__mkfs, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(getfree), mrb_getfree, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_mount), mrb__mount, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_unmount), mrb__unmount, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_chdir), mrb__chdir, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_utime), mrb__utime, MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_chmod), mrb__chmod, MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_setlabel), mrb__setlabel, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM(_getlabel), mrb__getlabel, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT, MRB_SYM_Q(_contiguous), mrb__contiguous_p, MRB_ARGS_REQ(1));

  mrb_init_class_FAT_Dir(mrb, class_FAT);
  mrb_init_class_FAT_File(mrb, class_FAT);

  struct RClass *class_FAT_Stat = mrb_define_class_under_id(mrb, class_FAT, MRB_SYM(Stat), mrb->object_class);
  mrb_define_method_id(mrb, class_FAT_Stat, MRB_SYM(_stat), mrb__stat, MRB_ARGS_REQ(1));

#ifdef USE_FAT_SD_DISK
  mrb_define_method(mrb, class_FAT, MRB_SYM(init_spi), mrb_FAT_init_spi, MRB_ARGS_REQ(5));
#endif
}

void
mrb_picoruby_filesystem_fat_gem_final(mrb_state* mrb)
{
}
