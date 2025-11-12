/**
 * @file input_socket.h
 * @brief Unix socket server for HID input events (separate from GFX socket)
 */

#ifndef INPUT_SOCKET_H
#define INPUT_SOCKET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start input socket server
 * @return 0 on success, -1 on error
 */
int input_socket_start(void);

/**
 * @brief Stop input socket server
 */
void input_socket_stop(void);

/**
 * @brief Send HID event to Core via socket
 * @param type Event type (HID_EVENT_*)
 * @param data Event data
 * @param len Event data length
 * @return 0 on success, -1 on error
 */
int input_socket_send_event(uint8_t type, const void* data, uint16_t len);

/**
 * @brief Check if client is connected
 * @return 1 if connected, 0 if not
 */
int input_socket_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // INPUT_SOCKET_H
