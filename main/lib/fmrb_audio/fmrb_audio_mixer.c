#include "fmrb_audio_mixer.h"
#include "fmrb_hal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    fmrb_audio_mixer_config_t config;
    bool initialized;
} fmrb_audio_mixer_impl_t;

fmrb_audio_mixer_err_t fmrb_audio_mixer_create(const fmrb_audio_mixer_config_t* config,
                                                fmrb_audio_mixer_t* mixer) {
    if (!config || !mixer) {
        return FMRB_AUDIO_MIXER_ERR_INVALID_PARAM;
    }

    fmrb_audio_mixer_impl_t* impl = malloc(sizeof(fmrb_audio_mixer_impl_t));
    if (!impl) {
        return FMRB_AUDIO_MIXER_ERR_NO_MEMORY;
    }

    memset(impl, 0, sizeof(fmrb_audio_mixer_impl_t));
    impl->config = *config;
    impl->initialized = true;

    *mixer = impl;
    return FMRB_AUDIO_MIXER_OK;
}

fmrb_audio_mixer_err_t fmrb_audio_mixer_destroy(fmrb_audio_mixer_t mixer) {
    if (!mixer) {
        return FMRB_AUDIO_MIXER_ERR_INVALID_PARAM;
    }

    fmrb_audio_mixer_impl_t* impl = (fmrb_audio_mixer_impl_t*)mixer;
    impl->initialized = false;
    free(impl);

    return FMRB_AUDIO_MIXER_OK;
}

fmrb_audio_mixer_err_t fmrb_audio_mixer_add_stream(fmrb_audio_mixer_t mixer,
                                                    const fmrb_audio_mixer_stream_config_t* stream_config,
                                                    fmrb_audio_mixer_stream_id_t* stream_id) {
    if (!mixer || !stream_config || !stream_id) {
        return FMRB_AUDIO_MIXER_ERR_INVALID_PARAM;
    }

    // Simple implementation - just return a dummy stream ID
    *stream_id = 1;
    return FMRB_AUDIO_MIXER_OK;
}

fmrb_audio_mixer_err_t fmrb_audio_mixer_remove_stream(fmrb_audio_mixer_t mixer,
                                                       fmrb_audio_mixer_stream_id_t stream_id) {
    if (!mixer) {
        return FMRB_AUDIO_MIXER_ERR_INVALID_PARAM;
    }

    // Simple stub implementation
    return FMRB_AUDIO_MIXER_OK;
}

fmrb_audio_mixer_err_t fmrb_audio_mixer_mix_samples(fmrb_audio_mixer_t mixer,
                                                     const int16_t** input_buffers,
                                                     uint32_t num_buffers,
                                                     int16_t* output_buffer,
                                                     uint32_t num_samples) {
    if (!mixer || !output_buffer) {
        return FMRB_AUDIO_MIXER_ERR_INVALID_PARAM;
    }

    // Simple stub - just silence
    memset(output_buffer, 0, num_samples * sizeof(int16_t));
    return FMRB_AUDIO_MIXER_OK;
}