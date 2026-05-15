/*
** dir_hal.c - POSIX HAL for mruby-dir (family-mruby patch)
**
** Upstream hal-posix-dir passes paths straight to opendir/stat/etc., so a
** virtual path like "/app" hits the real host filesystem. The Linux dev
** environment serves the on-device "/" namespace from a local "flash/"
** directory (see components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c),
** so this patch applies the same "flash" prefix before delegating to the
** POSIX API. Logic mirrors fmrb_hal_file_resolve_path() for POSIX; the dev
** build has no SD card / virtual mount points, so no children synthesis.
*/

#include <mruby.h>
#include "dir_hal.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#define POSIX_BASE_PATH "flash"
#define POSIX_PATH_MAX 512

static void
resolve_path(const char *virtual_path, char *out, size_t out_len)
{
  if (out == NULL || out_len == 0) return;
  if (virtual_path == NULL) { out[0] = '\0'; return; }
  if (virtual_path[0] == '/') {
    snprintf(out, out_len, "%s%s", POSIX_BASE_PATH, virtual_path);
  } else {
    snprintf(out, out_len, "%s/%s", POSIX_BASE_PATH, virtual_path);
  }
}

struct mrb_dir_handle {
  DIR *dir;
};

mrb_dir_handle*
mrb_hal_dir_open(mrb_state *mrb, const char *path)
{
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));
  DIR *dir = opendir(resolved);
  if (dir == NULL) {
    return NULL;
  }
  mrb_dir_handle *handle = (mrb_dir_handle*)mrb_malloc(mrb, sizeof(mrb_dir_handle));
  handle->dir = dir;
  return handle;
}

int
mrb_hal_dir_close(mrb_state *mrb, mrb_dir_handle *handle)
{
  int result = closedir(handle->dir);
  mrb_free(mrb, handle);
  return result;
}

const char*
mrb_hal_dir_read(mrb_state *mrb, mrb_dir_handle *handle)
{
  (void)mrb;
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
#if defined(__ANDROID__)
  (void)mrb; (void)handle; (void)pos;
  errno = ENOSYS;
  return -1;
#else
  (void)mrb;
  seekdir(handle->dir, pos);
  return 0;
#endif
}

long
mrb_hal_dir_tell(mrb_state *mrb, mrb_dir_handle *handle)
{
#if defined(__ANDROID__)
  (void)mrb; (void)handle;
  errno = ENOSYS;
  return -1;
#else
  (void)mrb;
  return telldir(handle->dir);
#endif
}

int
mrb_hal_dir_mkdir(mrb_state *mrb, const char *path, int mode)
{
  (void)mrb;
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));
  return mkdir(resolved, (mode_t)mode);
}

int
mrb_hal_dir_rmdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));
  return rmdir(resolved);
}

int
mrb_hal_dir_chdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));
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
#if defined(__ANDROID__) || defined(__MSDOS__)
  (void)mrb; (void)path;
  errno = ENOSYS;
  return -1;
#else
  (void)mrb;
  return chroot(path);
#endif
}

int
mrb_hal_dir_is_directory(mrb_state *mrb, const char *path)
{
  (void)mrb;
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));
  struct stat sb;
  if (stat(resolved, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return 1;
  }
  return 0;
}

void
mrb_hal_dir_init(mrb_state *mrb)
{
  (void)mrb;
}

void
mrb_hal_dir_final(mrb_state *mrb)
{
  (void)mrb;
}

void
mrb_hal_posix_dir_gem_init(mrb_state *mrb)
{
  (void)mrb;
}

void
mrb_hal_posix_dir_gem_final(mrb_state *mrb)
{
  (void)mrb;
}
