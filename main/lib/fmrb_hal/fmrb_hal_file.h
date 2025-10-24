#pragma once

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// File handle type (opaque)
typedef void* fmrb_file_t;

// Directory handle type (opaque)
typedef void* fmrb_dir_t;

// File seek modes
typedef enum {
    FMRB_SEEK_SET = 0,  // Seek from beginning
    FMRB_SEEK_CUR = 1,  // Seek from current position
    FMRB_SEEK_END = 2   // Seek from end
} fmrb_seek_mode_t;

// File open flags
typedef enum {
    FMRB_O_RDONLY = 0x0001,  // Read only
    FMRB_O_WRONLY = 0x0002,  // Write only
    FMRB_O_RDWR   = 0x0004,  // Read and write
    FMRB_O_CREAT  = 0x0008,  // Create if not exists
    FMRB_O_TRUNC  = 0x0010,  // Truncate to zero length
    FMRB_O_APPEND = 0x0020   // Append mode
} fmrb_open_flags_t;

// File info structure
typedef struct {
    char name[256];      // File/directory name
    uint32_t size;       // File size in bytes
    bool is_dir;         // True if directory
    uint32_t mtime;      // Modification time (Unix timestamp)
} fmrb_file_info_t;

/**
 * @brief Initialize file system
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_init(void);

/**
 * @brief Deinitialize file system
 */
void fmrb_hal_file_deinit(void);

/**
 * @brief Open a file
 * @param path File path
 * @param flags Open flags (can be OR'd together)
 * @param out_handle Output file handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_open(const char *path, uint32_t flags, fmrb_file_t *out_handle);

/**
 * @brief Close a file
 * @param handle File handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_close(fmrb_file_t handle);

/**
 * @brief Read from file
 * @param handle File handle
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @param bytes_read Number of bytes actually read
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_read(fmrb_file_t handle, void *buffer, size_t size, size_t *bytes_read);

/**
 * @brief Write to file
 * @param handle File handle
 * @param buffer Buffer to write from
 * @param size Number of bytes to write
 * @param bytes_written Number of bytes actually written
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_write(fmrb_file_t handle, const void *buffer, size_t size, size_t *bytes_written);

/**
 * @brief Seek to position in file
 * @param handle File handle
 * @param offset Offset to seek
 * @param mode Seek mode
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_seek(fmrb_file_t handle, int32_t offset, fmrb_seek_mode_t mode);

/**
 * @brief Get current position in file
 * @param handle File handle
 * @param position Output position
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_tell(fmrb_file_t handle, uint32_t *position);

/**
 * @brief Delete a file
 * @param path File path
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_remove(const char *path);

/**
 * @brief Rename/move a file
 * @param old_path Old file path
 * @param new_path New file path
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_rename(const char *old_path, const char *new_path);

/**
 * @brief Get file information
 * @param path File path
 * @param info Output file info
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_stat(const char *path, fmrb_file_info_t *info);

/**
 * @brief Create a directory
 * @param path Directory path
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_mkdir(const char *path);

/**
 * @brief Remove a directory (must be empty)
 * @param path Directory path
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_rmdir(const char *path);

/**
 * @brief Open a directory for reading
 * @param path Directory path
 * @param out_handle Output directory handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_opendir(const char *path, fmrb_dir_t *out_handle);

/**
 * @brief Close a directory
 * @param handle Directory handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_closedir(fmrb_dir_t handle);

/**
 * @brief Read next entry from directory
 * @param handle Directory handle
 * @param info Output file info
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if no more entries
 */
fmrb_err_t fmrb_hal_file_readdir(fmrb_dir_t handle, fmrb_file_info_t *info);

/**
 * @brief Flush file buffers to storage
 * @param handle File handle
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_sync(fmrb_file_t handle);

#ifdef __cplusplus
}
#endif
