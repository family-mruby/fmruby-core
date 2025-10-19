#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the system task
 * @return 0 on success, -1 on failure
 */
int fmrb_system_task_init(void);

/**
 * Deinitialize the system task
 */
void fmrb_system_task_deinit(void);

/**
 * Send HID key down event
 * @param key_code Key code
 * @return 0 on success, -1 on failure
 */
int fmrb_system_send_key_down(int key_code);

/**
 * Send HID key up event
 * @param key_code Key code
 * @return 0 on success, -1 on failure
 */
int fmrb_system_send_key_up(int key_code);

/**
 * Send mouse move event
 * @param x X coordinate
 * @param y Y coordinate
 * @return 0 on success, -1 on failure
 */
int fmrb_system_send_mouse_move(int x, int y);

/**
 * Send mouse click event
 * @param x X coordinate
 * @param y Y coordinate
 * @param button Button index (0=left, 1=right, 2=middle)
 * @return 0 on success, -1 on failure
 */
int fmrb_system_send_mouse_click(int x, int y, int button);

#ifdef __cplusplus
}
#endif
