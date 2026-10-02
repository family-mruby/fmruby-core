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

/**
 * @brief The machine's output settings: mute and volume (doc/reference/audio_output.md).
 *
 * Only the record. The audio keeps running and the backend applies these at
 * its last output stage (audio_p4 on Modern, graphics-audio on Retro and in
 * the simulator), told by FMRB_AUDIO_CMD_SET_OUTPUT. Callers change them
 * through fmrb_host_set_audio_mute / fmrb_host_set_audio_volume.
 *
 * The volume is a step, 0-10: 0 is silence, and 1-10 are spaced evenly in dB
 * from the level range's minimum (step 0's notional level) up to its maximum
 * (audio_level_min / max in system_conf, default -50 / 0 dB).
 */
#define FMRB_AUDIO_VOLUME_MAX 10
#define FMRB_AUDIO_VOLUME_DEFAULT 7

void fmrb_audio_set_muted(bool muted);
bool fmrb_audio_is_muted(void);

void fmrb_audio_set_volume_step(uint8_t step);   // clamped to 0-10
uint8_t fmrb_audio_volume_step(void);

/** @brief Level range for steps 1-10, in tenths of a dB. */
void fmrb_audio_set_level_range(int16_t min_db_x10, int16_t max_db_x10);
void fmrb_audio_level_range(int16_t *min_db_x10, int16_t *max_db_x10);

/**
 * @brief The level of a volume step, in tenths of a dB.
 * min + (max - min) * step / 10: step 10 is max, and each step is a tenth of
 * the range (5 dB with the default 0 / -50 dB). Step 0 (silence) answers min;
 * callers treat it as silence, not as a level.
 */
int16_t fmrb_audio_step_db_x10(uint8_t step, int16_t min_db_x10, int16_t max_db_x10);

#ifdef __cplusplus
}
#endif