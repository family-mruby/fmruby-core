#include "fmrb_audio.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_log_port.h"
#include <string.h>

static const char *TAG = "fmrb_audio";

// Audio context
typedef struct {
    bool initialized;
    fmrb_apu_status_t current_status;
    uint8_t current_volume;
} fmrb_audio_ctx_t;

static fmrb_audio_ctx_t audio_ctx = {
    .initialized = false,
    .current_status = FMRB_APU_STATUS_STOPPED,
    .current_volume = 128
};

static fmrb_audio_err_t send_audio_command(uint8_t sub_cmd, const void *data, size_t data_size) {
    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_AUDIO, sub_cmd,
        (const uint8_t *)data, (uint32_t)data_size,
        FMRB_TRANSPORT_TIMEOUT_DEFAULT);

    if (ret == FMRB_OK) {
        ESP_LOGI(TAG, "Audio command 0x%02x sent", sub_cmd);
        return FMRB_AUDIO_OK;
    } else {
        ESP_LOGE(TAG, "Failed to send audio command 0x%02x: %d", sub_cmd, ret);
        return FMRB_AUDIO_ERR_FAILED;
    }
}

fmrb_audio_err_t fmrb_audio_init(void) {
    if (audio_ctx.initialized) {
        return FMRB_AUDIO_OK;
    }

    audio_ctx.initialized = true;
    audio_ctx.current_status = FMRB_APU_STATUS_STOPPED;
    audio_ctx.current_volume = 128;

    ESP_LOGI(TAG, "Audio subsystem initialized");
    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_deinit(void) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_OK;
    }

    fmrb_audio_stop();
    audio_ctx.initialized = false;
    ESP_LOGI(TAG, "Audio subsystem deinitialized");
    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_load_music(const fmrb_audio_music_t *music) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }
    if (!music || !music->data || music->size == 0) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    ESP_LOGI(TAG, "Loading music binary: ID=%u, size=%zu bytes", music->id, music->size);
    return send_audio_command(FMRB_LINK_MSG_AUDIO_PLAY, music->data, music->size);
}

fmrb_audio_err_t fmrb_audio_play(uint32_t music_id) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Starting playback: music_id=%u", music_id);
    fmrb_audio_err_t ret = send_audio_command(FMRB_LINK_MSG_AUDIO_PLAY, &music_id, sizeof(music_id));
    if (ret == FMRB_AUDIO_OK) {
        audio_ctx.current_status = FMRB_APU_STATUS_PLAYING;
    }
    return ret;
}

fmrb_audio_err_t fmrb_audio_stop(void) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Stopping playback");
    fmrb_audio_err_t ret = send_audio_command(FMRB_LINK_MSG_AUDIO_STOP, NULL, 0);
    if (ret == FMRB_AUDIO_OK) {
        audio_ctx.current_status = FMRB_APU_STATUS_STOPPED;
    }
    return ret;
}

fmrb_audio_err_t fmrb_audio_pause(void) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Pausing playback");
    fmrb_audio_err_t ret = send_audio_command(FMRB_LINK_MSG_AUDIO_PAUSE, NULL, 0);
    if (ret == FMRB_AUDIO_OK) {
        audio_ctx.current_status = FMRB_APU_STATUS_PAUSED;
    }
    return ret;
}

fmrb_audio_err_t fmrb_audio_resume(void) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Resuming playback");
    fmrb_audio_err_t ret = send_audio_command(FMRB_LINK_MSG_AUDIO_RESUME, NULL, 0);
    if (ret == FMRB_AUDIO_OK) {
        audio_ctx.current_status = FMRB_APU_STATUS_PLAYING;
    }
    return ret;
}

fmrb_audio_err_t fmrb_audio_set_volume(uint8_t volume) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Setting volume: %u", volume);
    fmrb_audio_err_t ret = send_audio_command(FMRB_LINK_MSG_AUDIO_SET_VOLUME, &volume, sizeof(volume));
    if (ret == FMRB_AUDIO_OK) {
        audio_ctx.current_volume = volume;
    }
    return ret;
}

fmrb_audio_err_t fmrb_audio_get_status(fmrb_apu_status_t *status) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }
    if (!status) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    *status = audio_ctx.current_status;
    return FMRB_AUDIO_OK;
}
