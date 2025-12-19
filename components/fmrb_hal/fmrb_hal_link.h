#pragma once

#include "fmrb_hal.h"
#include "fmrb_link_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize link communication subsystem
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_link_init(void);

/**
 * @brief Deinitialize link communication subsystem
 */
void fmrb_hal_link_deinit(void);

/**
 * @brief Send message to specified channel
 * @param channel link communication channel
 * @param msg Message to send
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                              const fmrb_link_message_t *msg,
                              uint32_t timeout_ms);

/**
 * @brief Receive message from specified channel
 * @param channel link communication channel
 * @param msg Buffer to store received message
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_link_receive(fmrb_link_channel_t channel,
                                 fmrb_link_message_t *msg,
                                 uint32_t timeout_ms);

/**
 * @brief Register callback for link communication channel
 * @param channel link communication channel
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_link_register_callback(fmrb_link_channel_t channel,
                                           fmrb_link_callback_t callback,
                                           void *user_data);

/**
 * @brief Unregister callback for link communication channel
 * @param channel link communication channel
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_link_unregister_callback(fmrb_link_channel_t channel);

/**
 * @brief Get shared memory pointer
 * @param size Size of shared memory region
 * @return Pointer to shared memory, NULL on error
 */
void* fmrb_hal_link_get_shared_memory(size_t size);

/**
 * @brief Release shared memory
 * @param ptr Pointer to shared memory
 */
void fmrb_hal_link_release_shared_memory(void *ptr);

#ifdef __cplusplus
}
#endif
