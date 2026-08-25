// Audio command protocol shared with the kernel (audio_handler.rb encodes
// these packed little-endian layouts). Mirrors fmruby-graphics-audio
// main/common/audio_commands.h so FMSQ/NSF assets and apps work unchanged
// on both the WROVER (retro) and the local P4 (modern) audio backends.
// Keep the two files identical apart from this note.
#pragma once

#include <stdint.h>
#include <stddef.h>  /* offsetof */

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
    FMRB_AUDIO_CMD_GET_STATUS = 0x07,
    FMRB_AUDIO_CMD_PLAY_SLOT = 0x08,
    FMRB_AUDIO_CMD_NOTE_ON = 0x09,
    FMRB_AUDIO_CMD_NOTE_OFF = 0x0A,
    /* Load an FMSQ slot directly from a LittleFS path. Useful when the data
     * is too large for the inline IPC payload (LOAD_BINARY limit ~150 B). */
    FMRB_AUDIO_CMD_LOAD_FMSQ_FILE = 0x0B,
    /* Load an FMSQ slot in pieces. The inline LOAD_BINARY payload is limited
     * by the app -> host message size (~150 B), which is far less than one
     * PLAY worth of sequence, so BASIC's PLAY streams it in chunks. */
    FMRB_AUDIO_CMD_LOAD_BINARY_CHUNK = 0x0C,
    /* Play a WAV file from a path on the audio side, mixed on top of the APU.
     * Modern only: the Retro link has no room for PCM, so the WROVER refuses
     * it with one log line and the caller does not send it in the first
     * place (FmrbAudio#play_wav). */
    FMRB_AUDIO_CMD_PLAY_WAV = 0x0D,
    FMRB_AUDIO_CMD_STOP_WAV = 0x0E
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
    uint16_t path_len;
    char path[];  // Flexible array member
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

typedef struct {
    uint8_t cmd_type;
    uint32_t music_id;
    /* 0 = MAIN (default; shares the instance with NSF playback)
     * 1 = SUB  (shares the instance with note_on/off SE)
     * This trailing byte is optional for backwards compatibility: older
     * senders without the byte are accepted as MAIN. */
    uint8_t instance;
} __attribute__((packed)) fmrb_audio_play_slot_cmd_t;
/* Older payloads omit `instance`; everything up to music_id is the
 * legacy wire size. */
#define FMRB_AUDIO_PLAY_SLOT_CMD_LEGACY_SIZE (offsetof(fmrb_audio_play_slot_cmd_t, instance))

typedef struct {
    uint8_t cmd_type;
    uint32_t music_id;
    uint16_t path_len;
    char path[];  // Flexible array member, LittleFS-relative (will be prefixed with /flash)
} __attribute__((packed)) fmrb_audio_load_fmsq_file_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint16_t path_len;
    char path[];  // Flexible array member, LittleFS-relative (will be prefixed with /flash)
} __attribute__((packed)) fmrb_audio_play_wav_cmd_t;

typedef struct {
    uint8_t cmd_type;
} __attribute__((packed)) fmrb_audio_stop_wav_cmd_t;

// APU channel IDs
#define FMRB_APU_CH_PULSE1    0
#define FMRB_APU_CH_PULSE2    1
#define FMRB_APU_CH_TRIANGLE  2
#define FMRB_APU_CH_NOISE     3

typedef struct {
    uint8_t cmd_type;
    uint8_t channel;    // 0=pulse1, 1=pulse2, 2=triangle, 3=noise
    uint16_t freq;      // frequency in Hz
    uint8_t volume;     // 0-15
    uint8_t duty;       // duty cycle 0-3 (pulse only, ignored for triangle/noise)
    uint8_t sweep;      // sweep register value (pulse only): bit7=enable, bit6-4=period, bit3=negate, bit2-0=shift
} __attribute__((packed)) fmrb_audio_note_on_cmd_t;

typedef struct {
    uint8_t cmd_type;
    uint8_t channel;
} __attribute__((packed)) fmrb_audio_note_off_cmd_t;

// The rate apuif_process_mix produces: one NTSC frame of 262 mono samples at
// 60 Hz. Anything mixed on top of the APU (play_wav) has to arrive at this
// rate, and both audio backends resample to it, so it lives here with the
// protocol rather than in either one.
#define FMRB_APU_MIX_RATE 15720

// Audio configuration
#define FMRB_AUDIO_SAMPLE_RATE 44100
#define FMRB_AUDIO_CHANNELS    2
#define FMRB_AUDIO_BUFFER_SIZE 1024

/* Chunked slot load. Chunks must arrive in order; offset 0 starts a new
 * assembly, and the slot is stored once offset + chunk_len == total_size.
 * A different music_id or a gap in the offsets discards the assembly. */
typedef struct {
    uint8_t cmd_type;
    uint32_t music_id;
    uint16_t total_size;   // whole sequence, same value in every chunk
    uint16_t offset;       // where this chunk starts
    uint8_t chunk_len;     // bytes following this header (<= 160)
    // chunk_len bytes of data follow
} __attribute__((packed)) fmrb_audio_load_chunk_cmd_t;

/* Guard against a runaway assembly: one PLAY is a few kB at most. */
#define FMRB_AUDIO_CHUNK_MAX_TOTAL 16384
#define FMRB_AUDIO_CHUNK_MAX_DATA  160

#define FMRB_MAX_MUSIC_TRACKS  16

#ifdef __cplusplus
}
#endif