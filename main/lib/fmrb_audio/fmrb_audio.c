#include "fmrb_audio.h"
#include "fmrb_hal.h"
#include "fmrb_ipc_protocol.h"
#include "fmrb_ipc_transport.h"
#include "fmrb_audio_queue.h"
#include "fmrb_audio_mixer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "fmrb_audio";

// Supported sample rates
static const uint32_t supported_sample_rates[] = {
    8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000
};

typedef enum {
    FMRB_AUDIO_STREAM_STATE_STOPPED,
    FMRB_AUDIO_STREAM_STATE_PLAYING,
    FMRB_AUDIO_STREAM_STATE_PAUSED
} fmrb_audio_stream_state_t;

typedef struct {
    fmrb_audio_config_t config;
    fmrb_ipc_transport_handle_t transport;
    fmrb_audio_mixer_t* mixer;
    bool initialized;
} fmrb_audio_context_impl_t;

typedef struct {
    fmrb_audio_context_impl_t* context;
    fmrb_audio_stream_config_t config;
    fmrb_audio_queue_t* queue;
    fmrb_audio_stream_state_t state;
    float volume;
    uint32_t stream_id;
} fmrb_audio_stream_impl_t;

static uint32_t next_stream_id = 1;

// Helper function to send audio command
static fmrb_audio_err_t send_audio_command(fmrb_audio_context_impl_t* ctx, uint8_t cmd_type, const void* cmd_data, size_t cmd_size) {
    if (!ctx || !ctx->transport) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    fmrb_ipc_transport_err_t ret = fmrb_ipc_transport_send(ctx->transport, cmd_type, (const uint8_t*)cmd_data, cmd_size);

    switch (ret) {
        case FMRB_IPC_TRANSPORT_OK:
            return FMRB_AUDIO_OK;
        case FMRB_IPC_TRANSPORT_ERR_INVALID_PARAM:
            return FMRB_AUDIO_ERR_INVALID_PARAM;
        case FMRB_IPC_TRANSPORT_ERR_NO_MEMORY:
            return FMRB_AUDIO_ERR_NO_MEMORY;
        default:
            return FMRB_AUDIO_ERR_FAILED;
    }
}

