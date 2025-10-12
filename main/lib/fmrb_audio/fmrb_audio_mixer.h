#ifndef FMRB_AUDIO_MIXER_H
#define FMRB_AUDIO_MIXER_H

#include "fmrb_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio mixer error codes
typedef enum {
    FMRB_AUDIO_MIXER_OK = 0,
    FMRB_AUDIO_MIXER_ERR_INVALID_PARAM = -1,
    FMRB_AUDIO_MIXER_ERR_NO_MEMORY = -2,
    FMRB_AUDIO_MIXER_ERR_MAX_STREAMS = -3,
    FMRB_AUDIO_MIXER_ERR_STREAM_NOT_FOUND = -4,
    FMRB_AUDIO_MIXER_ERR_FAILED = -5
} fmrb_audio_mixer_err_t;

// Audio mixer handle
typedef void* fmrb_audio_mixer_t;

// Audio mixer stream handle
typedef uint32_t fmrb_audio_mixer_stream_id_t;

// Audio mixer configuration
typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    fmrb_audio_format_t format;
    uint8_t max_streams;     // Maximum number of concurrent streams
} fmrb_audio_mixer_config_t;

// Audio mixer stream configuration
typedef struct {
    float volume;            // Stream volume (0.0 to 1.0)
    float pan;              // Pan position (-1.0 left to +1.0 right, 0.0 center)
    bool loop;              // Whether to loop the stream
    uint32_t fade_in_ms;    // Fade in duration in milliseconds
    uint32_t fade_out_ms;   // Fade out duration in milliseconds
} fmrb_audio_mixer_stream_config_t;

/**
 * @brief Create audio mixer
 * @param config Mixer configuration
 * @param mixer Pointer to store mixer handle
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_create(const fmrb_audio_mixer_config_t* config,
                                                fmrb_audio_mixer_t* mixer);

/**
 * @brief Destroy audio mixer
 * @param mixer Audio mixer handle
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_destroy(fmrb_audio_mixer_t mixer);

/**
 * @brief Add stream to mixer
 * @param mixer Audio mixer handle
 * @param stream_config Stream configuration
 * @param stream_id Pointer to store stream ID
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_add_stream(fmrb_audio_mixer_t mixer,
                                                    const fmrb_audio_mixer_stream_config_t* stream_config,
                                                    fmrb_audio_mixer_stream_id_t* stream_id);

/**
 * @brief Remove stream from mixer
 * @param mixer Audio mixer handle
 * @param stream_id Stream ID
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_remove_stream(fmrb_audio_mixer_t mixer,
                                                       fmrb_audio_mixer_stream_id_t stream_id);

/**
 * @brief Set stream volume
 * @param mixer Audio mixer handle
 * @param stream_id Stream ID
 * @param volume Volume level (0.0 to 1.0)
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_set_stream_volume(fmrb_audio_mixer_t mixer,
                                                           fmrb_audio_mixer_stream_id_t stream_id,
                                                           float volume);

/**
 * @brief Set stream pan
 * @param mixer Audio mixer handle
 * @param stream_id Stream ID
 * @param pan Pan position (-1.0 to +1.0)
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_set_stream_pan(fmrb_audio_mixer_t mixer,
                                                        fmrb_audio_mixer_stream_id_t stream_id,
                                                        float pan);

/**
 * @brief Queue samples for specific stream
 * @param mixer Audio mixer handle
 * @param stream_id Stream ID
 * @param data Audio sample data
 * @param frames Number of frames
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_queue_stream_samples(fmrb_audio_mixer_t mixer,
                                                              fmrb_audio_mixer_stream_id_t stream_id,
                                                              const void* data,
                                                              size_t frames);

/**
 * @brief Mix all streams and produce output
 * @param mixer Audio mixer handle
 * @param output Output buffer
 * @param frames Number of frames to mix
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_mix(fmrb_audio_mixer_t mixer,
                                             void* output,
                                             size_t frames);

/**
 * @brief Set master volume
 * @param mixer Audio mixer handle
 * @param volume Master volume (0.0 to 1.0)
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_set_master_volume(fmrb_audio_mixer_t mixer,
                                                           float volume);

/**
 * @brief Get master volume
 * @param mixer Audio mixer handle
 * @param volume Pointer to store master volume
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_get_master_volume(fmrb_audio_mixer_t mixer,
                                                           float* volume);

/**
 * @brief Pause all streams
 * @param mixer Audio mixer handle
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_pause_all(fmrb_audio_mixer_t mixer);

/**
 * @brief Resume all streams
 * @param mixer Audio mixer handle
 * @return Audio mixer error code
 */
fmrb_audio_mixer_err_t fmrb_audio_mixer_resume_all(fmrb_audio_mixer_t mixer);

/**
 * @brief Get number of active streams
 * @param mixer Audio mixer handle
 * @return Number of active streams
 */
size_t fmrb_audio_mixer_get_stream_count(fmrb_audio_mixer_t mixer);

#ifdef __cplusplus
}
#endif

#endif // FMRB_AUDIO_MIXER_H