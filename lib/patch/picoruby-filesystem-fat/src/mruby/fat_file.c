#include <string.h>
#include "mruby/presym.h"
#include "mruby/string.h"
#include "mruby/class.h"
#include "mruby/data.h"
#include "fmrb_hal_file.h"
#include "fmrb_err.h"

static void
mrb_fat_file_free(mrb_state *mrb, void *ptr) {
  if (ptr) {
    fmrb_hal_file_close((fmrb_file_t)ptr);
  }
}

struct mrb_data_type mrb_fat_file_type = {
  "FATFile", mrb_fat_file_free,
};


// Convert mode string to fmrb_open_flags_t
static uint32_t parse_mode(const char *mode_str) {
  if (strcmp(mode_str, "r") == 0) {
    return FMRB_O_RDONLY;
  } else if (strcmp(mode_str, "r+") == 0) {
    return FMRB_O_RDWR;
  } else if (strcmp(mode_str, "w") == 0) {
    return FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC;
  } else if (strcmp(mode_str, "w+") == 0) {
    return FMRB_O_RDWR | FMRB_O_CREAT | FMRB_O_TRUNC;
  } else if (strcmp(mode_str, "a") == 0) {
    return FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_APPEND;
  } else if (strcmp(mode_str, "a+") == 0) {
    return FMRB_O_RDWR | FMRB_O_CREAT | FMRB_O_APPEND;
  } else if (strcmp(mode_str, "wx") == 0) {
    return FMRB_O_WRONLY | FMRB_O_CREAT;  // Create new (fail if exists)
  } else if (strcmp(mode_str, "w+x") == 0) {
    return FMRB_O_RDWR | FMRB_O_CREAT;  // Create new (fail if exists)
  }
  return 0;  // Invalid mode
}

static mrb_value
mrb_s_new(mrb_state *mrb, mrb_value klass)
{
  const char *path;
  const char *mode_str;
  mrb_get_args(mrb, "zz", &path, &mode_str);

  uint32_t flags = parse_mode(mode_str);
  if (flags == 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Unknown file open mode");
  }

  fmrb_file_t handle = NULL;
  fmrb_err_t err = fmrb_hal_file_open(path, flags, &handle);
  if (err != FMRB_OK || handle == NULL) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to open file: %s (error %d)", path, err);
  }

  mrb_value file = mrb_obj_value(Data_Wrap_Struct(mrb, mrb_class_ptr(klass), &mrb_fat_file_type, handle));
  return file;
}

// Platform-specific methods
static mrb_value
mrb_sector_size(mrb_state *mrb, mrb_value self)
{
  (void)self;
  uint32_t size = fmrb_hal_file_sector_size();
  return mrb_fixnum_value(size);
}

static mrb_value
mrb_physical_address(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  uintptr_t addr = 0;

  fmrb_err_t err = fmrb_hal_file_physical_address(handle, &addr);
  if (err == FMRB_ERR_NOT_SUPPORTED) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Physical address not supported on this platform");
  } else if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to get physical address: %d", err);
  }

  return mrb_fixnum_value((intptr_t)addr);
}

static mrb_value
mrb_tell(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  uint32_t pos = 0;
  fmrb_err_t err = fmrb_hal_file_tell(handle, &pos);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "tell failed: %d", err);
  }
  return mrb_fixnum_value(pos);
}

static mrb_value
mrb_seek(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  mrb_int ofs;
  mrb_int whence;
  mrb_get_args(mrb, "ii", &ofs, &whence);

  fmrb_seek_mode_t seek_mode;
  if (whence == SEEK_SET) {
    seek_mode = FMRB_SEEK_SET;
  } else if (whence == SEEK_CUR) {
    seek_mode = FMRB_SEEK_CUR;
  } else if (whence == SEEK_END) {
    seek_mode = FMRB_SEEK_END;
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Unknown whence");
  }

  fmrb_err_t err = fmrb_hal_file_seek(handle, (int32_t)ofs, seek_mode);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "seek failed: %d", err);
  }
  return mrb_fixnum_value(0);
}

static mrb_value
mrb_size(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);

  uint32_t size = 0;
  fmrb_err_t err = fmrb_hal_file_size(handle, &size);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "size failed: %d", err);
  }

  return mrb_fixnum_value(size);
}


static mrb_value
mrb_eof_p(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);

  // Get current position
  uint32_t current_pos = 0;
  fmrb_err_t err = fmrb_hal_file_tell(handle, &current_pos);
  if (err != FMRB_OK) {
    return mrb_false_value();
  }

  // Get file size
  uint32_t size = 0;
  err = fmrb_hal_file_size(handle, &size);
  if (err != FMRB_OK) {
    return mrb_false_value();
  }

  return (current_pos >= size) ? mrb_true_value() : mrb_false_value();
}

static mrb_value
mrb_read(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  mrb_int btr;
  mrb_get_args(mrb, "i", &btr);

  char *buff = (char *)mrb_malloc(mrb, btr);
  size_t bytes_read = 0;
  fmrb_err_t err = fmrb_hal_file_read(handle, buff, (size_t)btr, &bytes_read);

  if (err != FMRB_OK) {
    mrb_free(mrb, buff);
    mrb_raisef(mrb, E_RUNTIME_ERROR, "read failed: %d", err);
  }

  if (bytes_read > 0) {
    mrb_value value = mrb_str_new(mrb, (const void *)buff, bytes_read);
    mrb_free(mrb, buff);
    return value;
  } else {
    mrb_free(mrb, buff);
    return mrb_nil_value();
  }
}