fmrb_audio_err_t fmrb_audio_init(const fmrb_audio_config_t* config, fmrb_audio_context_t* context) {
    if (!config || !context) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_context_impl_t* ctx = malloc(sizeof(fmrb_audio_context_impl_t));
    if (!ctx) {
        return FMRB_AUDIO_ERR_NO_MEMORY;
    }

    memset(ctx, 0, sizeof(fmrb_audio_context_impl_t));
    ctx->config = *config;

    // Initialize IPC transport for audio
    fmrb_ipc_transport_config_t transport_config = {
        .timeout_ms = 1000,
        .enable_retransmit = true,
        .max_retries = 3,
        .window_size = 16
    };

    fmrb_ipc_transport_err_t ret = fmrb_ipc_transport_init(&transport_config, &ctx->transport);
    if (ret != FMRB_IPC_TRANSPORT_OK) {
        free(ctx);
        return FMRB_AUDIO_ERR_FAILED;
    }

    // Initialize mixer
    fmrb_audio_mixer_config_t mixer_config = {
        .sample_rate = config->sample_rate,
        .channels = config->channels,
        .format = config->format,
        .max_streams = 8
    };

    fmrb_audio_mixer_err_t mixer_ret = fmrb_audio_mixer_create(&mixer_config, &ctx->mixer);
    if (mixer_ret != FMRB_AUDIO_MIXER_OK) {
        fmrb_ipc_transport_deinit(ctx->transport);
        free(ctx);
        return FMRB_AUDIO_ERR_FAILED;
    }

    ctx->initialized = true;
    *context = ctx;

    ESP_LOGI(TAG, "Audio initialized: %d Hz, %d channels, format %d",
             config->sample_rate, config->channels, config->format);

    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_deinit(fmrb_audio_context_t context) {
    if (!context) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_context_impl_t* ctx = (fmrb_audio_context_impl_t*)context;

    if (ctx->mixer) {
        fmrb_audio_mixer_destroy(ctx->mixer);
    }

    if (ctx->transport) {
        fmrb_ipc_transport_deinit(ctx->transport);
    }

    ctx->initialized = false;
    free(ctx);

    ESP_LOGI(TAG, "Audio deinitialized");
    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_stream_create(fmrb_audio_context_t context,
                                           const fmrb_audio_stream_config_t* stream_config,
                                           fmrb_audio_stream_t* stream) {
    if (!context || !stream_config || !stream) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_context_impl_t* ctx = (fmrb_audio_context_impl_t*)context;
    if (!ctx->initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    fmrb_audio_stream_impl_t* stream_impl = malloc(sizeof(fmrb_audio_stream_impl_t));
    if (!stream_impl) {
        return FMRB_AUDIO_ERR_NO_MEMORY;
    }

    memset(stream_impl, 0, sizeof(fmrb_audio_stream_impl_t));
    stream_impl->context = ctx;
    stream_impl->config = *stream_config;
    stream_impl->state = FMRB_AUDIO_STREAM_STATE_STOPPED;
    stream_impl->volume = 1.0f;
    stream_impl->stream_id = next_stream_id++;

    // Create audio queue
    fmrb_audio_queue_config_t queue_config = {
        .sample_rate = stream_config->config.sample_rate,
        .channels = stream_config->config.channels,
        .format = stream_config->config.format,
        .buffer_size = stream_config->config.buffer_size,
        .num_buffers = stream_config->config.num_buffers
    };

    fmrb_audio_queue_err_t queue_ret = fmrb_audio_queue_create(&queue_config, &stream_impl->queue);
    if (queue_ret != FMRB_AUDIO_QUEUE_OK) {
        free(stream_impl);
        return FMRB_AUDIO_ERR_FAILED;
    }

    *stream = stream_impl;

    ESP_LOGI(TAG, "Audio stream created: ID=%d, %d Hz, %d ch",
             stream_impl->stream_id,
             stream_config->config.sample_rate,
             stream_config->config.channels);

    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_stream_destroy(fmrb_audio_stream_t stream) {
    if (!stream) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    // Stop stream if playing
    if (stream_impl->state != FMRB_AUDIO_STREAM_STATE_STOPPED) {
        fmrb_audio_stream_stop(stream);
    }

    if (stream_impl->queue) {
        fmrb_audio_queue_destroy(stream_impl->queue);
    }

    ESP_LOGI(TAG, "Audio stream destroyed: ID=%d", stream_impl->stream_id);
    free(stream_impl);

    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_stream_start(fmrb_audio_stream_t stream) {
    if (!stream) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    if (stream_impl->state == FMRB_AUDIO_STREAM_STATE_PLAYING) {
        return FMRB_AUDIO_OK; // Already playing
    }

    // Send play command via IPC
    fmrb_ipc_audio_play_t play_cmd = {
        .sample_rate = stream_impl->config.config.sample_rate,
        .channels = stream_impl->config.config.channels,
        .bits_per_sample = fmrb_audio_get_sample_size(stream_impl->config.config.format) * 8,
        .data_len = 0 // Will be set per data packet
    };

    fmrb_audio_err_t ret = send_audio_command(stream_impl->context, FMRB_IPC_MSG_AUDIO_PLAY, &play_cmd, sizeof(play_cmd));
    if (ret == FMRB_AUDIO_OK) {
        stream_impl->state = FMRB_AUDIO_STREAM_STATE_PLAYING;
        ESP_LOGI(TAG, "Audio stream started: ID=%d", stream_impl->stream_id);
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_stream_stop(fmrb_audio_stream_t stream) {
    if (!stream) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    if (stream_impl->state == FMRB_AUDIO_STREAM_STATE_STOPPED) {
        return FMRB_AUDIO_OK; // Already stopped
    }

    fmrb_audio_err_t ret = send_audio_command(stream_impl->context, FMRB_IPC_MSG_AUDIO_STOP, NULL, 0);
    if (ret == FMRB_AUDIO_OK) {
        stream_impl->state = FMRB_AUDIO_STREAM_STATE_STOPPED;
        ESP_LOGI(TAG, "Audio stream stopped: ID=%d", stream_impl->stream_id);
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_stream_pause(fmrb_audio_stream_t stream) {
    if (!stream) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    if (stream_impl->state != FMRB_AUDIO_STREAM_STATE_PLAYING) {
        return FMRB_AUDIO_ERR_FAILED;
    }

    fmrb_audio_err_t ret = send_audio_command(stream_impl->context, FMRB_IPC_MSG_AUDIO_PAUSE, NULL, 0);
    if (ret == FMRB_AUDIO_OK) {
        stream_impl->state = FMRB_AUDIO_STREAM_STATE_PAUSED;
        ESP_LOGI(TAG, "Audio stream paused: ID=%d", stream_impl->stream_id);
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_stream_resume(fmrb_audio_stream_t stream) {
    if (!stream) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    if (stream_impl->state != FMRB_AUDIO_STREAM_STATE_PAUSED) {
        return FMRB_AUDIO_ERR_FAILED;
    }

    fmrb_audio_err_t ret = send_audio_command(stream_impl->context, FMRB_IPC_MSG_AUDIO_RESUME, NULL, 0);
    if (ret == FMRB_AUDIO_OK) {
        stream_impl->state = FMRB_AUDIO_STREAM_STATE_PLAYING;
        ESP_LOGI(TAG, "Audio stream resumed: ID=%d", stream_impl->stream_id);
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_stream_set_volume(fmrb_audio_stream_t stream, float volume) {
    if (!stream || volume < 0.0f || volume > 1.0f) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    fmrb_ipc_audio_volume_t volume_cmd = {
        .volume = (uint8_t)(volume * 100)
    };

    fmrb_audio_err_t ret = send_audio_command(stream_impl->context, FMRB_IPC_MSG_AUDIO_SET_VOLUME, &volume_cmd, sizeof(volume_cmd));
    if (ret == FMRB_AUDIO_OK) {
        stream_impl->volume = volume;
        ESP_LOGI(TAG, "Audio stream volume set: ID=%d, volume=%.2f", stream_impl->stream_id, volume);
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_stream_get_volume(fmrb_audio_stream_t stream, float* volume) {
    if (!stream || !volume) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;
    *volume = stream_impl->volume;

    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_stream_queue_samples(fmrb_audio_stream_t stream,
                                                  const void* data,
                                                  size_t frames) {
    if (!stream || !data || frames == 0) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;

    fmrb_audio_queue_err_t ret = fmrb_audio_queue_enqueue(stream_impl->queue, data, frames);

    switch (ret) {
        case FMRB_AUDIO_QUEUE_OK:
            return FMRB_AUDIO_OK;
        case FMRB_AUDIO_QUEUE_ERR_BUFFER_FULL:
            return FMRB_AUDIO_ERR_BUFFER_FULL;
        default:
            return FMRB_AUDIO_ERR_FAILED;
    }
}

fmrb_audio_err_t fmrb_audio_stream_get_queued_frames(fmrb_audio_stream_t stream, size_t* frames) {
    if (!stream || !frames) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    fmrb_audio_stream_impl_t* stream_impl = (fmrb_audio_stream_impl_t*)stream;
    *frames = fmrb_audio_queue_get_available_frames(stream_impl->queue);

    return FMRB_AUDIO_OK;
}

size_t fmrb_audio_get_sample_size(fmrb_audio_format_t format) {
    switch (format) {
        case FMRB_AUDIO_FORMAT_S8:
        case FMRB_AUDIO_FORMAT_U8:
            return 1;
        case FMRB_AUDIO_FORMAT_S16_LE:
        case FMRB_AUDIO_FORMAT_S16_BE:
            return 2;
        case FMRB_AUDIO_FORMAT_S24_LE:
            return 3;
        case FMRB_AUDIO_FORMAT_S32_LE:
        case FMRB_AUDIO_FORMAT_F32_LE:
            return 4;
        default:
            return 0;
    }
}

const char* fmrb_audio_get_format_name(fmrb_audio_format_t format) {
    switch (format) {
        case FMRB_AUDIO_FORMAT_S8:      return "S8";
        case FMRB_AUDIO_FORMAT_U8:      return "U8";
        case FMRB_AUDIO_FORMAT_S16_LE:  return "S16_LE";
        case FMRB_AUDIO_FORMAT_S16_BE:  return "S16_BE";
        case FMRB_AUDIO_FORMAT_S24_LE:  return "S24_LE";
        case FMRB_AUDIO_FORMAT_S32_LE:  return "S32_LE";
        case FMRB_AUDIO_FORMAT_F32_LE:  return "F32_LE";
        default:                        return "UNKNOWN";
    }
}

uint32_t fmrb_audio_get_supported_sample_rate(uint32_t requested_rate) {
    size_t num_rates = sizeof(supported_sample_rates) / sizeof(supported_sample_rates[0]);

    // Find closest supported rate
    uint32_t closest_rate = supported_sample_rates[0];
    uint32_t min_diff = abs((int)requested_rate - (int)closest_rate);

    for (size_t i = 1; i < num_rates; i++) {
        uint32_t diff = abs((int)requested_rate - (int)supported_sample_rates[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest_rate = supported_sample_rates[i];
        }
    }

    return closest_rate;
}