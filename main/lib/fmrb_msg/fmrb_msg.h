#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inter-Task Message Queue Registry
 *
 * This module provides a centralized message queue registry for inter-task
 * communication. Both host tasks and mrbgem tasks register their queues
 * and communicate using task IDs.
 *
 * Design principles:
 * - All tasks are equal (host and mrbgem use same API)
 * - ID-based addressing (task ID 0-15)
 * - Centralized registry management
 * - Thread-safe queue operations
 */

// Task ID type
typedef int32_t fmrb_msg_task_id_t;

// Reserved task IDs
#define FMRB_MSG_TASK_HOST    0   // Host task (HID, graphics, audio)
#define FMRB_MSG_TASK_SYSTEM  1   // System/Kernel task
// User application task IDs: 2-15

// Maximum number of tasks that can register queues
#define FMRB_MSG_MAX_TASKS    16

// Message structure
typedef struct {
    uint32_t type;        // Message type (application-defined)
    uint32_t size;        // Actual data size in bytes
    uint8_t data[64];     // Message payload
} fmrb_msg_t;

// Queue configuration
typedef struct {
    uint32_t queue_length;     // Number of messages the queue can hold
    uint32_t message_size;     // Size of each message (default: sizeof(fmrb_msg_t))
} fmrb_msg_queue_config_t;

// Queue statistics
typedef struct {
    uint32_t messages_sent;       // Total messages sent to this queue
    uint32_t messages_received;   // Total messages received from this queue
    uint32_t send_failures;       // Failed send attempts
    uint32_t current_waiting;     // Current messages waiting in queue
} fmrb_msg_queue_stats_t;

/**
 * @brief Initialize the message queue registry
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_msg_init(void);

/**
 * @brief Deinitialize the message queue registry
 */
void fmrb_msg_deinit(void);

/**
 * @brief Create and register a message queue for a task
 * @param task_id Task ID (0-15)
 * @param config Queue configuration (NULL for default: 10 messages)
 * @return FMRB_OK on success, error code otherwise
 *
 * If a queue is already registered for this task_id, returns FMRB_ERR_INVALID_STATE
 */
fmrb_err_t fmrb_msg_create_queue(fmrb_msg_task_id_t task_id,
                                      const fmrb_msg_queue_config_t *config);

/**
 * @brief Delete a task's message queue
 * @param task_id Task ID
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_msg_delete_queue(fmrb_msg_task_id_t task_id);

/**
 * @brief Send a message to a task's queue
 * @param dest_task_id Destination task ID
 * @param msg Message to send
 * @param timeout_ms Timeout in milliseconds (0 for no wait, UINT32_MAX for infinite)
 * @return FMRB_OK on success, FMRB_ERR_TIMEOUT on timeout, error code otherwise
 *
 * Returns FMRB_ERR_NOT_FOUND if destination task has no registered queue
 */
fmrb_err_t fmrb_msg_send(fmrb_msg_task_id_t dest_task_id,
                              const fmrb_msg_t *msg,
                              uint32_t timeout_ms);

/**
 * @brief Receive a message from a task's queue
 * @param task_id Task ID of the receiving task (usually current task)
 * @param msg Buffer to receive message
 * @param timeout_ms Timeout in milliseconds (0 for no wait, UINT32_MAX for infinite)
 * @return FMRB_OK on success, FMRB_ERR_TIMEOUT on timeout, error code otherwise
 *
 * Returns FMRB_ERR_NOT_FOUND if task has no registered queue
 */
fmrb_err_t fmrb_msg_receive(fmrb_msg_task_id_t task_id,
                                 fmrb_msg_t *msg,
                                 uint32_t timeout_ms);

/**
 * @brief Broadcast a message to all registered task queues
 * @param msg Message to broadcast
 * @param timeout_ms Timeout in milliseconds per queue
 * @return Number of tasks that successfully received the message
 *
 * Sends to all registered queues. If a send fails (timeout/full queue),
 * continues to other queues and returns count of successful sends.
 */
int fmrb_msg_broadcast(const fmrb_msg_t *msg, uint32_t timeout_ms);

/**
 * @brief Check if a task has a registered queue
 * @param task_id Task ID to check
 * @return true if queue exists, false otherwise
 */
bool fmrb_msg_queue_exists(fmrb_msg_task_id_t task_id);

/**
 * @brief Get queue statistics for a task
 * @param task_id Task ID
 * @param stats Output statistics structure
 * @return FMRB_OK on success, FMRB_ERR_NOT_FOUND if queue doesn't exist
 */
fmrb_err_t fmrb_msg_get_stats(fmrb_msg_task_id_t task_id,
                                   fmrb_msg_queue_stats_t *stats);

#ifdef __cplusplus
}
#endif