static mrb_value
mrb_getbyte(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  char buff[1];
  size_t bytes_read = 0;
  fmrb_err_t err = fmrb_hal_file_read(handle, buff, 1, &bytes_read);

  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "getbyte failed: %d", err);
  }

  if (bytes_read == 1) {
    return mrb_fixnum_value((unsigned char)buff[0]);
  } else {
    return mrb_nil_value();
  }
}

static mrb_value
mrb_write(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  mrb_value str;
  mrb_get_args(mrb, "S", &str);

  size_t bytes_written = 0;
  fmrb_err_t err = fmrb_hal_file_write(handle, RSTRING_PTR(str), RSTRING_LEN(str), &bytes_written);

  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "write failed: %d", err);
  }

  // Sync after write (like original implementation)
  fmrb_hal_file_sync(handle);

  return mrb_fixnum_value(bytes_written);
}

static mrb_value
mrb_File_close(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  fmrb_err_t err = fmrb_hal_file_close(handle);

  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "close failed: %d", err);
  }

  // Clear the pointer to prevent double-close
  DATA_PTR(self) = NULL;

  return mrb_nil_value();
}

static mrb_value
mrb_expand(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  mrb_int size;
  mrb_get_args(mrb, "i", &size);

  // HAL doesn't have direct expand API, use seek to expand
  fmrb_err_t err = fmrb_hal_file_seek(handle, (int32_t)size, FMRB_SEEK_SET);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "expand failed: %d", err);
  }

  err = fmrb_hal_file_sync(handle);
  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "sync failed: %d", err);
  }

  return mrb_fixnum_value(size);
}

static mrb_value
mrb_fsync(mrb_state *mrb, mrb_value self)
{
  fmrb_file_t handle = (fmrb_file_t)mrb_data_get_ptr(mrb, self, &mrb_fat_file_type);
  fmrb_err_t err = fmrb_hal_file_sync(handle);

  if (err != FMRB_OK) {
    mrb_raisef(mrb, E_RUNTIME_ERROR, "fsync failed: %d", err);
  }

  return mrb_fixnum_value(0);
}


// VFSMethods support (may not be available on all platforms)
static void
mrb_vfs_methods_free(mrb_state *mrb, void *ptr) {
  if (ptr) {
    mrb_free(mrb, ptr);
  }
}

struct mrb_data_type mrb_vfs_methods_type = {
  "VFSMethods", mrb_vfs_methods_free,
};

static mrb_value
mrb_s_vfs_methods(mrb_state *mrb, mrb_value klass)
{
  prb_vfs_methods m = {
    mrb_s_new,
    mrb_File_close,
    mrb_read,
    mrb_getbyte,
    mrb_write,
    mrb_seek,
    mrb_tell,
    mrb_size,
    mrb_fsync,
    mrb__exist_p,
    mrb__unlink
  };
  prb_vfs_methods *mm = (prb_vfs_methods *)mrb_malloc(mrb, sizeof(prb_vfs_methods));
  memcpy(mm, &m, sizeof(prb_vfs_methods));
  mrb_value methods = mrb_obj_value(Data_Wrap_Struct(mrb, mrb_class(mrb, klass), &mrb_vfs_methods_type, mm));
  return methods;
}

void
mrb_init_class_FAT_File(mrb_state *mrb ,struct RClass *class_FAT)
{

  struct RClass *class_FAT_File = mrb_define_class_under_id(mrb, class_FAT, MRB_SYM(File), mrb->object_class);
  MRB_SET_INSTANCE_TT(class_FAT_File, MRB_TT_CDATA);

  struct RClass *class_FAT_VFSMethods = mrb_define_class_under_id(mrb, class_FAT, MRB_SYM(VFSMethods), mrb->object_class);
  MRB_SET_INSTANCE_TT(class_FAT_VFSMethods, MRB_TT_CDATA);

  // Common methods available on all platforms using fmrb_hal_file
  mrb_define_class_method_id(mrb, class_FAT_File, MRB_SYM(new), mrb_s_new, MRB_ARGS_REQ(2));
  mrb_define_class_method_id(mrb, class_FAT_File, MRB_SYM(open), mrb_s_new, MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(tell), mrb_tell, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(seek), mrb_seek, MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM_Q(eof), mrb_eof_p, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(read), mrb_read, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(getbyte), mrb_getbyte, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(write), mrb_write, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(close), mrb_File_close, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(size), mrb_size, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(expand), mrb_expand, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(fsync), mrb_fsync, MRB_ARGS_NONE());

  // Platform-specific methods (may raise NOT_SUPPORTED error on some platforms)
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(physical_address), mrb_physical_address, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_FAT_File, MRB_SYM(sector_size), mrb_sector_size, MRB_ARGS_NONE());

  // VFS methods (available on all platforms)
  mrb_define_class_method_id(mrb, class_FAT, MRB_SYM(vfs_methods), mrb_s_vfs_methods, MRB_ARGS_NONE());
}
