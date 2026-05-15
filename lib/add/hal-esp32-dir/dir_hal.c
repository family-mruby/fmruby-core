/*
** dir_hal.c - Directory HAL for ESP32 (ESP-IDF VFS)
**
** ESP-IDF VFS provides POSIX directory APIs (opendir/readdir/closedir/mkdir/
** rmdir/chdir/getcwd/stat) but lacks chroot(), seekdir(), and telldir().
**
** Path translation (alias rewriting and virtual mount-point synthesis) is
** owned by fmrb_hal_file: open/read here defer to the resolver and the
** virtual children table so callers see a single Unix-style namespace.
**
** PSRAM stack tasks are routed through hw_proxy (internal RAM stack) for
** opendir/readdir/closedir/stat to avoid SPI flash DMA crashes.
*/

#include <mruby.h>
#include "dir_hal.h"
#include "fmrb_hal_file.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifndef CONFIG_IDF_TARGET_LINUX
#include "hw_proxy.h"
#include "hw_proxy_internal.h"
#endif

#define DIR_HAL_PATH_MAX 128

// A single handle can wrap a real DIR* (NULL for purely-virtual paths) plus a
// tail of synthetic child names from fmrb_hal_file_virtual_children. readdir
// drains the real handle first, then yields virtual names. close releases the
// real handle if any. The virtual_names pointer references a static table in
// fmrb_hal so it does not need freeing.
struct mrb_dir_handle {
  DIR *real;
  const char *const *virtual_names;
  size_t virtual_count;
  size_t virtual_idx;
};

// Open a real directory handle for `resolved` path. Returns NULL on failure.
static DIR *
real_opendir(const char *resolved)
{
#ifndef CONFIG_IDF_TARGET_LINUX
  if (hw_proxy_needs_proxy()) {
    hw_proxy_dir_open_params_t p = { .path = resolved, .out_dir = NULL };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_OPEN, .params = &p };
    if (hw_proxy_call(&req) != 0) return NULL;
    return (DIR *)p.out_dir;
  }
#endif
  return opendir(resolved);
}

mrb_dir_handle*
mrb_hal_dir_open(mrb_state *mrb, const char *path)
{
  char resolved[DIR_HAL_PATH_MAX];
  if (fmrb_hal_file_resolve_path(path, resolved, sizeof(resolved)) != FMRB_OK) {
    return NULL;
  }

  DIR *real = real_opendir(resolved);

  size_t vcount = 0;
  const char *const *vnames = fmrb_hal_file_virtual_children(path, &vcount);

  if (real == NULL && vcount == 0) {
    return NULL;
  }

  mrb_dir_handle *handle = (mrb_dir_handle*)mrb_malloc(mrb, sizeof(mrb_dir_handle));
  handle->real = real;
  handle->virtual_names = vnames;
  handle->virtual_count = vcount;
  handle->virtual_idx = 0;
  return handle;
}

int
mrb_hal_dir_close(mrb_state *mrb, mrb_dir_handle *handle)
{
  int result = 0;
  if (handle->real != NULL) {
#ifndef CONFIG_IDF_TARGET_LINUX
    if (hw_proxy_needs_proxy()) {
      hw_proxy_dir_close_params_t p = { .dir = handle->real };
      hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_CLOSE, .params = &p };
      hw_proxy_call(&req);
    } else {
      result = closedir(handle->real);
    }
#else
    result = closedir(handle->real);
#endif
  }
  mrb_free(mrb, handle);
  return result;
}

const char*
mrb_hal_dir_read(mrb_state *mrb, mrb_dir_handle *handle)
{
  (void)mrb;
  if (handle->real != NULL) {
    const char *name = NULL;
#ifndef CONFIG_IDF_TARGET_LINUX
    if (hw_proxy_needs_proxy()) {
      hw_proxy_dir_read_params_t p = { .dir = handle->real, .out_name = &name };
      hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_READ, .params = &p };
      hw_proxy_call(&req);
    } else {
      struct dirent *dp = readdir(handle->real);
      name = dp ? dp->d_name : NULL;
    }
#else
    struct dirent *dp = readdir(handle->real);
    name = dp ? dp->d_name : NULL;
#endif
    if (name != NULL) return name;
  }
  // Real entries exhausted (or none); deliver synthetic mount-point children.
  while (handle->virtual_idx < handle->virtual_count) {
    return handle->virtual_names[handle->virtual_idx++];
  }
  return NULL;
}

void
mrb_hal_dir_rewind(mrb_state *mrb, mrb_dir_handle *handle)
{
  (void)mrb;
  if (handle->real != NULL) {
    rewinddir(handle->real);
  }
  handle->virtual_idx = 0;
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
  // Refuse to create on top of a synthetic mount-point parent.
  if (fmrb_hal_file_virtual_children(path, NULL) != NULL) {
    errno = EEXIST;
    return -1;
  }
  char resolved[DIR_HAL_PATH_MAX];
  if (fmrb_hal_file_resolve_path(path, resolved, sizeof(resolved)) != FMRB_OK) {
    return -1;
  }
  return mkdir(resolved, (mode_t)mode);
}

int
mrb_hal_dir_rmdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  // Synthetic mount-point parents are not removable.
  if (fmrb_hal_file_virtual_children(path, NULL) != NULL) {
    errno = EPERM;
    return -1;
  }
  char resolved[DIR_HAL_PATH_MAX];
  if (fmrb_hal_file_resolve_path(path, resolved, sizeof(resolved)) != FMRB_OK) {
    return -1;
  }
  return rmdir(resolved);
}

int
mrb_hal_dir_chdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  char resolved[DIR_HAL_PATH_MAX];
  if (fmrb_hal_file_resolve_path(path, resolved, sizeof(resolved)) != FMRB_OK) {
    return -1;
  }
  return chdir(resolved);
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
  // Virtual mount-point parents always count as directories.
  if (fmrb_hal_file_virtual_children(path, NULL) != NULL) {
    return 1;
  }
  char resolved[DIR_HAL_PATH_MAX];
  if (fmrb_hal_file_resolve_path(path, resolved, sizeof(resolved)) != FMRB_OK) {
    return 0;
  }
#ifndef CONFIG_IDF_TARGET_LINUX
  if (hw_proxy_needs_proxy()) {
    int is_dir = 0;
    hw_proxy_dir_stat_params_t p = { .path = resolved, .out_is_dir = &is_dir };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_DIR_STAT, .params = &p };
    hw_proxy_call(&req);
    return is_dir;
  }
#endif
  struct stat sb;
  if (stat(resolved, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return 1;
  }
  return 0;
}

void mrb_hal_dir_init(mrb_state *mrb) { (void)mrb; }
void mrb_hal_dir_final(mrb_state *mrb) { (void)mrb; }
