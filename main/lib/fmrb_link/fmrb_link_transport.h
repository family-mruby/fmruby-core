#pragma once

#include "fmrb_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Transport error codes
typedef enum {
    FMRB_LINK_TRANSPORT_OK = 0,
    FMRB_LINK_TRANSPORT_ERR_INVALID_PARAM = -1,
    FMRB_LINK_TRANSPORT_ERR_NO_MEMORY = -2,
    FMRB_LINK_TRANSPORT_ERR_TIMEOUT = -3,
    FMRB_LINK_TRANSPORT_ERR_FAILED = -4,
    FMRB_LINK_TRANSPORT_ERR_CHECKSUM = -5,
    FMRB_LINK_TRANSPORT_ERR_SEQUENCE = -6
} fmrb_link_transport_err_t;

// Transport handle
#ifndef FMRB_LINK_TRANSPORT_HANDLE_DEFINED
#define FMRB_LINK_TRANSPORT_HANDLE_DEFINED
typedef void* fmrb_link_transport_handle_t;
#endif

// Transport configuration
typedef struct {
    uint32_t timeout_ms;
    bool enable_retransmit;
    uint8_t max_retries;
    uint16_t window_size;
} fmrb_link_transport_config_t;

// Message callback
typedef void (*fmrb_link_transport_callback_t)(const fmrb_link_header_t *header, const uint8_t *payload, void *user_data);

/**
 * @brief Initialize IPC transport
 * @param config Transport configuration
 * @param handle Pointer to store transport handle
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_init(const fmrb_link_transport_config_t *config,
                                                  fmrb_link_transport_handle_t *handle);

/**
 * @brief Deinitialize IPC transport
 * @param handle Transport handle
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_deinit(fmrb_link_transport_handle_t handle);

/**
 * @brief Send message with automatic sequence numbering and retransmission
 * @param handle Transport handle
 * @param msg_type Message type
 * @param payload Payload data
 * @param payload_len Payload length
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_send(fmrb_link_transport_handle_t handle,
                                                  uint8_t msg_type,
                                                  const uint8_t *payload,
                                                  uint32_t payload_len);

/**
 * @brief Send message synchronously and wait for ACK
 * @param handle Transport handle
 * @param msg_type Message type
 * @param payload Payload data
 * @param payload_len Payload length
 * @param response_payload Buffer for response payload (optional)
 * @param response_len Pointer to response length (optional)
 * @param timeout_ms Timeout in milliseconds
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_send_sync(fmrb_link_transport_handle_t handle,
                                                       uint8_t msg_type,
                                                       const uint8_t *payload,
                                                       uint32_t payload_len,
                                                       uint8_t *response_payload,
                                                       uint32_t *response_len,
                                                       uint32_t timeout_ms);

/**
 * @brief Register callback for specific message type
 * @param handle Transport handle
 * @param msg_type Message type to listen for
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_register_callback(fmrb_link_transport_handle_t handle,
                                                               uint8_t msg_type,
                                                               fmrb_link_transport_callback_t callback,
                                                               void *user_data);

/**
 * @brief Unregister callback for message type
 * @param handle Transport handle
 * @param msg_type Message type
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_unregister_callback(fmrb_link_transport_handle_t handle,
                                                                 uint8_t msg_type);

/**
 * @brief Process incoming messages (should be called regularly)
 * @param handle Transport handle
 * @return Transport error code
 */
fmrb_link_transport_err_t fmrb_link_transport_process(fmrb_link_transport_handle_t handle);

#ifdef __cplusplus
}
#endif
