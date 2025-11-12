#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

// Input handler log levels
typedef enum {
    INPUT_LOG_NONE = 0,
    INPUT_LOG_ERROR = 1,
    INPUT_LOG_INFO = 2,
    INPUT_LOG_DEBUG = 3,
} input_log_level_t;

void input_handler_set_log_level(int level);

/**
 * @brief Initialize input handler
 * @return 0 on success, -1 on failure
 */
int input_handler_init(void);

/**
 * @brief Process SDL keyboard and mouse events
 * @return 0 on success, -1 on failure, 1 if quit requested
 */
int input_handler_process_events(void);

/**
 * @brief Cleanup input handler
 */
void input_handler_cleanup(void);

/**
 * @brief Get current mouse position
 * @param x Pointer to store X coordinate
 * @param y Pointer to store Y coordinate
 * @return 0 on success, -1 on failure
 */
int input_handler_get_mouse_position(int* x, int* y);

#ifdef __cplusplus
}
#endif

#endif // INPUT_HANDLER_H
