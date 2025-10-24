#include "fmrb_hal_file.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Maximum number of open files/directories
#define MAX_OPEN_FILES 10
#define MAX_OPEN_DIRS 10

// Internal file handle structure
typedef struct {
    FILE *fp;
    bool in_use;
} fmrb_file_slot_t;

// Internal directory handle structure
typedef struct {
    DIR *dir;
    bool in_use;
} fmrb_dir_slot_t;

// Static file and directory handle pools
static fmrb_file_slot_t s_file_slots[MAX_OPEN_FILES];
static fmrb_dir_slot_t s_dir_slots[MAX_OPEN_DIRS];

// Global mutex for thread safety
static SemaphoreHandle_t s_file_mutex = NULL;
#define LOCK() xSemaphoreTake(s_file_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_file_mutex)

// Base path for file operations
#define BASE_PATH "/littlefs"

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

// Find free file slot
static fmrb_file_slot_t* find_free_file_slot(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!s_file_slots[i].in_use) {
            return &s_file_slots[i];
        }
    }
    return NULL;
}

// Find free directory slot
static fmrb_dir_slot_t* find_free_dir_slot(void) {
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (!s_dir_slots[i].in_use) {
            return &s_dir_slots[i];
        }
    }
    return NULL;
}

// Validate file handle
static bool is_valid_file_handle(fmrb_file_t handle) {
    if (handle == NULL) {
        return false;
    }
    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    // Check if pointer is within our static array
    if (slot < s_file_slots || slot >= s_file_slots + MAX_OPEN_FILES) {
        return false;
    }
    return slot->in_use;
}

// Validate directory handle
static bool is_valid_dir_handle(fmrb_dir_t handle) {
    if (handle == NULL) {
        return false;
    }
    fmrb_dir_slot_t *slot = (fmrb_dir_slot_t *)handle;
    // Check if pointer is within our static array
    if (slot < s_dir_slots || slot >= s_dir_slots + MAX_OPEN_DIRS) {
        return false;
    }
    return slot->in_use;
}

// Initialize file system
fmrb_err_t fmrb_hal_file_init(void) {
    // Initialize file slots
    memset(s_file_slots, 0, sizeof(s_file_slots));
    memset(s_dir_slots, 0, sizeof(s_dir_slots));

    // Create mutex
    s_file_mutex = xSemaphoreCreateMutex();
    if (s_file_mutex == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }

    // Mount LittleFS
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        vSemaphoreDelete(s_file_mutex);
        s_file_mutex = NULL;
        return FMRB_ERR_FAILED;
    }

    return FMRB_OK;
}

// Deinitialize file system
void fmrb_hal_file_deinit(void) {
    if (s_file_mutex != NULL) {
        LOCK();

        // Close all open files
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (s_file_slots[i].in_use && s_file_slots[i].fp != NULL) {
                fclose(s_file_slots[i].fp);
                s_file_slots[i].in_use = false;
                s_file_slots[i].fp = NULL;
            }
        }

        // Close all open directories
        for (int i = 0; i < MAX_OPEN_DIRS; i++) {
            if (s_dir_slots[i].in_use && s_dir_slots[i].dir != NULL) {
                closedir(s_dir_slots[i].dir);
                s_dir_slots[i].in_use = false;
                s_dir_slots[i].dir = NULL;
            }
        }

        UNLOCK();

        esp_vfs_littlefs_unregister("storage");
        vSemaphoreDelete(s_file_mutex);
        s_file_mutex = NULL;
    }
}

// Open a file
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle) {
    if (path == NULL || out_handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    fmrb_file_slot_t *slot = find_free_file_slot();
    if (slot == NULL) {
        UNLOCK();
        return FMRB_ERR_BUSY;  // All slots are in use
    }

    char full_path[512];
    build_path(path, full_path, sizeof(full_path));

    char mode[8];
    flags_to_mode(flags, mode);

    slot->fp = fopen(full_path, mode);
    if (slot->fp == NULL) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    slot->in_use = true;
    *out_handle = (fmrb_file_t)slot;
    UNLOCK();
    return FMRB_OK;
}

// Close a file
fmrb_err_t fmrb_hal_file_close(fmrb_file_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    fclose(slot->fp);
    slot->fp = NULL;
    slot->in_use = false;

    UNLOCK();
    return FMRB_OK;
}

// Read from file
fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read) {
    if (handle == NULL || buffer == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    size_t n = fread(buffer, 1, size, slot->fp);
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

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    size_t n = fwrite(buffer, 1, size, slot->fp);
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

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    int ret = fseek(slot->fp, offset, whence);

    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}

// Get current position in file
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t handle, uint32_t *position) {
    if (handle == NULL || position == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    long pos = ftell(slot->fp);

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
    int ret = mkdir(full_path, 0755);
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

    fmrb_dir_slot_t *slot = find_free_dir_slot();
    if (slot == NULL) {
        UNLOCK();
        return FMRB_ERR_BUSY;  // All slots are in use
    }

    slot->dir = opendir(full_path);
    if (slot->dir == NULL) {
        UNLOCK();
        return FMRB_ERR_FAILED;
    }

    slot->in_use = true;
    *out_handle = (fmrb_dir_t)slot;

    UNLOCK();
    return FMRB_OK;
}

// Close a directory
fmrb_err_t fmrb_hal_file_closedir(fmrb_dir_t handle) {
    if (handle == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_dir_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_dir_slot_t *slot = (fmrb_dir_slot_t *)handle;
    closedir(slot->dir);
    slot->dir = NULL;
    slot->in_use = false;

    UNLOCK();
    return FMRB_OK;
}

// Read next entry from directory
fmrb_err_t fmrb_hal_file_readdir(fmrb_dir_t handle, fmrb_file_info_t *info) {
    if (handle == NULL || info == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    LOCK();

    if (!is_valid_dir_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_dir_slot_t *slot = (fmrb_dir_slot_t *)handle;
    struct dirent *entry = readdir(slot->dir);

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

    if (!is_valid_file_handle(handle)) {
        UNLOCK();
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_file_slot_t *slot = (fmrb_file_slot_t *)handle;
    int ret = fflush(slot->fp);

    UNLOCK();
    return (ret == 0) ? FMRB_OK : FMRB_ERR_FAILED;
}
