#include "fmrb_hal.h"
#include "fmrb_hal_file.h"
#include "fmrb_rtos.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <errno.h>

const char* TAG = "file_posix";

// Internal file handle structure
typedef struct {
    FILE *fp;
} fmrb_file_handle_t;

// Internal directory handle structure
typedef struct {
    DIR *dir;
    char dir_path[512];  // Store directory path for stat() calls in readdir
} fmrb_dir_handle_t;

// Global mutex for thread safety
static fmrb_semaphore_t s_file_mutex = NULL;
#define LOCK() fmrb_semaphore_take(s_file_mutex, FMRB_TICK_MAX)
#define UNLOCK() fmrb_semaphore_give(s_file_mutex)

// Base path for file operations - mount data/ directory
#define BASE_PATH "flash"

// Helper function to build full path
static void build_path(const char *path, char *full_path, size_t max_len) {
    if (path[0] == '/') {
        snprintf(full_path, max_len, "%s%s", BASE_PATH, path);
    } else {
        snprintf(full_path, max_len, "%s/%s", BASE_PATH, path);
    }
}

// Convert flags to POSIX mode string
static void flags_to_mode(uint32_t flags, char *mode) {
    if (flags & FMRB_O_RDWR) {
        if (flags & FMRB_O_CREAT) {
            if (flags & FMRB_O_TRUNC) {
                strcpy(mode, "w+");
            } else {
                strcpy(mode, "r+");
            }
        } else {
            strcpy(mode, "r+");
        }
    } else if (flags & FMRB_O_WRONLY) {
        if (flags & FMRB_O_APPEND) {
            strcpy(mode, "a");
        } else if (flags & FMRB_O_TRUNC) {
            strcpy(mode, "w");
        } else {
            strcpy(mode, "r+");
        }
    } else {
        strcpy(mode, "r");
    }

    // Add binary mode
    strcat(mode, "b");
}

