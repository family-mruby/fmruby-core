// Audio command dispatch for the Modern (P4) target. Parses the packed
// command bytes produced by the kernel (audio_handler.rb) and routes
// them to the local APU engine. Ported from fmruby-graphics-audio
// main/audio/audio_handler_esp32.c, plus LOAD_FMSQ_FILE support (reads
// the FMSQ blob from the local /flash LittleFS).

#include "audio_p4_internal.h"

#include "fmrb_log.h"

#include "apu_if.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio_p4";

typedef struct {
    uint32_t music_id;
    uint8_t *data;
    uint32_t size;
} music_track_t;

static music_track_t s_tracks[FMRB_MAX_MUSIC_TRACKS];
static int s_track_count = 0;

// Store an FMSQ blob into a slot keyed by music_id (PSRAM).
int audio_p4_store_track(uint32_t music_id, const uint8_t *data, uint32_t size) {
    int idx = -1;
    for (int i = 0; i < s_track_count; i++) {
        if (s_tracks[i].music_id == music_id) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        if (s_track_count >= FMRB_MAX_MUSIC_TRACKS) {
            FMRB_LOGE(TAG, "Maximum music tracks reached");
            return -1;
        }
        idx = s_track_count++;
    } else if (s_tracks[idx].data) {
        apuemu_free(s_tracks[idx].data);
        s_tracks[idx].data = NULL;
    }

    s_tracks[idx].music_id = music_id;
    s_tracks[idx].size = size;
    s_tracks[idx].data = (uint8_t *)apuemu_malloc(size);
    if (!s_tracks[idx].data) {
        FMRB_LOGE(TAG, "Failed to allocate music data (%lu bytes)", (unsigned long)size);
        s_tracks[idx].size = 0;
        return -1;
    }
    memcpy(s_tracks[idx].data, data, size);

    FMRB_LOGI(TAG, "Loaded music track %lu (%lu bytes)",
              (unsigned long)music_id, (unsigned long)size);
    return 0;
}

int audio_p4_get_track(uint32_t music_id, const uint8_t **out_data, uint32_t *out_size) {
    for (int i = 0; i < s_track_count; i++) {
        if (s_tracks[i].music_id == music_id && s_tracks[i].data) {
            *out_data = s_tracks[i].data;
            *out_size = s_tracks[i].size;
            return 0;
        }
    }
    return -1;
}

static int process_play_command(const fmrb_audio_play_cmd_t *cmd, size_t total_size) {
    if (total_size < sizeof(fmrb_audio_play_cmd_t) + cmd->path_len) {
        FMRB_LOGE(TAG, "Play command too short");
        return -1;
    }

    char path[128];
    int len = cmd->path_len < sizeof(path) - 1 ? cmd->path_len : (int)sizeof(path) - 1;
    memcpy(path, cmd->path, len);
    path[len] = '\0';

    int track = 0;
    if (total_size > sizeof(fmrb_audio_play_cmd_t) + cmd->path_len) {
        track = (int)((uint8_t)cmd->path[cmd->path_len]);
    }

    return audio_p4_engine_nsf_play(path, track);
}

// Read an FMSQ file from the local filesystem into a track slot
static int process_load_fmsq_file(const fmrb_audio_load_fmsq_file_cmd_t *cmd, size_t total_size) {
    if (total_size < sizeof(fmrb_audio_load_fmsq_file_cmd_t) + cmd->path_len) {
        FMRB_LOGE(TAG, "load_fmsq_file command too short");
        return -1;
    }

    char full_path[256];
    int len = cmd->path_len < 128 ? cmd->path_len : 127;
    snprintf(full_path, sizeof(full_path), "/flash%.*s", len, cmd->path);

    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        FMRB_LOGE(TAG, "load_fmsq_file: cannot open %s", full_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 256 * 1024) {
        FMRB_LOGE(TAG, "load_fmsq_file: invalid size %ld for %s", fsize, full_path);
        fclose(fp);
        return -1;
    }

    uint8_t *buf = (uint8_t *)apuemu_malloc((uint32_t)fsize);
    if (!buf) {
        FMRB_LOGE(TAG, "load_fmsq_file: malloc failed (%ld bytes)", fsize);
        fclose(fp);
        return -1;
    }
    size_t rd = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    if (rd != (size_t)fsize) {
        FMRB_LOGE(TAG, "load_fmsq_file: short read %zu/%ld", rd, fsize);
        apuemu_free(buf);
        return -1;
    }

    int ret = audio_p4_store_track(cmd->music_id, buf, (uint32_t)fsize);
    apuemu_free(buf);
    if (ret == 0) {
        FMRB_LOGI(TAG, "load_fmsq_file: %s -> slot %lu",
                  full_path, (unsigned long)cmd->music_id);
    }
    return ret;
}

