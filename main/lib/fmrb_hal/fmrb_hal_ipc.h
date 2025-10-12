#ifndef FMRB_HAL_IPC_H
#define FMRB_HAL_IPC_H

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize IPC subsystem
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_ipc_init(void);

/**
 * @brief Deinitialize IPC subsystem
 */
void fmrb_hal_ipc_deinit(void);

/**
 * @brief Send message to specified channel
 * @param channel IPC channel
 * @param msg Message to send
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_ipc_send(fmrb_ipc_channel_t channel,
                              const fmrb_ipc_message_t *msg,
                              uint32_t timeout_ms);

/**
 * @brief Receive message from specified channel
 * @param channel IPC channel
 * @param msg Buffer to store received message
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_ipc_receive(fmrb_ipc_channel_t channel,
                                 fmrb_ipc_message_t *msg,
                                 uint32_t timeout_ms);

/**
 * @brief Register callback for IPC channel
 * @param channel IPC channel
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_ipc_register_callback(fmrb_ipc_channel_t channel,
                                           fmrb_ipc_callback_t callback,
                                           void *user_data);

/**
 * @brief Unregister callback for IPC channel
 * @param channel IPC channel
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_ipc_unregister_callback(fmrb_ipc_channel_t channel);

/**
 * @brief Get shared memory pointer
 * @param size Size of shared memory region
 * @return Pointer to shared memory, NULL on error
 */
void* fmrb_hal_ipc_get_shared_memory(size_t size);

/**
 * @brief Release shared memory
 * @param ptr Pointer to shared memory
 */
void fmrb_hal_ipc_release_shared_memory(void *ptr);

#ifdef __cplusplus
}
#endif

#endif // FMRB_HAL_IPC_H