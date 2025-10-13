#ifndef FMRB_HOST_AUDIO_COMMANDS_H
#define FMRB_HOST_AUDIO_COMMANDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio command types (matching APU commands)
typedef enum {
    FMRB_AUDIO_CMD_LOAD_BINARY = 0x01,
    FMRB_AUDIO_CMD_PLAY = 0x02,
    FMRB_AUDIO_CMD_STOP = 0x03,
    FMRB_AUDIO_CMD_PAUSE = 0x04,
    FMRB_AUDIO_CMD_RESUME = 0x05,
    FMRB_AUDIO_CMD_SET_VOLUME = 0x06,
    FMRB_AUDIO_CMD_GET_STATUS = 0x07
} fmrb_audio_cmd_type_t;

// Audio status
typedef enum {
    FMRB_AUDIO_STATUS_STOPPED = 0,
    FMRB_AUDIO_STATUS_PLAYING = 1,
    FMRB_AUDIO_STATUS_PAUSED = 2,
    FMRB_AUDIO_STATUS_ERROR = 3
} fmrb_audio_status_t;

// Audio command structures
typedef struct {
    uint8_t cmd_type;
    uint32_t music_id;
    uint32_t data_size;
    // music binary data follows
} __attribute__((packed)) fmrb_audio_load_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint32_t music_id;
} __attribute__((packed)) fmrb_audio_play_cmd_t;

typedef struct {
    uint8_t cmd_type;
} __attribute__((packed)) fmrb_audio_stop_cmd_t;

typedef struct {
    uint8_t cmd_type;
} __attribute__((packed)) fmrb_audio_pause_cmd_t;

typedef struct {
    uint8_t cmd_type;
} __attribute__((packed)) fmrb_audio_resume_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint8_t volume;  // 0-255
} __attribute__((packed)) fmrb_audio_volume_cmd_t;

typedef struct {
    uint8_t cmd_type;
} __attribute__((packed)) fmrb_audio_status_cmd_t;

// Audio configuration
#define FMRB_AUDIO_SAMPLE_RATE 44100
#define FMRB_AUDIO_CHANNELS    2
#define FMRB_AUDIO_BUFFER_SIZE 1024
#define FMRB_MAX_MUSIC_TRACKS  16

#ifdef __cplusplus
}
#endif

#endif // FMRB_HOST_AUDIO_COMMANDS_H