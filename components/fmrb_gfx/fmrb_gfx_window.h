#pragma once

#include "fmrb_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Window handle
typedef void* fmrb_gfx_window_t;

// Window flags
typedef enum {
    FMRB_WINDOW_FLAG_NONE = 0,
    FMRB_WINDOW_FLAG_VISIBLE = (1 << 0),
    FMRB_WINDOW_FLAG_RESIZABLE = (1 << 1),
    FMRB_WINDOW_FLAG_MOVABLE = (1 << 2),
    FMRB_WINDOW_FLAG_CLOSABLE = (1 << 3),
    FMRB_WINDOW_FLAG_MINIMIZABLE = (1 << 4),
    FMRB_WINDOW_FLAG_MAXIMIZABLE = (1 << 5),
    FMRB_WINDOW_FLAG_MODAL = (1 << 6)
} fmrb_window_flags_t;

// Window configuration
typedef struct {
    const char *title;
    fmrb_rect_t bounds;
    fmrb_window_flags_t flags;
    fmrb_color_t background_color;
    fmrb_color_t border_color;
    uint8_t border_width;
} fmrb_gfx_window_config_t;

// Window event types
typedef enum {
    FMRB_WINDOW_EVENT_CLOSE,
    FMRB_WINDOW_EVENT_MINIMIZE,
    FMRB_WINDOW_EVENT_MAXIMIZE,
    FMRB_WINDOW_EVENT_RESTORE,
    FMRB_WINDOW_EVENT_MOVE,
    FMRB_WINDOW_EVENT_RESIZE,
    FMRB_WINDOW_EVENT_FOCUS,
    FMRB_WINDOW_EVENT_BLUR,
    FMRB_WINDOW_EVENT_PAINT
} fmrb_window_event_type_t;

// Window event structure
typedef struct {
    fmrb_window_event_type_t type;
    fmrb_gfx_window_t window;
    union {
        fmrb_rect_t bounds; // For move/resize events
        fmrb_rect_t damage; // For paint events
    } data;
} fmrb_window_event_t;

// Window event callback
typedef void (*fmrb_window_event_callback_t)(const fmrb_window_event_t* event, void* user_data);

/**
 * @brief Initialize window system
 * @param context Graphics context
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_system_init(fmrb_gfx_context_t context);

/**
 * @brief Deinitialize window system
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_system_deinit(void);

/**
 * @brief Create a new window
 * @param config Window configuration
 * @param window Pointer to store window handle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_create(const fmrb_gfx_window_config_t* config, fmrb_gfx_window_t* window);

/**
 * @brief Destroy a window
 * @param window Window handle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_destroy(fmrb_gfx_window_t window);

/**
 * @brief Show window
 * @param window Window handle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_show(fmrb_gfx_window_t window);

/**
 * @brief Hide window
 * @param window Window handle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_hide(fmrb_gfx_window_t window);

/**
 * @brief Set window position and size
 * @param window Window handle
 * @param bounds New window bounds
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_set_bounds(fmrb_gfx_window_t window, const fmrb_rect_t* bounds);

/**
 * @brief Get window bounds
 * @param window Window handle
 * @param bounds Pointer to store window bounds
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_get_bounds(fmrb_gfx_window_t window, fmrb_rect_t* bounds);

/**
 * @brief Set window title
 * @param window Window handle
 * @param title New window title
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_set_title(fmrb_gfx_window_t window, const char* title);

/**
 * @brief Bring window to front
 * @param window Window handle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_bring_to_front(fmrb_gfx_window_t window);

/**
 * @brief Invalidate window region (mark for redraw)
 * @param window Window handle
 * @param rect Rectangle to invalidate (NULL for entire window)
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_invalidate(fmrb_gfx_window_t window, const fmrb_rect_t* rect);

/**
 * @brief Get window client area (content area excluding borders)
 * @param window Window handle
 * @param client_rect Pointer to store client rectangle
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_get_client_rect(fmrb_gfx_window_t window, fmrb_rect_t* client_rect);

/**
 * @brief Set window event callback
 * @param window Window handle
 * @param callback Event callback function
 * @param user_data User data passed to callback
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_set_event_callback(fmrb_gfx_window_t window, fmrb_window_event_callback_t callback, void* user_data);

/**
 * @brief Process window system events
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_process_events(void);

/**
 * @brief Draw all windows
 * @return Graphics error code
 */
fmrb_gfx_err_t fmrb_gfx_window_draw_all(void);

#ifdef __cplusplus
}
#endif
