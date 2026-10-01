#include "fmrb_audio.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_log_port.h"
#include "fmrb_attr.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "fmrb_audio";

// Audio context. `muted` and `volume_step` sit in the padding after
// `initialized`, so the output settings cost no internal RAM (the level range
// below is in PSRAM). fmrb_audio_init leaves them alone: the kernel sets them
// from system_conf before the host task initializes this module.
typedef struct {
    bool initialized;
    volatile bool muted;
    volatile uint8_t volume_step;
    fmrb_apu_status_t current_status;
    uint8_t current_volume;
} fmrb_audio_ctx_t;

static fmrb_audio_ctx_t audio_ctx = {
    .initialized = false,
    .muted = false,
    .volume_step = FMRB_AUDIO_VOLUME_DEFAULT,
    .current_status = FMRB_APU_STATUS_STOPPED,
    .current_volume = 128
};

FMRB_EXT_RAM_BSS_ATTR static int16_t s_level_min_x10;
FMRB_EXT_RAM_BSS_ATTR static int16_t s_level_max_x10;

void fmrb_audio_set_muted(bool muted) {
    audio_ctx.muted = muted;
}

bool fmrb_audio_is_muted(void) {
    return audio_ctx.muted;
}

void fmrb_audio_set_volume_step(uint8_t step) {
    audio_ctx.volume_step = step > FMRB_AUDIO_VOLUME_MAX ? FMRB_AUDIO_VOLUME_MAX : step;
}

uint8_t fmrb_audio_volume_step(void) {
    return audio_ctx.volume_step;
}

void fmrb_audio_set_level_range(int16_t min_db_x10, int16_t max_db_x10) {
    if (min_db_x10 > max_db_x10) {
        int16_t t = min_db_x10;
        min_db_x10 = max_db_x10;
        max_db_x10 = t;
    }
    s_level_min_x10 = min_db_x10;
    s_level_max_x10 = max_db_x10;
}

void fmrb_audio_level_range(int16_t *min_db_x10, int16_t *max_db_x10) {
    if (min_db_x10) *min_db_x10 = s_level_min_x10;
    if (max_db_x10) *max_db_x10 = s_level_max_x10;
}

int16_t fmrb_audio_step_db_x10(uint8_t step, int16_t min_db_x10, int16_t max_db_x10) {
    if (step <= 1) return min_db_x10;
    if (step >= FMRB_AUDIO_VOLUME_MAX) return max_db_x10;
    int32_t span = (int32_t)max_db_x10 - min_db_x10;
    int32_t steps = FMRB_AUDIO_VOLUME_MAX - 1;
    // Rounded to the nearest tenth, so the steps stay even.
    int32_t off = (span * (step - 1) * 2 + steps) / (steps * 2);
    return (int16_t)(min_db_x10 + off);
}

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
