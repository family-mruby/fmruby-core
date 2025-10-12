#include "fmrb_audio_queue.h"
#include "fmrb_hal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    fmrb_audio_queue_config_t config;
    uint8_t* buffer;
    uint32_t write_pos;
    uint32_t read_pos;
    uint32_t used_bytes;
    bool initialized;
} fmrb_audio_queue_impl_t;

fmrb_audio_queue_err_t fmrb_audio_queue_create(const fmrb_audio_queue_config_t* config,
                                                fmrb_audio_queue_t* queue) {
    if (!config || !queue) {
        return FMRB_AUDIO_QUEUE_ERR_INVALID_PARAM;
    }

    fmrb_audio_queue_impl_t* impl = malloc(sizeof(fmrb_audio_queue_impl_t));
    if (!impl) {
        return FMRB_AUDIO_QUEUE_ERR_NO_MEMORY;
    }

    memset(impl, 0, sizeof(fmrb_audio_queue_impl_t));
    impl->config = *config;

    uint32_t total_buffer_size = config->buffer_size * config->num_buffers;
    impl->buffer = malloc(total_buffer_size);
    if (!impl->buffer) {
        free(impl);
        return FMRB_AUDIO_QUEUE_ERR_NO_MEMORY;
    }

    impl->initialized = true;
    *queue = impl;
    return FMRB_AUDIO_QUEUE_OK;
}

fmrb_audio_queue_err_t fmrb_audio_queue_destroy(fmrb_audio_queue_t queue) {
    if (!queue) {
        return FMRB_AUDIO_QUEUE_ERR_INVALID_PARAM;
    }

    fmrb_audio_queue_impl_t* impl = (fmrb_audio_queue_impl_t*)queue;

    if (impl->buffer) {
        free(impl->buffer);
    }

    impl->initialized = false;
    free(impl);
    return FMRB_AUDIO_QUEUE_OK;
}

fmrb_audio_queue_err_t fmrb_audio_queue_enqueue(fmrb_audio_queue_t queue,
                                                 const void* data,
                                                 size_t frames) {
    if (!queue || !data) {
        return FMRB_AUDIO_QUEUE_ERR_INVALID_PARAM;
    }

    fmrb_audio_queue_impl_t* impl = (fmrb_audio_queue_impl_t*)queue;

    uint32_t total_size = impl->config.buffer_size * impl->config.num_buffers;
    const uint8_t* data_bytes = (const uint8_t*)data;
    size_t size = frames * sizeof(int16_t) * 2; // Assume stereo 16-bit

    if (impl->used_bytes + size > total_size) {
        return FMRB_AUDIO_QUEUE_ERR_BUFFER_FULL;
    }

    // Simple circular buffer implementation
    for (size_t i = 0; i < size; i++) {
        impl->buffer[impl->write_pos] = data_bytes[i];
        impl->write_pos = (impl->write_pos + 1) % total_size;
    }

    impl->used_bytes += size;
    return FMRB_AUDIO_QUEUE_OK;
}

fmrb_audio_queue_err_t fmrb_audio_queue_dequeue(fmrb_audio_queue_t queue,
                                                 void* data,
                                                 size_t frames,
                                                 size_t* frames_read) {
    if (!queue || !data || !frames_read) {
        return FMRB_AUDIO_QUEUE_ERR_INVALID_PARAM;
    }

    fmrb_audio_queue_impl_t* impl = (fmrb_audio_queue_impl_t*)queue;

    uint8_t* data_bytes = (uint8_t*)data;
    size_t size = frames * sizeof(int16_t) * 2; // Assume stereo 16-bit

    if (impl->used_bytes < size) {
        return FMRB_AUDIO_QUEUE_ERR_BUFFER_EMPTY;
    }

    uint32_t total_size = impl->config.buffer_size * impl->config.num_buffers;

    // Simple circular buffer implementation
    for (size_t i = 0; i < size; i++) {
        data_bytes[i] = impl->buffer[impl->read_pos];
        impl->read_pos = (impl->read_pos + 1) % total_size;
    }

    impl->used_bytes -= size;
    if (frames_read) {
        *frames_read = frames;
    }
    return FMRB_AUDIO_QUEUE_OK;
}