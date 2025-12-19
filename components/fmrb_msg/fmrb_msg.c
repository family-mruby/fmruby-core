#include "fmrb_msg.h"
#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include <string.h>

// Registry entry for each task's message queue
typedef struct {
    fmrb_queue_t queue;               // FreeRTOS queue handle
    bool registered;                   // Whether this entry is in use
    uint32_t message_size;             // Size of messages in this queue
    fmrb_msg_queue_stats_t stats;     // Statistics
} msg_queue_entry_t;

// Global registry
static msg_queue_entry_t g_msg_queues[FMRB_MAX_APPS];
static fmrb_semaphore_t g_registry_lock = NULL;
static bool g_initialized = false;

// Default queue configuration
static const fmrb_msg_queue_config_t DEFAULT_CONFIG = {
    .queue_length = 10,
    .message_size = sizeof(fmrb_msg_t)
};

/**
 * @brief Initialize the message queue registry
 */
fmrb_err_t fmrb_msg_init(void)
{
    if (g_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    // Create registry lock
    g_registry_lock = fmrb_semaphore_create_mutex();
    if (g_registry_lock == NULL) {
        return FMRB_ERR_NO_MEMORY;
    }

    // Initialize all entries
    memset(g_msg_queues, 0, sizeof(g_msg_queues));
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        g_msg_queues[i].registered = false;
        g_msg_queues[i].queue = NULL;
    }

    g_initialized = true;
    return FMRB_OK;
}

/**
 * @brief Deinitialize the message queue registry
 */
void fmrb_msg_deinit(void)
{
    if (!g_initialized) {
        return;
    }

    // Delete all queues
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        if (g_msg_queues[i].registered && g_msg_queues[i].queue != NULL) {
            fmrb_queue_delete(g_msg_queues[i].queue);
            g_msg_queues[i].queue = NULL;
            g_msg_queues[i].registered = false;
        }
    }

    // Delete lock
    if (g_registry_lock != NULL) {
        fmrb_semaphore_delete(g_registry_lock);
        g_registry_lock = NULL;
    }

    g_initialized = false;
}

/**
 * @brief Create and register a message queue for a task
 */
