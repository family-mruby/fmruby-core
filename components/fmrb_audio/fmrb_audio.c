#include "fmrb_audio.h"
#include "fmrb_hal.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_transport.h"
#include "fmrb_mem.h"
#include "esp_log.h"
#include <stdlib.h>
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

static fmrb_audio_err_t send_apu_command(fmrb_apu_cmd_t cmd, const void* data, size_t data_size) {
    // Create command packet
    size_t packet_size = sizeof(fmrb_apu_cmd_t) + data_size;
    uint8_t* packet = fmrb_sys_malloc(packet_size);
    if (!packet) {
        return FMRB_AUDIO_ERR_NO_MEMORY;
    }

    // Pack command and data
    memcpy(packet, &cmd, sizeof(fmrb_apu_cmd_t));
    if (data && data_size > 0) {
        memcpy(packet + sizeof(fmrb_apu_cmd_t), data, data_size);
    }

    // Send via link communication
    fmrb_link_message_t msg = {
        .data = packet,
        .size = packet_size
    };

    fmrb_err_t ret = fmrb_hal_link_send(FMRB_LINK_AUDIO, &msg, 1000);
    fmrb_sys_free(packet);

    if (ret == FMRB_OK) {
        ESP_LOGI(TAG, "APU command 0x%02x sent", cmd);
        return FMRB_AUDIO_OK;
    } else {
        ESP_LOGE(TAG, "Failed to send APU command 0x%02x", cmd);
        return FMRB_AUDIO_ERR_FAILED;
    }
}

fmrb_audio_err_t fmrb_audio_init(void) {
    if (audio_ctx.initialized) {
        return FMRB_AUDIO_OK;
    }

    // Initialize link communication if needed
    fmrb_err_t ret = fmrb_hal_link_init();
    if (ret != FMRB_OK) {
        ESP_LOGE(TAG, "Failed to initialize link communication");
        return FMRB_AUDIO_ERR_FAILED;
    }

    audio_ctx.initialized = true;
    audio_ctx.current_status = FMRB_APU_STATUS_STOPPED;
    audio_ctx.current_volume = 128;

    ESP_LOGI(TAG, "Audio subsystem (APU emulator) initialized");
    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_deinit(void) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_OK;
    }

    // Stop any playing audio
    fmrb_audio_stop();

    audio_ctx.initialized = false;
    ESP_LOGI(TAG, "Audio subsystem deinitialized");
    return FMRB_AUDIO_OK;
}

fmrb_audio_err_t fmrb_audio_load_music(const fmrb_audio_music_t* music) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    if (!music || !music->data || music->size == 0) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    ESP_LOGI(TAG, "Loading music binary: ID=%u, size=%zu bytes", music->id, music->size);

    // Send load command with music data
    // Note: For large binaries, this might need chunking in production
    fmrb_audio_err_t ret = send_apu_command(FMRB_APU_CMD_LOAD_BINARY, music, sizeof(fmrb_audio_music_t));
    if (ret == FMRB_AUDIO_OK) {
        // Send the actual binary data
        ret = send_apu_command(FMRB_APU_CMD_LOAD_BINARY, music->data, music->size);
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_play(uint32_t music_id) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    ESP_LOGI(TAG, "Starting playback: music_id=%u", music_id);

    fmrb_audio_err_t ret = send_apu_command(FMRB_APU_CMD_PLAY, &music_id, sizeof(music_id));
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

    fmrb_audio_err_t ret = send_apu_command(FMRB_APU_CMD_STOP, NULL, 0);
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

    fmrb_audio_err_t ret = send_apu_command(FMRB_APU_CMD_PAUSE, NULL, 0);
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

    fmrb_audio_err_t ret = send_apu_command(FMRB_APU_CMD_RESUME, NULL, 0);
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

    fmrb_audio_err_t ret = send_apu_command(FMRB_APU_CMD_SET_VOLUME, &volume, sizeof(volume));
    if (ret == FMRB_AUDIO_OK) {
        audio_ctx.current_volume = volume;
    }

    return ret;
}

fmrb_audio_err_t fmrb_audio_get_status(fmrb_apu_status_t* status) {
    if (!audio_ctx.initialized) {
        return FMRB_AUDIO_ERR_NOT_INITIALIZED;
    }

    if (!status) {
        return FMRB_AUDIO_ERR_INVALID_PARAM;
    }

    // For now, return cached status
    // In production, this might query the APU via link communication
    *status = audio_ctx.current_status;

    return FMRB_AUDIO_OK;
}