#pragma once

#include "fmrb_gfx_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the host task
 * @return 0 on success, -1 on failure
 */
int fmrb_host_task_init(void);

/**
 * @brief Deinitialize the host task
 */
void fmrb_host_task_deinit(void);

/**
 * @brief Send key down event
 * @param key_code The key code
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_key_down(int key_code);

/**
 * @brief Send key up event
 * @param key_code The key code
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_key_up(int key_code);

/**
 * @brief Send mouse move event
 * @param x X coordinate
 * @param y Y coordinate
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_mouse_move(int x, int y);

/**
 * @brief Send mouse click event
 * @param x X coordinate
 * @param y Y coordinate
 * @param button Mouse button number
 * @param state Button state (1=pressed, 0=released)
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_mouse_click(int x, int y, int button, int state);

#ifdef __cplusplus
}
#endif
