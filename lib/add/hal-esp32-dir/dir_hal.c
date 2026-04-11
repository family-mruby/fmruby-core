/*
** dir_hal.c - Directory HAL for ESP32 (ESP-IDF VFS)
**
** ESP-IDF VFS provides POSIX directory APIs (opendir/readdir/closedir/mkdir/
** rmdir/chdir/getcwd/stat) but lacks chroot(), seekdir(), and telldir().
**
** PSRAM stack tasks are routed through hw_proxy (internal RAM stack) for
** opendir/readdir/closedir/stat to avoid SPI flash DMA crashes.
*/

#include <mruby.h>
#include "dir_hal.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "hw_proxy.h"
#include "hw_proxy_internal.h"
#endif

struct mrb_dir_handle {
  DIR *dir;
};

mrb_dir_handle*
mrb_hal_dir_open(mrb_state *mrb, const char *path)
{
#ifndef CONFIG_IDF_TARGET_LINUX
  if (hw_proxy_needs_proxy()) {
    hw_proxy_dir_open_params_t p = { .path = path, .out_dir = NULL };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_OPEN, .params = &p };
    if (hw_proxy_call(&req) != 0 || p.out_dir == NULL) return NULL;
    mrb_dir_handle *handle = (mrb_dir_handle*)mrb_malloc(mrb, sizeof(mrb_dir_handle));
    handle->dir = (DIR *)p.out_dir;
    return handle;
  }
#endif
  DIR *dir = opendir(path);
  if (dir == NULL) return NULL;
  mrb_dir_handle *handle = (mrb_dir_handle*)mrb_malloc(mrb, sizeof(mrb_dir_handle));
  handle->dir = dir;
  return handle;
}

int
mrb_hal_dir_close(mrb_state *mrb, mrb_dir_handle *handle)
{
#ifndef CONFIG_IDF_TARGET_LINUX
  if (hw_proxy_needs_proxy()) {
    hw_proxy_dir_close_params_t p = { .dir = handle->dir };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_CLOSE, .params = &p };
    hw_proxy_call(&req);
    mrb_free(mrb, handle);
    return 0;
  }
#endif
  int result = closedir(handle->dir);
  mrb_free(mrb, handle);
  return result;
}

const char*
mrb_hal_dir_read(mrb_state *mrb, mrb_dir_handle *handle)
{
  (void)mrb;
#ifndef CONFIG_IDF_TARGET_LINUX
  if (hw_proxy_needs_proxy()) {
    const char *name = NULL;
    hw_proxy_dir_read_params_t p = { .dir = handle->dir, .out_name = &name };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_READ, .params = &p };
    hw_proxy_call(&req);
    return name;
  }
#endif
  struct dirent *dp = readdir(handle->dir);
  return dp ? dp->d_name : NULL;
}

void
mrb_hal_dir_rewind(mrb_state *mrb, mrb_dir_handle *handle)
{
  (void)mrb;
  rewinddir(handle->dir);
}

int
mrb_hal_dir_seek(mrb_state *mrb, mrb_dir_handle *handle, long pos)
{
  /* seekdir not available on ESP-IDF VFS */
  (void)mrb; (void)handle; (void)pos;
  errno = ENOSYS;
  return -1;
}

long
mrb_hal_dir_tell(mrb_state *mrb, mrb_dir_handle *handle)
{
  /* telldir not available on ESP-IDF VFS */
  (void)mrb; (void)handle;
  errno = ENOSYS;
  return -1;
}

int
mrb_hal_dir_mkdir(mrb_state *mrb, const char *path, int mode)
{
  (void)mrb;
  return mkdir(path, (mode_t)mode);
}

int
mrb_hal_dir_rmdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  return rmdir(path);
}

int
mrb_hal_dir_chdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  return chdir(path);
}

int
mrb_hal_dir_getcwd(mrb_state *mrb, char *buf, size_t size)
{
  (void)mrb;
  return getcwd(buf, size) ? 0 : -1;
}

int
mrb_hal_dir_chroot(mrb_state *mrb, const char *path)
{
  /* chroot not available on ESP-IDF */
  (void)mrb; (void)path;
  errno = ENOSYS;
  return -1;
}

int
mrb_hal_dir_is_directory(mrb_state *mrb, const char *path)
{
  (void)mrb;
#ifndef CONFIG_IDF_TARGET_LINUX
  if (hw_proxy_needs_proxy()) {
    int is_dir = 0;
    hw_proxy_dir_stat_params_t p = { .path = path, .out_is_dir = &is_dir };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_STAT, .params = &p };
    hw_proxy_call(&req);
    return is_dir;
  }
#endif
  struct stat sb;
  if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return 1;
  }
  return 0;
}

void mrb_hal_dir_init(mrb_state *mrb) { (void)mrb; }
void mrb_hal_dir_final(mrb_state *mrb) { (void)mrb; }
