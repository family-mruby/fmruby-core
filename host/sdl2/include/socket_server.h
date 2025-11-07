#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start socket server
 * @return 0 on success, -1 on error
 */
int socket_server_start(void);

/**
 * @brief Stop socket server
 */
void socket_server_stop(void);

/**
 * @brief Process incoming socket messages
 * @return Number of messages processed
 */
int socket_server_process(void);

/**
 * @brief Check if server is running
 * @return 1 if running, 0 if not
 */
int socket_server_is_running(void);

/**
 * @brief Send ACK response with optional payload
 * @param type Message type
 * @param seq Sequence number
 * @param response_data Response payload data (can be NULL)
 * @param response_len Response payload length
 * @return 0 on success, -1 on error
 */
int socket_server_send_ack(uint8_t type, uint8_t seq, const uint8_t *response_data, uint16_t response_len);

#ifdef __cplusplus
}
#endif

#endif // SOCKET_SERVER_H