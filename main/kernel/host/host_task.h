#pragma once

#include "fmrb_gfx_msg.h"
#include "fmrb_rtos.h"

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
 * @param scancode The scancode
 * @param modifier The modifier keys state
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_key_down(int key_code, int scancode, int modifier);

/**
 * @brief Send key up event
 * @param key_code The key code
 * @param scancode The scancode
 * @param modifier The modifier keys state
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_key_up(int key_code, int scancode, int modifier);

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

/**
 * @brief Send gamepad button event
 * @param gamepad_id Gamepad ID (0-1)
 * @param button_num Button number (0-15)
 * @param state Button state (1=pressed, 0=released)
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_gamepad_button(int gamepad_id, int button_num, int state);

/**
 * @brief Send gamepad axis event
 * @param gamepad_id Gamepad ID (0-1)
 * @param axis_num Axis number (0=LeftX, 1=LeftY, 2=RightX, 3=RightY, 4=L2, 5=R2)
 * @param value Axis value (-128 to 127 for sticks, 0 to 255 for triggers)
 * @return 0 on success, -1 on failure
 */
int fmrb_host_send_gamepad_axis(int gamepad_id, int axis_num, int value);

/**
 * @brief Get HOST task GFX queue semaphore for flow control
 * @return Semaphore handle, or NULL if not initialized
 *
 * This semaphore is used to prevent graphics commands from overflowing
 * the HOST message queue. App tasks should acquire this semaphore before
 * sending GFX messages to HOST task, and HOST task releases it after
 * processing each GFX command.
 *
 * HID events do NOT use this semaphore - they always have reserved space.
 */
fmrb_semaphore_t fmrb_host_get_gfx_queue_semaphore(void);

/**
 * @brief Allow the on-screen cursor to appear on the next mouse event.
 *
 * The cursor stays hidden after boot until this is called, so the boot
 * animation is not interrupted by a stray mouse cursor sprite. After this
 * call the next mouse-move event makes the cursor visible (one-shot).
 */
void fmrb_host_enable_cursor(void);

/**
 * @brief Read cumulative GFX counters (for runtime monitoring).
 * @param out_cmds Cumulative GFX command count since boot (may be NULL)
 * @param out_presents Cumulative GFX present() count since boot (may be NULL)
 *
 * Counters wrap modulo 2^32. Callers should compute rate from deltas between
 * successive samples; the delta computation wraps correctly as long as the
 * sampling interval is shorter than the wrap period.
 */
void fmrb_host_get_gfx_counters(uint32_t *out_cmds, uint32_t *out_presents);

#ifdef __cplusplus
}
#endif
