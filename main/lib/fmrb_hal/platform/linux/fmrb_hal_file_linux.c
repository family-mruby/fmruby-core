#include "fmrb_hal_file.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

// Internal file handle structure
typedef struct {
    FILE *fp;
} fmrb_file_handle_t;

// Internal directory handle structure
typedef struct {
    DIR *dir;
} fmrb_dir_handle_t;

// Global mutex for thread safety
static pthread_mutex_t s_file_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&s_file_mutex)
#define UNLOCK() pthread_mutex_unlock(&s_file_mutex)

// Base path for file operations - mount data/ directory
#define BASE_PATH "data"

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
    // Create base directory if it doesn't exist
    if (mkdir_recursive(BASE_PATH) != 0 && errno != EEXIST) {
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

// Deinitialize file system
void fmrb_hal_file_deinit(void) {
    // Nothing to do for Linux
}

// Open a file
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    fmrb_file_handle_t *handle = malloc(sizeof(fmrb_file_handle_t));
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
        free(handle);
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
    free(fh);
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
    fmrb_dir_handle_t *handle = malloc(sizeof(fmrb_dir_handle_t));
    if (handle == NULL) {
        UNLOCK();
        return FMRB_ERR_NO_MEMORY;
    }

    handle->dir = opendir(full_path);
    if (handle->dir == NULL) {
        free(handle);
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

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
    free(dh);
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
    info->size = 0;
    info->mtime = 0;

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
