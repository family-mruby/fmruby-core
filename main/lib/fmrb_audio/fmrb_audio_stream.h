#ifndef FMRB_AUDIO_STREAM_H
#define FMRB_AUDIO_STREAM_H

#include "fmrb_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio stream types
typedef enum {
    FMRB_AUDIO_STREAM_TYPE_PLAYBACK,
    FMRB_AUDIO_STREAM_TYPE_CAPTURE,
    FMRB_AUDIO_STREAM_TYPE_DUPLEX
} fmrb_audio_stream_type_t;

// Audio stream processing mode
typedef enum {
    FMRB_AUDIO_STREAM_MODE_BLOCKING,    // Synchronous, blocking I/O
    FMRB_AUDIO_STREAM_MODE_CALLBACK,    // Asynchronous, callback-based
    FMRB_AUDIO_STREAM_MODE_POLLING      // Asynchronous, polling-based
} fmrb_audio_stream_mode_t;

// Audio stream state
typedef enum {
    FMRB_AUDIO_STREAM_STATE_CLOSED,
    FMRB_AUDIO_STREAM_STATE_OPEN,
    FMRB_AUDIO_STREAM_STATE_PREPARED,
    FMRB_AUDIO_STREAM_STATE_RUNNING,
    FMRB_AUDIO_STREAM_STATE_PAUSED,
    FMRB_AUDIO_STREAM_STATE_DRAINING,
    FMRB_AUDIO_STREAM_STATE_XRUN
} fmrb_audio_stream_state_t;

// Stream control configuration
typedef struct {
    fmrb_audio_stream_type_t type;
    fmrb_audio_stream_mode_t mode;
    fmrb_audio_config_t audio_config;

    // Buffer management
    uint32_t period_size;     // Frames per period
    uint32_t buffer_periods;  // Number of periods in buffer

    // Timing
    uint32_t start_threshold; // Frames to start playback
    uint32_t stop_threshold;  // Frames to stop playback

    // Advanced features
    bool enable_resampling;   // Enable automatic sample rate conversion
    bool enable_channels_remap; // Enable channel remapping
    uint8_t channel_map[8];   // Channel mapping
} fmrb_audio_stream_control_config_t;

// Stream statistics
typedef struct {
    uint64_t frames_processed;
    uint64_t underruns;
    uint64_t overruns;
    uint32_t current_latency_frames;
    float cpu_usage_percent;
} fmrb_audio_stream_stats_t;

// Stream control handle
typedef void* fmrb_audio_stream_control_t;

/**
 * @brief Create stream control
 * @param config Stream control configuration
 * @param control Pointer to store control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_create(const fmrb_audio_stream_control_config_t* config,
                                                   fmrb_audio_stream_control_t* control);

/**
 * @brief Destroy stream control
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_destroy(fmrb_audio_stream_control_t control);

/**
 * @brief Open audio stream
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_open(fmrb_audio_stream_control_t control);

/**
 * @brief Close audio stream
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_close(fmrb_audio_stream_control_t control);

/**
 * @brief Prepare stream for I/O
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_prepare(fmrb_audio_stream_control_t control);

/**
 * @brief Start stream I/O
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_start(fmrb_audio_stream_control_t control);

/**
 * @brief Stop stream I/O
 * @param control Stream control handle
 * @param drain Whether to drain remaining data
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_stop(fmrb_audio_stream_control_t control, bool drain);

/**
 * @brief Pause stream
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_pause(fmrb_audio_stream_control_t control);

/**
 * @brief Resume stream
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_resume(fmrb_audio_stream_control_t control);

/**
 * @brief Get current stream state
 * @param control Stream control handle
 * @param state Pointer to store stream state
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_get_state(fmrb_audio_stream_control_t control,
                                                      fmrb_audio_stream_state_t* state);

/**
 * @brief Get available frames for writing (playback) or reading (capture)
 * @param control Stream control handle
 * @param frames Pointer to store available frames
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_get_available_frames(fmrb_audio_stream_control_t control,
                                                                 size_t* frames);

/**
 * @brief Write frames to playback stream
 * @param control Stream control handle
 * @param data Audio data
 * @param frames Number of frames to write
 * @param frames_written Pointer to store actual frames written
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_write(fmrb_audio_stream_control_t control,
                                                  const void* data,
                                                  size_t frames,
                                                  size_t* frames_written);

/**
 * @brief Read frames from capture stream
 * @param control Stream control handle
 * @param data Buffer for audio data
 * @param frames Number of frames to read
 * @param frames_read Pointer to store actual frames read
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_read(fmrb_audio_stream_control_t control,
                                                 void* data,
                                                 size_t frames,
                                                 size_t* frames_read);

/**
 * @brief Get stream statistics
 * @param control Stream control handle
 * @param stats Pointer to store statistics
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_get_stats(fmrb_audio_stream_control_t control,
                                                      fmrb_audio_stream_stats_t* stats);

/**
 * @brief Reset stream statistics
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_reset_stats(fmrb_audio_stream_control_t control);

/**
 * @brief Recover from XRUN (underrun/overrun)
 * @param control Stream control handle
 * @return Audio error code
 */
fmrb_audio_err_t fmrb_audio_stream_control_recover_xrun(fmrb_audio_stream_control_t control);

#ifdef __cplusplus
}
#endif

#endif // FMRB_AUDIO_STREAM_H