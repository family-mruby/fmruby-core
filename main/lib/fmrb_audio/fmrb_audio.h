#ifndef FMRB_AUDIO_H
#define FMRB_AUDIO_H

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
    FMRB_AUDIO_ERR_BUFFER_FULL = -5,
    FMRB_AUDIO_ERR_BUFFER_EMPTY = -6,
    FMRB_AUDIO_ERR_FORMAT_NOT_SUPPORTED = -7
} fmrb_audio_err_t;

// Audio sample formats
typedef enum {
    FMRB_AUDIO_FORMAT_S8,      // Signed 8-bit
    FMRB_AUDIO_FORMAT_U8,      // Unsigned 8-bit
    FMRB_AUDIO_FORMAT_S16_LE,  // Signed 16-bit little endian
    FMRB_AUDIO_FORMAT_S16_BE,  // Signed 16-bit big endian
    FMRB_AUDIO_FORMAT_S24_LE,  // Signed 24-bit little endian
    FMRB_AUDIO_FORMAT_S32_LE,  // Signed 32-bit little endian
    FMRB_AUDIO_FORMAT_F32_LE   // 32-bit float little endian
} fmrb_audio_format_t;

// Audio configuration
typedef struct {
    uint32_t sample_rate;     // Sample rate in Hz (e.g., 44100, 48000)
    uint8_t channels;         // Number of channels (1=mono, 2=stereo)
    fmrb_audio_format_t format; // Sample format
    uint16_t buffer_size;     // Buffer size in samples
    uint8_t num_buffers;      // Number of buffers for double/triple buffering
} fmrb_audio_config_t;

// Audio context handle
typedef void* fmrb_audio_context_t;

// Audio stream handle
typedef void* fmrb_audio_stream_t;

// Audio callback function type
typedef void (*fmrb_audio_callback_t)(void* buffer, size_t frames, void* user_data);

// Audio stream configuration
typedef struct {
    fmrb_audio_config_t config;
    fmrb_audio_callback_t callback;
    void* user_data;
    bool auto_start;
} fmrb_audio_stream_config_t;

/**
 * @brief Initialize audio subsystem
 * @param config Audio configuration
 * @param context Pointer to store audio context
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_init(const fmrb_audio_config_t* config, fmrb_audio_context_t* context);

/**
 * @brief Deinitialize audio subsystem
 * @param context Audio context
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_deinit(fmrb_audio_context_t context);

/**
 * @brief Create audio stream
 * @param context Audio context
 * @param stream_config Stream configuration
 * @param stream Pointer to store stream handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_create(fmrb_audio_context_t context,
                                           const fmrb_audio_stream_config_t* stream_config,
                                           fmrb_audio_stream_t* stream);

/**
 * @brief Destroy audio stream
 * @param stream Audio stream handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_destroy(fmrb_audio_stream_t stream);

/**
 * @brief Start audio stream playback
 * @param stream Audio stream handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_start(fmrb_audio_stream_t stream);

/**
 * @brief Stop audio stream playback
 * @param stream Audio stream handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_stop(fmrb_audio_stream_t stream);

/**
 * @brief Pause audio stream playback
 * @param stream Audio stream handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_pause(fmrb_audio_stream_t stream);

/**
 * @brief Resume audio stream playback
 * @param stream Audio stream handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_resume(fmrb_audio_stream_t stream);

/**
 * @brief Set stream volume
 * @param stream Audio stream handle
 * @param volume Volume level (0.0 to 1.0)
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_set_volume(fmrb_audio_stream_t stream, float volume);

/**
 * @brief Get stream volume
 * @param stream Audio stream handle
 * @param volume Pointer to store volume level
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_get_volume(fmrb_audio_stream_t stream, float* volume);

/**
 * @brief Queue audio samples for playback
 * @param stream Audio stream handle
 * @param data Audio sample data
 * @param frames Number of frames to queue
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_queue_samples(fmrb_audio_stream_t stream,
                                                  const void* data,
                                                  size_t frames);

/**
 * @brief Get number of queued frames
 * @param stream Audio stream handle
 * @param frames Pointer to store number of queued frames
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_get_queued_frames(fmrb_audio_stream_t stream, size_t* frames);

/**
 * @brief Get sample size in bytes for given format
 * @param format Audio format
 * @return Sample size in bytes
 */
size_t fmrb_audio_get_sample_size(fmrb_audio_format_t format);

/**
 * @brief Get format name string
 * @param format Audio format
 * @return Format name string
 */
const char* fmrb_audio_get_format_name(fmrb_audio_format_t format);

/**
 * @brief Convert sample rate to closest supported rate
 * @param requested_rate Requested sample rate
 * @return Closest supported sample rate
 */
uint32_t fmrb_audio_get_supported_sample_rate(uint32_t requested_rate);

#ifdef __cplusplus
}
#endif

#endif // FMRB_AUDIO_H