fmrb_err_t fmrb_msg_create_queue(fmrb_proc_id_t task_id,
                                      const fmrb_msg_queue_config_t *config)
{
    if (!g_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (task_id < 0 || task_id >= FMRB_MAX_APPS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Use default config if not provided
    if (config == NULL) {
        config = &DEFAULT_CONFIG;
    }

    // Validate config
    if (config->queue_length == 0 || config->message_size == 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Lock registry
    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    fmrb_err_t result = FMRB_OK;

    // Check if already registered
    if (g_msg_queues[task_id].registered) {
        result = FMRB_ERR_INVALID_STATE;
        goto cleanup;
    }

    // Create queue
    fmrb_queue_t queue = fmrb_queue_create(config->queue_length, config->message_size);
    if (queue == NULL) {
        result = FMRB_ERR_NO_MEMORY;
        goto cleanup;
    }

    // Register queue
    g_msg_queues[task_id].queue = queue;
    g_msg_queues[task_id].registered = true;
    g_msg_queues[task_id].message_size = config->message_size;
    memset(&g_msg_queues[task_id].stats, 0, sizeof(fmrb_msg_queue_stats_t));

cleanup:
    fmrb_semaphore_give(g_registry_lock);
    return result;
}

/**
 * @brief Delete a task's message queue
 */
fmrb_err_t fmrb_msg_delete_queue(fmrb_proc_id_t task_id)
{
    if (!g_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (task_id < 0 || task_id >= FMRB_MAX_APPS) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Lock registry
    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    fmrb_err_t result = FMRB_OK;

    if (!g_msg_queues[task_id].registered) {
        result = FMRB_ERR_NOT_FOUND;
        goto cleanup;
    }

    // Delete queue
    if (g_msg_queues[task_id].queue != NULL) {
        fmrb_queue_delete(g_msg_queues[task_id].queue);
        g_msg_queues[task_id].queue = NULL;
    }

    g_msg_queues[task_id].registered = false;

cleanup:
    fmrb_semaphore_give(g_registry_lock);
    return result;
}

/**
 * @brief Send a message to a task's queue
 */
fmrb_err_t fmrb_msg_send(fmrb_proc_id_t dest_task_id,
                              const fmrb_msg_t *msg,
                              uint32_t timeout_ms)
{
    if (!g_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (dest_task_id < 0 || dest_task_id >= FMRB_MAX_APPS || msg == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Lock registry for read
    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    // Check if queue exists
    if (!g_msg_queues[dest_task_id].registered) {
        fmrb_semaphore_give(g_registry_lock);
        return FMRB_ERR_NOT_FOUND;
    }

    fmrb_queue_t queue = g_msg_queues[dest_task_id].queue;
    fmrb_semaphore_give(g_registry_lock);

    // Send to queue (outside lock to avoid blocking other operations)
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);
    fmrb_base_type_t send_result = fmrb_queue_send(queue, msg, ticks);

    // Update statistics
    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) == FMRB_TRUE) {
        if (send_result == FMRB_TRUE) {
            g_msg_queues[dest_task_id].stats.messages_sent++;
        } else {
            g_msg_queues[dest_task_id].stats.send_failures++;
        }
        fmrb_semaphore_give(g_registry_lock);
    }

    return (send_result == FMRB_TRUE) ? FMRB_OK : FMRB_ERR_TIMEOUT;
}

/**
 * @brief Receive a message from a task's queue
 */
fmrb_err_t fmrb_msg_receive(fmrb_proc_id_t task_id,
                                 fmrb_msg_t *msg,
                                 uint32_t timeout_ms)
{
    if (!g_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (task_id < 0 || task_id >= FMRB_MAX_APPS || msg == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Lock registry for read
    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    // Check if queue exists
    if (!g_msg_queues[task_id].registered) {
        fmrb_semaphore_give(g_registry_lock);
        return FMRB_ERR_NOT_FOUND;
    }

    fmrb_queue_t queue = g_msg_queues[task_id].queue;
    fmrb_semaphore_give(g_registry_lock);

    // Receive from queue (outside lock to avoid blocking)
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);
    fmrb_base_type_t recv_result = fmrb_queue_receive(queue, msg, ticks);

    // Update statistics
    if (recv_result == FMRB_TRUE) {
        if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) == FMRB_TRUE) {
            g_msg_queues[task_id].stats.messages_received++;
            fmrb_semaphore_give(g_registry_lock);
        }
    }

    return (recv_result == FMRB_TRUE) ? FMRB_OK : FMRB_ERR_TIMEOUT;
}

/**
 * @brief Broadcast a message to all registered task queues
 */
int fmrb_msg_broadcast(const fmrb_msg_t *msg, uint32_t timeout_ms)
{
    if (!g_initialized || msg == NULL) {
        return 0;
    }

    int success_count = 0;
    fmrb_tick_t ticks = (timeout_ms == UINT32_MAX) ? FMRB_TICK_MAX : FMRB_MS_TO_TICKS(timeout_ms);

    // Lock registry
    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
        return 0;
    }

    // Send to all registered queues
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        if (g_msg_queues[i].registered && g_msg_queues[i].queue != NULL) {
            fmrb_queue_t queue = g_msg_queues[i].queue;

            // Send without blocking the registry for too long
            fmrb_semaphore_give(g_registry_lock);
            fmrb_base_type_t result = fmrb_queue_send(queue, msg, ticks);

            // Re-lock to update stats and continue iteration
            if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
                break;
            }

            if (result == FMRB_TRUE) {
                success_count++;
                g_msg_queues[i].stats.messages_sent++;
            } else {
                g_msg_queues[i].stats.send_failures++;
            }
        }
    }

    fmrb_semaphore_give(g_registry_lock);
    return success_count;
}

/**
 * @brief Check if a task has a registered queue
 */
bool fmrb_msg_queue_exists(fmrb_proc_id_t task_id)
{
    if (!g_initialized || task_id < 0 || task_id >= FMRB_MAX_APPS) {
        return false;
    }

    bool exists = false;

    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) == FMRB_TRUE) {
        exists = g_msg_queues[task_id].registered;
        fmrb_semaphore_give(g_registry_lock);
    }

    return exists;
}

/**
 * @brief Get queue statistics for a task
 */
fmrb_err_t fmrb_msg_get_stats(fmrb_proc_id_t task_id,
                                   fmrb_msg_queue_stats_t *stats)
{
    if (!g_initialized) {
        return FMRB_ERR_INVALID_STATE;
    }

    if (task_id < 0 || task_id >= FMRB_MAX_APPS || stats == NULL) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (fmrb_semaphore_take(g_registry_lock, FMRB_TICK_MAX) != FMRB_TRUE) {
        return FMRB_ERR_TIMEOUT;
    }

    fmrb_err_t result = FMRB_OK;

    if (!g_msg_queues[task_id].registered) {
        result = FMRB_ERR_NOT_FOUND;
    } else {
        memcpy(stats, &g_msg_queues[task_id].stats, sizeof(fmrb_msg_queue_stats_t));
        // Note: current_waiting would require FreeRTOS queue inspection APIs
        // which are not always available. Set to 0 for now.
        stats->current_waiting = 0;
    }

    fmrb_semaphore_give(g_registry_lock);
    return result;
}
