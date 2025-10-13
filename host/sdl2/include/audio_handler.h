#ifndef AUDIO_HANDLER_H
#define AUDIO_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include "../../common/audio_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize audio handler
 * @return 0 on success, -1 on error
 */
int audio_handler_init(void);

/**
 * @brief Cleanup audio handler
 */
void audio_handler_cleanup(void);

/**
 * @brief Process audio command
 * @param data Command data
 * @param size Data size
 * @return 0 on success, -1 on error
 */
int audio_handler_process_command(const uint8_t *data, size_t size);

/**
 * @brief Get current audio status
 * @return Current audio status
 */
fmrb_audio_status_t audio_handler_get_status(void);

/**
 * @brief Set audio volume
 * @param volume Volume level (0-255)
 */
void audio_handler_set_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_HANDLER_H