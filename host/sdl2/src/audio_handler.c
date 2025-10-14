#include "audio_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

typedef struct {
    uint32_t music_id;
    uint8_t *data;
    uint32_t size;
} music_track_t;

static SDL_AudioDeviceID audio_device = 0;
static fmrb_audio_status_t current_status = FMRB_AUDIO_STATUS_STOPPED;
static uint8_t current_volume = 128;
static music_track_t music_tracks[FMRB_MAX_MUSIC_TRACKS];
static int track_count = 0;

// Simple audio callback (placeholder)
void audio_callback(void *userdata, Uint8 *stream, int len) {
    // For now, just output silence
    memset(stream, 0, len);
}

int audio_handler_init(void) {
    SDL_AudioSpec want, have;

    // Initialize SDL audio subsystem if not already initialized
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            fprintf(stderr, "Failed to initialize SDL audio subsystem: %s\n", SDL_GetError());
            return -1;
        }
    }

    // Clear music tracks
    memset(music_tracks, 0, sizeof(music_tracks));
    track_count = 0;

    // Setup audio specification
    SDL_zero(want);
    want.freq = FMRB_AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16LSB;
    want.channels = FMRB_AUDIO_CHANNELS;
    want.samples = FMRB_AUDIO_BUFFER_SIZE;
    want.callback = audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
        return -1;
    }

    printf("Audio handler initialized: %d Hz, %d channels\n",
           have.freq, have.channels);
    return 0;
}

void audio_handler_cleanup(void) {
    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    // Free music tracks
    for (int i = 0; i < track_count; i++) {
        if (music_tracks[i].data) {
            free(music_tracks[i].data);
            music_tracks[i].data = NULL;
        }
    }
    track_count = 0;

    printf("Audio handler cleaned up\n");
}

static int process_load_command(const fmrb_audio_load_cmd_t *cmd, const uint8_t *music_data) {
    if (track_count >= FMRB_MAX_MUSIC_TRACKS) {
        fprintf(stderr, "Maximum music tracks reached\n");
        return -1;
    }

    // Find existing track or create new one
    int track_idx = -1;
    for (int i = 0; i < track_count; i++) {
        if (music_tracks[i].music_id == cmd->music_id) {
            track_idx = i;
            break;
        }
    }

    if (track_idx == -1) {
        track_idx = track_count++;
    } else {
        // Free existing data
        if (music_tracks[track_idx].data) {
            free(music_tracks[track_idx].data);
        }
    }

    // Store music data
    music_tracks[track_idx].music_id = cmd->music_id;
    music_tracks[track_idx].size = cmd->data_size;
    music_tracks[track_idx].data = malloc(cmd->data_size);

    if (!music_tracks[track_idx].data) {
        fprintf(stderr, "Failed to allocate music data\n");
        return -1;
    }

    memcpy(music_tracks[track_idx].data, music_data, cmd->data_size);

    printf("Loaded music track %u (%u bytes)\n", cmd->music_id, cmd->data_size);
    return 0;
}

static int process_play_command(const fmrb_audio_play_cmd_t *cmd) {
    // Find music track
    for (int i = 0; i < track_count; i++) {
        if (music_tracks[i].music_id == cmd->music_id) {
            printf("Playing music track %u\n", cmd->music_id);
            current_status = FMRB_AUDIO_STATUS_PLAYING;
            SDL_PauseAudioDevice(audio_device, 0);
            return 0;
        }
    }

    fprintf(stderr, "Music track %u not found\n", cmd->music_id);
    return -1;
}

static int process_stop_command(void) {
    printf("Stopping audio playback\n");
    current_status = FMRB_AUDIO_STATUS_STOPPED;
    SDL_PauseAudioDevice(audio_device, 1);
    return 0;
}

static int process_pause_command(void) {
    printf("Pausing audio playback\n");
    current_status = FMRB_AUDIO_STATUS_PAUSED;
    SDL_PauseAudioDevice(audio_device, 1);
    return 0;
}

static int process_resume_command(void) {
    printf("Resuming audio playback\n");
    current_status = FMRB_AUDIO_STATUS_PLAYING;
    SDL_PauseAudioDevice(audio_device, 0);
    return 0;
}

static int process_volume_command(const fmrb_audio_volume_cmd_t *cmd) {
    current_volume = cmd->volume;
    printf("Set volume to %u\n", cmd->volume);
    // Note: SDL2 doesn't have built-in volume control, would need mixing
    return 0;
}

int audio_handler_process_command(const uint8_t *data, size_t size) {
    if (!data || size == 0) {
        return -1;
    }

    uint8_t cmd_type = data[0];

    switch (cmd_type) {
        case FMRB_AUDIO_CMD_LOAD_BINARY:
            if (size >= sizeof(fmrb_audio_load_cmd_t)) {
                const fmrb_audio_load_cmd_t *cmd = (const fmrb_audio_load_cmd_t*)data;
                const uint8_t *music_data = data + sizeof(fmrb_audio_load_cmd_t);
                if (size >= sizeof(fmrb_audio_load_cmd_t) + cmd->data_size) {
                    return process_load_command(cmd, music_data);
                }
            }
            break;

        case FMRB_AUDIO_CMD_PLAY:
            if (size >= sizeof(fmrb_audio_play_cmd_t)) {
                return process_play_command((const fmrb_audio_play_cmd_t*)data);
            }
            break;

        case FMRB_AUDIO_CMD_STOP:
            return process_stop_command();

        case FMRB_AUDIO_CMD_PAUSE:
            return process_pause_command();

        case FMRB_AUDIO_CMD_RESUME:
            return process_resume_command();

        case FMRB_AUDIO_CMD_SET_VOLUME:
            if (size >= sizeof(fmrb_audio_volume_cmd_t)) {
                return process_volume_command((const fmrb_audio_volume_cmd_t*)data);
            }
            break;

        default:
            fprintf(stderr, "Unknown audio command: 0x%02x\n", cmd_type);
            return -1;
    }

    fprintf(stderr, "Invalid command size for audio type 0x%02x\n", cmd_type);
    return -1;
}

fmrb_audio_status_t audio_handler_get_status(void) {
    return current_status;
}

void audio_handler_set_volume(uint8_t volume) {
    current_volume = volume;
}