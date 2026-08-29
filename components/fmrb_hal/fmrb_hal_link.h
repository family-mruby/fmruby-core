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
 * @brief Send messages to specified channel (batch)
 *
 * Multiple messages are packed into SPI frames (up to SPI_MAX_DATA per frame).
 * ACK is waited per frame. For single message, pass msg_count=1.
 *
 * @param channel link communication channel
 * @param msgs Array of messages to send
 * @param msg_count Number of messages in the array
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_link_send(fmrb_link_channel_t channel,
                              const fmrb_link_message_t *msgs,
                              size_t msg_count,
                              uint32_t timeout_ms);

/**
 * @brief Send messages without waiting for ACK (fire-and-forget)
 *
 * Same batching as fmrb_hal_link_send(), but returns immediately after TX.
 * Use for non-critical commands where frame-level ACK is not required.
 */
fmrb_err_t fmrb_hal_link_send_noack(fmrb_link_channel_t channel,
                                     const fmrb_link_message_t *msgs,
                                     size_t msg_count,
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

// Extended API for the local Message Buffer link (ATOM_DISPLAY, Modern P4,
// and the wasm build, whose in-process display task reuses the same link).
// The in-process display task uses these to read commands from Core and send
// ACK responses back.
#if defined(FMRB_HW_ATOM_DISPLAY) || defined(FMRB_HW_MODERN) || defined(FMRB_PLATFORM_WASM)
fmrb_err_t fmrb_hal_link_local_receive_cmd(fmrb_link_channel_t channel,
                                            fmrb_link_message_t *msg,
                                            uint32_t timeout_ms);
fmrb_err_t fmrb_hal_link_local_send_response(fmrb_link_channel_t channel,
                                              const fmrb_link_message_t *msg,
                                              uint32_t timeout_ms);
#endif

#ifdef __cplusplus
}
#endif
