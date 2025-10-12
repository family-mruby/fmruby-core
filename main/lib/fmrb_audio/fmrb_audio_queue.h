#ifndef FMRB_AUDIO_QUEUE_H
#define FMRB_AUDIO_QUEUE_H

#include "fmrb_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio queue error codes
typedef enum {
    FMRB_AUDIO_QUEUE_OK = 0,
    FMRB_AUDIO_QUEUE_ERR_INVALID_PARAM = -1,
    FMRB_AUDIO_QUEUE_ERR_NO_MEMORY = -2,
    FMRB_AUDIO_QUEUE_ERR_BUFFER_FULL = -3,
    FMRB_AUDIO_QUEUE_ERR_BUFFER_EMPTY = -4,
    FMRB_AUDIO_QUEUE_ERR_FAILED = -5
} fmrb_audio_queue_err_t;

// Audio queue handle
typedef void* fmrb_audio_queue_t;

// Audio queue configuration
typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    fmrb_audio_format_t format;
    uint16_t buffer_size;    // Size of each buffer in frames
    uint8_t num_buffers;     // Number of buffers in the queue
} fmrb_audio_queue_config_t;

/**
 * @brief Create audio queue
 * @param config Queue configuration
 * @param queue Pointer to store queue handle
 * @return Audio queue error code
 */
fmrb_audio_queue_err_t fmrb_audio_queue_create(const fmrb_audio_queue_config_t* config,
                                                fmrb_audio_queue_t* queue);

/**
 * @brief Destroy audio queue
 * @param queue Audio queue handle
 * @return Audio queue error code
 */
fmrb_audio_queue_err_t fmrb_audio_queue_destroy(fmrb_audio_queue_t queue);

/**
 * @brief Enqueue audio samples
 * @param queue Audio queue handle
 * @param data Audio sample data
 * @param frames Number of frames to enqueue
 * @return Audio queue error code
 */
fmrb_audio_queue_err_t fmrb_audio_queue_enqueue(fmrb_audio_queue_t queue,
                                                 const void* data,
                                                 size_t frames);

/**
 * @brief Dequeue audio samples
 * @param queue Audio queue handle
 * @param data Buffer to store audio sample data
 * @param frames Number of frames to dequeue
 * @param frames_read Pointer to store actual frames read
 * @return Audio queue error code
 */
fmrb_audio_queue_err_t fmrb_audio_queue_dequeue(fmrb_audio_queue_t queue,
                                                 void* data,
                                                 size_t frames,
                                                 size_t* frames_read);

/**
 * @brief Get number of available frames in queue
 * @param queue Audio queue handle
 * @return Number of available frames
 */
size_t fmrb_audio_queue_get_available_frames(fmrb_audio_queue_t queue);

/**
 * @brief Get free space in queue (in frames)
 * @param queue Audio queue handle
 * @return Number of frames that can be enqueued
 */
size_t fmrb_audio_queue_get_free_space(fmrb_audio_queue_t queue);

/**
 * @brief Clear all data from queue
 * @param queue Audio queue handle
 * @return Audio queue error code
 */
fmrb_audio_queue_err_t fmrb_audio_queue_clear(fmrb_audio_queue_t queue);

/**
 * @brief Check if queue is empty
 * @param queue Audio queue handle
 * @return true if empty, false otherwise
 */
bool fmrb_audio_queue_is_empty(fmrb_audio_queue_t queue);

/**
 * @brief Check if queue is full
 * @param queue Audio queue handle
 * @return true if full, false otherwise
 */
bool fmrb_audio_queue_is_full(fmrb_audio_queue_t queue);

#ifdef __cplusplus
}
#endif

#endif // FMRB_AUDIO_QUEUE_H