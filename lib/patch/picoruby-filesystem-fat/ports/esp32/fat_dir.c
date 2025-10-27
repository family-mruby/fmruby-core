#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/string.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include "fmrb_hal_file.h"
#include "fmrb_err.h"

#include "fat_local.h"

static void
mrb_fat_dir_free(mrb_state *mrb, void *ptr) {
  if (ptr) {
    fmrb_hal_file_closedir((fmrb_dir_t)ptr);
  }
}

struct mrb_data_type mrb_fat_dir_type = {
  "FATDir", mrb_fat_dir_free,
};


static mrb_value
mrb_s_initialize(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_get_args(mrb, "z", &path);

  // Check if path exists and is a directory
  fmrb_file_info_t info;
  fmrb_err_t err = fmrb_hal_file_stat(path, &info);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "Cannot access directory: %s (error %d)", path, err);
  }

  if (!info.is_dir) {
    mrb_raise(
      mrb, E_RUNTIME_ERROR, // Errno::ENOTDIR in CRuby
      "Not a directory @ dir_initialize"
    );
  }

  fmrb_dir_t handle = NULL;
  err = fmrb_hal_file_opendir(path, &handle);
  if (err != FMRB_OK || handle == NULL) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to open directory: %s (error %d)", path, err);
  }

  DATA_PTR(self) = handle;
  DATA_TYPE(self) = &mrb_fat_dir_type;
  return self;
}

static mrb_value
mrb_Dir_close(mrb_state *mrb, mrb_value self)
{
  fmrb_dir_t handle = (fmrb_dir_t)mrb_data_get_ptr(mrb, self, &mrb_fat_dir_type);
  fmrb_err_t err = fmrb_hal_file_closedir(handle);

  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "closedir failed: %d", err);
  }

  // Clear the pointer to prevent double-close
  DATA_PTR(self) = NULL;

  return mrb_nil_value();
}

static mrb_value
mrb_read(mrb_state *mrb, mrb_value self)
{
  fmrb_dir_t handle = (fmrb_dir_t)mrb_data_get_ptr(mrb, self, &mrb_fat_dir_type);
  fmrb_file_info_t info;
  fmrb_err_t err = fmrb_hal_file_readdir(handle, &info);

  if (err == FMRB_ERR_NOT_FOUND) {
    // No more entries
    return mrb_nil_value();
  }

  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "readdir failed: %d", err);
  }

  if (info.name[0] == 0) {
    return mrb_nil_value();
  } else {
    mrb_value value = mrb_str_new_cstr(mrb, (const char *)(info.name));
    return value;
  }
}

// FAT-specific methods - not supported in HAL abstraction
// findnext, pat=, rewind are FATFS-specific features
// For HAL abstraction, we implement basic alternatives

static mrb_value
mrb_findnext(mrb_state *mrb, mrb_value self)
{
  // Same as read() for HAL abstraction
  return mrb_read(mrb, self);
}

static mrb_value
mrb_pat_e(mrb_state *mrb, mrb_value self)
{
  // Pattern matching is not supported in basic HAL abstraction
  // Just accept the pattern but don't use it
  const char *pattern;
  mrb_get_args(mrb, "z", &pattern);
  // TODO: Store pattern for future filtering if needed
  return mrb_fixnum_value(0);
}

static mrb_value
mrb_rewind(mrb_state *mrb, mrb_value self)
{
  // HAL doesn't support rewind, so we need to close and reopen
  // For now, just return self (rewind not fully supported)
  // This would require storing the original path
  return self;
}

void
mrb_init_class_FAT_Dir(mrb_state *mrb, struct RClass *class_FAT)
{
  struct RClass *class_FAT_Dir = mrb_define_class_under_id(mrb, class_FAT, MRB_SYM(Dir), mrb->object_class);

  MRB_SET_INSTANCE_TT(class_FAT_Dir, MRB_TT_CDATA);

  // Common methods available on all platforms using fmrb_hal_file
  mrb_define_method_id(mrb, class_FAT_Dir, MRB_SYM(initialize), mrb_s_initialize, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT_Dir, MRB_SYM(close), mrb_Dir_close, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_Dir, MRB_SYM(read), mrb_read, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_Dir, MRB_SYM(findnext), mrb_findnext, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_Dir, MRB_SYM_E(pat), mrb_pat_e, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT_Dir, MRB_SYM(rewind), mrb_rewind, MRB_ARGS_NONE());
}
