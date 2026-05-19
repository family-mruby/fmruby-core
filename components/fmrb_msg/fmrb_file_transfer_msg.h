#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fmrb_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

// File transfer command types (internal message between app task and host_task)
typedef enum {
    FILE_CMD_TRANSFER = 0,  // Transfer file to graphics-audio LittleFS
    FILE_CMD_STATUS,        // Check if file exists on graphics-audio
    FILE_CMD_DELETE,        // Delete file on graphics-audio
    FILE_CMD_RMDIR,         // Recursively delete a directory (cache root only)
} file_cmd_type_t;

// Shared result structure (allocated by caller, pointed to by file_cmd_t)
typedef struct {
    fmrb_semaphore_t done_sem;  // Binary semaphore for synchronous completion
    int32_t result;             // Result code (set by host_task: 0=success, <0=error)
    union {
        struct {
            uint8_t exists;     // 0=not found, 1=exists
            uint32_t file_size;
            uint32_t checksum;  // CRC32
        } status;
        struct {
            uint32_t deleted_count;
            uint8_t  remote_status;  // Mirrors fmrb_link_file_transfer_rmdir_resp_t.status
        } rmdir;
    } data;
} file_cmd_result_t;

// File transfer command structure
// Fits within FMRB_MAX_MSG_PAYLOAD_SIZE (160 bytes)
typedef struct {
    file_cmd_type_t cmd_type;
    file_cmd_result_t *result;  // Pointer to caller-allocated result (survives msg copy)
    uint16_t path_len;
    char path[120];             // Destination path on graphics-audio
    union {
        struct {
            uint8_t *data;      // Heap-allocated file data (ownership -> host_task)
            uint32_t data_len;  // File data size in bytes
        } transfer;
    } params;
} file_cmd_t;

#ifdef __cplusplus
}
#endif
