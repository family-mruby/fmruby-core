/*
** dir_hal.c - POSIX HAL for mruby-dir (family-mruby patch)
**
** Upstream passes paths straight to opendir/mkdir/stat/etc., so a virtual
** path like "/app" would hit the real host filesystem. The Linux dev
** environment serves the on-device "/" namespace from a local "flash/"
** directory (see components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c),
** so this patch applies the same "flash" prefix before delegating to the
** POSIX API, mirroring fmrb_hal_file_resolve_path(). getcwd does the reverse
** (strips the prefix) so Ruby sees virtual paths. The dev build has no SD
** card / virtual mount points, so no children synthesis.
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

/* On POSIX, mrb_dir_handle wraps DIR */
struct mrb_dir_handle {
  DIR *dir;
};

#define POSIX_BASE_PATH "flash"
#define POSIX_PATH_MAX  512

/* Map a virtual path ("/app", "app") to the host path under "flash/". */
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
  char host[POSIX_PATH_MAX];
  if (getcwd(host, sizeof(host)) == NULL) return -1;
  /* Reverse resolve_path: strip the "flash" prefix so Ruby sees virtual paths. */
  size_t base_len = strlen(POSIX_BASE_PATH);
  const char *virt = host;
  if (strncmp(host, POSIX_BASE_PATH, base_len) == 0 &&
      (host[base_len] == '/' || host[base_len] == '\0')) {
    virt = host + base_len;
    if (virt[0] == '\0') virt = "/";
  }
  if (strlen(virt) + 1 > size) { errno = ERANGE; return -1; }
  strcpy(buf, virt);
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