int audio_p4_process_command(const uint8_t *data, size_t size) {
    if (!data || size == 0) {
        return -1;
    }

    uint8_t cmd_type = data[0];

    switch (cmd_type) {
        case FMRB_AUDIO_CMD_LOAD_BINARY:
            if (size >= sizeof(fmrb_audio_load_cmd_t)) {
                const fmrb_audio_load_cmd_t *cmd = (const fmrb_audio_load_cmd_t *)data;
                if (size >= sizeof(fmrb_audio_load_cmd_t) + cmd->data_size) {
                    return audio_p4_store_track(cmd->music_id,
                                                data + sizeof(fmrb_audio_load_cmd_t),
                                                cmd->data_size);
                }
            }
            break;

        case FMRB_AUDIO_CMD_PLAY:
            if (size >= sizeof(fmrb_audio_play_cmd_t)) {
                return process_play_command((const fmrb_audio_play_cmd_t *)data, size);
            }
            break;

        case FMRB_AUDIO_CMD_STOP:
            audio_p4_engine_nsf_stop();
            return 0;

        case FMRB_AUDIO_CMD_PAUSE:
        case FMRB_AUDIO_CMD_RESUME:
            // Not implemented (parity with the WROVER backend)
            return 0;

        case FMRB_AUDIO_CMD_SET_VOLUME:
            if (size >= sizeof(fmrb_audio_volume_cmd_t)) {
                const fmrb_audio_volume_cmd_t *cmd = (const fmrb_audio_volume_cmd_t *)data;
                audio_p4_hw_set_volume(cmd->volume);
                return 0;
            }
            break;

        case FMRB_AUDIO_CMD_PLAY_SLOT:
            if (size >= sizeof(fmrb_audio_play_slot_cmd_t)) {
                const fmrb_audio_play_slot_cmd_t *cmd = (const fmrb_audio_play_slot_cmd_t *)data;
                return audio_p4_engine_fmsq_play_slot(cmd->music_id);
            }
            break;

        case FMRB_AUDIO_CMD_NOTE_ON:
            if (size >= sizeof(fmrb_audio_note_on_cmd_t)) {
                const fmrb_audio_note_on_cmd_t *cmd = (const fmrb_audio_note_on_cmd_t *)data;
                return audio_p4_engine_note_on(cmd->channel, cmd->freq, cmd->volume,
                                               cmd->duty, cmd->sweep);
            }
            break;

        case FMRB_AUDIO_CMD_NOTE_OFF:
            if (size >= sizeof(fmrb_audio_note_off_cmd_t)) {
                const fmrb_audio_note_off_cmd_t *cmd = (const fmrb_audio_note_off_cmd_t *)data;
                return audio_p4_engine_note_off(cmd->channel);
            }
            break;

        case FMRB_AUDIO_CMD_LOAD_FMSQ_FILE:
            if (size >= sizeof(fmrb_audio_load_fmsq_file_cmd_t)) {
                return process_load_fmsq_file(
                    (const fmrb_audio_load_fmsq_file_cmd_t *)data, size);
            }
            break;

        default:
            FMRB_LOGE(TAG, "Unknown audio command: 0x%02x", cmd_type);
            return -1;
    }

    FMRB_LOGE(TAG, "Invalid command size for audio type 0x%02x", cmd_type);
    return -1;
}
