#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fmrb_err.h"

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

// File mode bits (POSIX-compatible)
#define FMRB_S_IFMT   0170000  // File type mask
#define FMRB_S_IFREG  0100000  // Regular file
#define FMRB_S_IFDIR  0040000  // Directory
#define FMRB_S_IFLNK  0120000  // Symbolic link

// File type checking macros
#define FMRB_S_ISREG(m)  (((m) & FMRB_S_IFMT) == FMRB_S_IFREG)
#define FMRB_S_ISDIR(m)  (((m) & FMRB_S_IFMT) == FMRB_S_IFDIR)
#define FMRB_S_ISLNK(m)  (((m) & FMRB_S_IFMT) == FMRB_S_IFLNK)

// Permission bits (for future use)
#define FMRB_S_IRWXU  0000700  // Owner: RWX
#define FMRB_S_IRUSR  0000400  // Owner: read
#define FMRB_S_IWUSR  0000200  // Owner: write
#define FMRB_S_IXUSR  0000100  // Owner: execute

// File info structure
typedef struct {
    char name[256];      // File/directory name
    uint32_t mode;       // File mode (POSIX-compatible: type + permissions)
    uint64_t size;       // File size in bytes
    bool is_dir;         // True if directory (deprecated, use FMRB_S_ISDIR(mode))
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

/**
 * @brief Get file size
 * @param handle File handle
 * @param size Output file size in bytes
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_size(fmrb_file_t handle, uint32_t *size);

/**
 * @brief Change current working directory
 * @param path Directory path
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_chdir(const char *path);

/**
 * @brief Get current working directory
 * @param buffer Buffer to store path
 * @param size Buffer size
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_getcwd(char *buffer, size_t size);

/**
 * @brief Change file modification time
 * @param path File path
 * @param mtime Modification time (Unix timestamp)
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_utime(const char *path, uint32_t mtime);

/**
 * @brief Change file attributes/permissions
 * @param path File path
 * @param attr Attributes (platform-specific)
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_chmod(const char *path, uint32_t attr);

/**
 * @brief Get filesystem statistics
 * @param path Path to mounted filesystem
 * @param total_bytes Total bytes on filesystem (can be NULL)
 * @param free_bytes Free bytes on filesystem (can be NULL)
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_file_statfs(const char *path, uint64_t *total_bytes, uint64_t *free_bytes);

/**
 * @brief Format filesystem
 * @param path Path to device/partition
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_mkfs(const char *path);

/**
 * @brief Get volume label
 * @param path Path to filesystem
 * @param label Buffer to store label (must be at least 12 bytes)
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_getlabel(const char *path, char *label);

/**
 * @brief Set volume label
 * @param path Path to filesystem
 * @param label New label (max 11 chars)
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_setlabel(const char *path, const char *label);

/**
 * @brief Get sector size
 * @return Sector size in bytes, or 0 if not supported
 */
uint32_t fmrb_hal_file_sector_size(void);

/**
 * @brief Get physical address of file data (for XIP)
 * @param handle File handle
 * @param addr Output physical address
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_physical_address(fmrb_file_t handle, uintptr_t *addr);

/**
 * @brief Erase storage volume
 * @param volume Volume name (e.g., "0:")
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_erase(const char *volume);

/**
 * @brief Check if file is stored contiguously
 * @param path File path
 * @param is_contiguous Output: true if contiguous
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if not available
 */
fmrb_err_t fmrb_hal_file_is_contiguous(const char *path, bool *is_contiguous);

/**
 * @brief Mount filesystem (if manual mount is required)
 * @param path Mount point path
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if auto-mounted
 */
fmrb_err_t fmrb_hal_file_mount(const char *path);

/**
 * @brief Unmount filesystem (if manual unmount is required)
 * @param path Mount point path
 * @return FMRB_OK on success, FMRB_ERR_NOT_SUPPORTED if auto-mounted
 */
fmrb_err_t fmrb_hal_file_unmount(const char *path);

#ifdef __cplusplus
}
#endif
