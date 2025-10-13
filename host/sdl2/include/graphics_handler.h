#ifndef GRAPHICS_HANDLER_H
#define GRAPHICS_HANDLER_H

#include <SDL2/SDL.h>
#include "../../common/graphics_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * @param data Command data
 * @param size Data size
 * @return 0 on success, -1 on error
 */
int graphics_handler_process_command(const uint8_t *data, size_t size);

/**
 * @brief Get SDL renderer reference
 * @return SDL renderer pointer
 */
SDL_Renderer* graphics_handler_get_renderer(void);

#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_HANDLER_H