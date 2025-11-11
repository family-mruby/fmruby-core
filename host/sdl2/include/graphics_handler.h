#ifndef GRAPHICS_HANDLER_H
#define GRAPHICS_HANDLER_H

#include <SDL2/SDL.h>
#include "fmrb_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Graphics handler log levels
typedef enum {
    GFX_LOG_NONE = 0,     // No logging
    GFX_LOG_ERROR = 1,    // Error messages only
    GFX_LOG_INFO = 2,     // Info + Error
    GFX_LOG_DEBUG = 3,    // Debug + Info + Error (verbose)
} gfx_log_level_t;

/**
 * @brief Initialize graphics handler
 * @param renderer SDL renderer
 * @return 0 on success, -1 on error
 */
int graphics_handler_init(SDL_Renderer *renderer);

/**
 * @brief Cleanup graphics handler
 */
void graphics_handler_cleanup(void);

/**
 * @brief Process graphics command
 * @param msg_type Message type (for ACK response)
 * @param cmd_type Graphics command type (from msgpack sub_cmd)
 * @param seq Sequence number
 * @param data Command data (structure only, no cmd_type prefix)
 * @param size Data size
 * @return 0 on success, -1 on error
 */
int graphics_handler_process_command(uint8_t msg_type, uint8_t cmd_type, uint8_t seq, const uint8_t *data, size_t size);

/**
 * @brief Get SDL renderer reference
 * @return SDL renderer pointer
 */
SDL_Renderer* graphics_handler_get_renderer(void);

/**
 * @brief Set graphics handler log level
 * @param level Log level (0=NONE, 1=ERROR, 2=INFO, 3=DEBUG)
 */
void graphics_handler_set_log_level(int level);

/**
 * @brief Render all canvases to screen in Z-order
 * This function composites all visible canvases to the screen based on their Z-order.
 * Should be called periodically (e.g., every frame in main loop).
 */
void graphics_handler_render_frame(void);

#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_HANDLER_H