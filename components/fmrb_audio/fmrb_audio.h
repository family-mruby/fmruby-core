#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio error codes
typedef enum {
    FMRB_AUDIO_OK = 0,
    FMRB_AUDIO_ERR_INVALID_PARAM = -1,
    FMRB_AUDIO_ERR_NO_MEMORY = -2,
    FMRB_AUDIO_ERR_NOT_INITIALIZED = -3,
    FMRB_AUDIO_ERR_FAILED = -4,
    FMRB_AUDIO_ERR_TIMEOUT = -5
} fmrb_audio_err_t;

// APU playback status
typedef enum {
    FMRB_APU_STATUS_STOPPED = 0,
    FMRB_APU_STATUS_PLAYING = 1,
    FMRB_APU_STATUS_PAUSED = 2,
    FMRB_APU_STATUS_ERROR = 3
} fmrb_apu_status_t;

// Music binary info
typedef struct {
    const uint8_t* data;        // Binary data pointer
    size_t size;                // Data size in bytes
    uint32_t id;                // Music track ID
} fmrb_audio_music_t;

/**
 * @brief Initialize audio subsystem (APU emulator interface)
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_init(void);

/**
 * @brief Deinitialize audio subsystem
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_deinit(void);

/**
 * @brief Load music binary to APU emulator
 * @param music Music binary information
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_load_music(const fmrb_audio_music_t* music);

/**
 * @brief Start music playback
 * @param music_id Music track ID to play
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_play(uint32_t music_id);

/**
 * @brief Stop music playback
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stop(void);

/**
 * @brief Pause music playback
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_pause(void);

/**
 * @brief Resume music playback
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_resume(void);

/**
 * @brief Set volume level
 * @param volume Volume level (0-255)
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_set_volume(uint8_t volume);

/**
 * @brief Get current playback status
 * @param status Pointer to store status
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_get_status(fmrb_apu_status_t* status);

#ifdef __cplusplus
}
#endif