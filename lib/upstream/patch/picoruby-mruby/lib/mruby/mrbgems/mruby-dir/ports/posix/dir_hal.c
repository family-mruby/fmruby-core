/*
** dir_hal.c - POSIX HAL for mruby-dir (family-mruby patch)
**
** Upstream passes paths straight to opendir/mkdir/stat/etc., so a virtual
** path like "/app" would hit the real host filesystem. The Linux simulation
** and the browser build serve the on-device "/" namespace from a local
** "flash/" directory, so every path goes through the file HAL's resolver
** (components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c) before the POSIX
** API. chdir/getcwd are the file HAL's per-app working directory
** (doc/api_fixes F7): the process's own is never moved, which would shift
** every app's paths at once. The flat namespace has no SD card / virtual
** mount points, so no children synthesis.
*/

#include <mruby.h>
#include "dir_hal.h"
#include "fmrb_hal_file.h"

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

/* On POSIX, mrb_dir_handle wraps DIR */
struct mrb_dir_handle {
  DIR *dir;
};

#define POSIX_PATH_MAX  512

/* Map a virtual path ("/app", or "app" under the working directory) to the
 * host path under "flash/". A path that does not resolve leaves "", which no
 * call below finds. */
static void
resolve_path(const char *virtual_path, char *out, size_t out_len)
{
  if (out == NULL || out_len == 0) return;
  if (virtual_path == NULL ||
      fmrb_hal_file_resolve_path(virtual_path, out, out_len) != FMRB_OK) {
    out[0] = '\0';
  }
}

/*
 * Directory Operations
 */

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

/*
 * Optional Operations
 */

int
mrb_hal_dir_seek(mrb_state *mrb, mrb_dir_handle *handle, long pos)
{
#if defined(__ANDROID__)
  /* Android doesn't have reliable seekdir */
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
  /* Android doesn't have reliable telldir */
  (void)mrb; (void)handle;
  errno = ENOSYS;
  return -1;
#else
  (void)mrb;
  return telldir(handle->dir);
#endif
}

/*
 * Filesystem Operations
 */

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

static int
cwd_errno(fmrb_err_t err)
{
  switch (err) {
  case FMRB_ERR_NOT_FOUND:     return ENOENT;
  case FMRB_ERR_INVALID_STATE: return ENOTDIR;
  case FMRB_ERR_INVALID_PARAM: return ENAMETOOLONG;
  default:                     return ENOSYS;
  }
}

int
mrb_hal_dir_chdir(mrb_state *mrb, const char *path)
{
  (void)mrb;
  fmrb_err_t err = fmrb_hal_file_chdir(path);
  if (err != FMRB_OK) {
    errno = cwd_errno(err);
    return -1;
  }
  return 0;
}

int
mrb_hal_dir_getcwd(mrb_state *mrb, char *buf, size_t size)
{
  (void)mrb;
  char cwd[FMRB_HAL_FILE_CWD_MAX];
  if (fmrb_hal_file_getcwd(cwd, sizeof(cwd)) != FMRB_OK) {
    errno = EIO;
    return -1;
  }
  size_t n = strlen(cwd);
  if (n + 1 > size) {
    errno = ERANGE;   /* Dir.getwd grows the buffer and asks again */
    return -1;
  }
  memcpy(buf, cwd, n + 1);
  return 0;
}

int
mrb_hal_dir_chroot(mrb_state *mrb, const char *path)
{
#if defined(__ANDROID__) || defined(__MSDOS__)
  /* Not available on these platforms */
  (void)mrb; (void)path;
  errno = ENOSYS;
  return -1;
#else
  (void)mrb;
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));
  return chroot(resolved);
#endif
}

int
mrb_hal_dir_is_directory(mrb_state *mrb, const char *path)
{
  struct stat sb;
  (void)mrb;
  char resolved[POSIX_PATH_MAX];
  resolve_path(path, resolved, sizeof(resolved));

  if (stat(resolved, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return 1;
  }
  return 0;
}

/*
 * HAL Initialization/Finalization
 */

void
mrb_hal_dir_init(mrb_state *mrb)
{
  (void)mrb;
  /* No initialization needed for POSIX */
}

void
mrb_hal_dir_final(mrb_state *mrb)
{
  (void)mrb;
  /* No cleanup needed for POSIX */
}
