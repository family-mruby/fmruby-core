#pragma once

#include "fmrb_link_protocol.h"
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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
typedef void (*fmrb_link_transport_callback_t)(uint8_t type, uint8_t seq, uint8_t sub_cmd,
                                               const uint8_t *payload, uint32_t payload_len,
                                               void *user_data);

/**
 * @brief Initialize IPC transport
 * @param config Transport configuration
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_init(const fmrb_link_transport_config_t *config);

/**
 * @brief Deinitialize IPC transport
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_deinit(void);

/**
 * @brief Send message with automatic sequence numbering and retransmission
 * @param link_type Link type (FMRB_LINK_TYPE_CONTROL, FMRB_LINK_TYPE_GRAPHICS, etc.)
 * @param sub_cmd Sub-command within the link type
 * @param payload Payload data
 * @param payload_len Payload length
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_send(uint8_t link_type,
                                    uint8_t sub_cmd,
                                    const uint8_t *payload,
                                    uint32_t payload_len);

/**
 * @brief Send message synchronously and wait for ACK
 * @param link_type Link type (FMRB_LINK_TYPE_CONTROL, FMRB_LINK_TYPE_GRAPHICS, etc.)
 * @param sub_cmd Sub-command within the link type
 * @param payload Payload data
 * @param payload_len Payload length
 * @param response_payload Buffer for response payload (optional)
 * @param response_len Pointer to response length (optional)
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_send_sync(uint8_t link_type,
                                         uint8_t sub_cmd,
                                         const uint8_t *payload,
                                         uint32_t payload_len,
                                         uint8_t *response_payload,
                                         uint32_t *response_len,
                                         uint32_t timeout_ms);

/**
 * @brief Register callback for specific message type
 * @param msg_type Message type to listen for
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_register_callback(uint8_t msg_type,
                                                 fmrb_link_transport_callback_t callback,
                                                 void *user_data);

/**
 * @brief Unregister callback for message type
 * @param msg_type Message type
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_unregister_callback(uint8_t msg_type);

/**
 * @brief Process incoming messages (should be called regularly)
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_link_transport_process(void);

/**
 * @brief Get transport handle (for backward compatibility)
 * @return Transport handle or NULL if not initialized
 */
fmrb_link_transport_handle_t fmrb_link_transport_get_handle(void);

/**
 * @brief Check protocol version with remote
 * @param timeout_ms Timeout in milliseconds
 * @return FMRB_OK on success (version matched), error code otherwise
 */
fmrb_err_t fmrb_link_transport_check_version(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
