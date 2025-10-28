#pragma once

#include <mruby.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// FATFS-specific functions - not used on ESP32 (HAL abstraction)
#ifndef FMRB_TARGET_ESP32
typedef struct FIL FIL;  // Forward declaration
void FILE_physical_address(FIL *fp, uint8_t **addr);
int FILE_sector_size(void);
#endif

mrb_value mrb__exist_p(mrb_state *mrb, mrb_value klass);
mrb_value mrb__unlink(mrb_state *mrb, mrb_value klass);
mrb_value mrb__rename(mrb_state *mrb, mrb_value klass);

typedef struct prb_vfs_methods
{
  mrb_value (*file_new)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_close)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_read)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_write)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_seek)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_tell)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_size)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_fsync)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_exist_q)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_unlink)(mrb_state *mrb, mrb_value self);
  mrb_value (*file_stat)(mrb_state *mrb, mrb_value self);
} prb_vfs_methods;

#ifndef FMRB_TARGET_ESP32
typedef int FRESULT;  // Forward declaration
void mrb_raise_iff_f_error(mrb_state *mrb, FRESULT res, const char *func);
#endif

void mrb_init_class_FAT_File(mrb_state *mrb, struct RClass *class_FAT);
void mrb_init_class_FAT_Dir(mrb_state *mrb, struct RClass *class_FAT);

#ifdef __cplusplus
}
#endif