// Helper function to create directory recursively
static int mkdir_recursive(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

// Initialize file system
fmrb_err_t fmrb_hal_file_init(void) {
    // Create mutex for file operations
    s_file_mutex = fmrb_semaphore_create_mutex();
    if (s_file_mutex == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }

    // Create base directory if it doesn't exist
    if (mkdir_recursive(BASE_PATH) != 0 && errno != EEXIST) {
        fmrb_semaphore_delete(s_file_mutex);
        s_file_mutex = NULL;
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

// Deinitialize file system
void fmrb_hal_file_deinit(void) {
    if (s_file_mutex != NULL) {
        fmrb_semaphore_delete(s_file_mutex);
        s_file_mutex = NULL;
    }
}

// Open a file
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    fmrb_file_handle_t *handle = fmrb_sys_malloc(sizeof(fmrb_file_handle_t));
    if (handle == NULL) {
        UNLOCK();
        return FMRB_ERR_NO_MEMORY;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    // Create parent directory if needed
    if (flags & FMRB_O_CREAT) {
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "%s", full_path);
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            mkdir_recursive(dir_path);
        }
    }

    char mode[8];
    flags_to_mode(flags, mode);

    handle->fp = fopen(full_path, mode);
    if (handle->fp == NULL) {
        fmrb_sys_free(handle);
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    *out_handle = handle;
    UNLOCK();
    return FMRB_OK;
}

// Close a file
fmrb_err_t fmrb_hal_file_close(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    fclose(fh->fp);
    fmrb_sys_free(fh);
    UNLOCK();
    return FMRB_OK;
}

// Read from file
fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    size_t n = fread(buffer, 1, size, fh->fp);
    if (bytes_read != NULL) {
        *bytes_read = n;
    }
    UNLOCK();
    return FMRB_OK;
}

// Write to file
fmrb_err_t fmrb_hal_file_write(fmrb_file_t handle, const void *buffer, size_t size, size_t *bytes_written) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    size_t n = fwrite(buffer, 1, size, fh->fp);
    if (bytes_written != NULL) {
        *bytes_written = n;
    }
    UNLOCK();
    return (n == size) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Seek to position in file
fmrb_err_t fmrb_hal_file_seek(fmrb_file_t handle, int32_t offset, fmrb_seek_mode_t mode) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    int whence;
    switch (mode) {
        case FMRB_SEEK_SET: whence = SEEK_SET; break;
        case FMRB_SEEK_CUR: whence = SEEK_CUR; break;
        case FMRB_SEEK_END: whence = SEEK_END; break;
        default: return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    int ret = fseek(fh->fp, offset, whence);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current position in file
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t handle, uint32_t *position) {
    if (handle == NULL || position == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    long pos = ftell(fh->fp);
    UNLOCK();

    if (pos < 0) {
        return FMRB_ERR_FAILED;
    }

    *position = (uint32_t)pos;
    return FMRB_OK;
}

// Delete a file
fmrb_err_t fmrb_hal_file_remove(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = unlink(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Rename/move a file
fmrb_err_t fmrb_hal_file_rename(const char *old_path, const char *new_path) {
    if (old_path == NULL || new_path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char old_full_path[512];
    char new_full_path[512];
    build_path(old_path, old_full_path, sizeof(old_full_path));
    build_path(new_path, new_full_path, sizeof(new_full_path));

    LOCK();
    int ret = rename(old_full_path, new_full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get file information
fmrb_err_t fmrb_hal_file_stat(const char *path, fmrb_file_info_t *info) {
    if (path == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));
    FMRB_LOGI(TAG, "path:%s full_path:%s",path,full_path);

    LOCK();
    struct stat st;
    int ret = stat(full_path, &st);
    UNLOCK();

    if (ret != 0) {
        return FMRB_ERR_FAILED;
    }

    memset(info, 0, sizeof(fmrb_file_info_t));

    // Extract basename from path
    const char *basename = strrchr(path, '/');
    if (basename != NULL) {
        basename++;
    } else {
        basename = path;
    }
    snprintf(info->name, sizeof(info->name), "%s", basename);

    info->size = st.st_size;
    info->is_dir = S_ISDIR(st.st_mode);
    info->mtime = st.st_mtime;

    return FMRB_OK;
}

// Create a directory
fmrb_err_t fmrb_hal_file_mkdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = mkdir_recursive(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Remove a directory
fmrb_err_t fmrb_hal_file_rmdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = rmdir(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Open a directory for reading
fmrb_err_t fmrb_hal_file_opendir(const char *path, fmrb_dir_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    fmrb_dir_handle_t *handle = fmrb_sys_malloc(sizeof(fmrb_dir_handle_t));
    if (handle == NULL) {
        UNLOCK();
        return FMRB_ERR_NO_MEMORY;
    }

    handle->dir = opendir(full_path);
    if (handle->dir == NULL) {
        fmrb_sys_free(handle);
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    // Store directory path for use in readdir
    strncpy(handle->dir_path, full_path, sizeof(handle->dir_path) - 1);
    handle->dir_path[sizeof(handle->dir_path) - 1] = '\0';

    *out_handle = handle;
    UNLOCK();
    return FMRB_OK;
}

// Close a directory
fmrb_err_t fmrb_hal_file_closedir(fmrb_dir_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_dir_handle_t *dh = (fmrb_dir_handle_t *)handle;
    closedir(dh->dir);
    fmrb_sys_free(dh);
    UNLOCK();
    return FMRB_OK;
}

// Read next entry from directory
fmrb_err_t fmrb_hal_file_readdir(fmrb_dir_t handle, fmrb_file_info_t *info) {
    if (handle == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_dir_handle_t *dh = (fmrb_dir_handle_t *)handle;
    struct dirent *entry = readdir(dh->dir);
    UNLOCK();

    if (entry == NULL) {
        return FMRB_ERR_NOT_SUPPORTED;  // No more entries
    }

    memset(info, 0, sizeof(fmrb_file_info_t));
    snprintf(info->name, sizeof(info->name), "%s", entry->d_name);
    info->is_dir = (entry->d_type == DT_DIR);

    // Get file size and mtime using stat()
    char entry_path[1024];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", dh->dir_path, entry->d_name);

    struct stat st;
    if (stat(entry_path, &st) == 0) {
        // For directories, show size as 0 (directory metadata size is not meaningful for users)
        info->size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
        info->mtime = st.st_mtime;
    } else {
        info->size = 0;
        info->mtime = 0;
    }

    return FMRB_OK;
}

// Flush file buffers to storage
fmrb_err_t fmrb_hal_file_sync(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    int ret = fflush(fh->fp);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get file size
fmrb_err_t fmrb_hal_file_size(fmrb_file_t handle, uint32_t *size) {
    if (handle == NULL || size == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    fmrb_file_handle_t *fh = (fmrb_file_handle_t *)handle;
    long current = ftell(fh->fp);
    if (current < 0) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    if (fseek(fh->fp, 0, SEEK_END) != 0) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    long file_size = ftell(fh->fp);
    fseek(fh->fp, current, SEEK_SET);  // Restore position
    UNLOCK();

    if (file_size < 0) {
        return FMRB_ERR_FAILED;
    }

    *size = (uint32_t)file_size;
    return FMRB_OK;
}

// Change current working directory
fmrb_err_t fmrb_hal_file_chdir(const char *path) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    LOCK();
    int ret = chdir(full_path);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current working directory
fmrb_err_t fmrb_hal_file_getcwd(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();
    char *ret = getcwd(buffer, size);
    UNLOCK();
    return (ret != NULL) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Change file modification time
fmrb_err_t fmrb_hal_file_utime(const char *path, uint32_t mtime) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    struct utimbuf times;
    times.actime = mtime;
    times.modtime = mtime;

    LOCK();
    int ret = utime(full_path, &times);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Change file attributes/permissions
fmrb_err_t fmrb_hal_file_chmod(const char *path, uint32_t attr) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    // On POSIX, attr is mode_t
    LOCK();
    int ret = chmod(full_path, (mode_t)attr);
    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get filesystem statistics
fmrb_err_t fmrb_hal_file_statfs(const char *path, uint64_t *total_bytes, uint64_t *free_bytes) {
    if (path == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    struct statvfs stat;
    LOCK();
    int ret = statvfs(full_path, &stat);
    UNLOCK();

    if (ret != 0) {
        return FMRB_ERR_FAILED;
    }

    if (total_bytes != NULL) {
        *total_bytes = (uint64_t)stat.f_blocks * stat.f_frsize;
    }
    if (free_bytes != NULL) {
        *free_bytes = (uint64_t)stat.f_bavail * stat.f_frsize;
    }

    return FMRB_OK;
}

// Format filesystem - not supported on POSIX
fmrb_err_t fmrb_hal_file_mkfs(const char *path) {
    (void)path;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Get volume label - not supported on POSIX
fmrb_err_t fmrb_hal_file_getlabel(const char *path, char *label) {
    (void)path;
    (void)label;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Set volume label - not supported on POSIX
fmrb_err_t fmrb_hal_file_setlabel(const char *path, const char *label) {
    (void)path;
    (void)label;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Get sector size - return 512 as default
uint32_t fmrb_hal_file_sector_size(void) {
    return 512;  // Standard sector size
}

// Get physical address - not supported on POSIX
fmrb_err_t fmrb_hal_file_physical_address(fmrb_file_t handle, uintptr_t *addr) {
    (void)handle;
    (void)addr;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Erase storage volume - not supported on POSIX
fmrb_err_t fmrb_hal_file_erase(const char *volume) {
    (void)volume;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Check if file is stored contiguously - not supported on POSIX
fmrb_err_t fmrb_hal_file_is_contiguous(const char *path, bool *is_contiguous) {
    (void)path;
    (void)is_contiguous;
    return FMRB_ERR_NOT_SUPPORTED;
}

// Mount filesystem - not needed on POSIX
fmrb_err_t fmrb_hal_file_mount(const char *path) {
    (void)path;
    return FMRB_ERR_NOT_SUPPORTED;  // Auto-mounted
}

// Unmount filesystem - not needed on POSIX
fmrb_err_t fmrb_hal_file_unmount(const char *path) {
    (void)path;
    return FMRB_ERR_NOT_SUPPORTED;  // Auto-mounted
}
