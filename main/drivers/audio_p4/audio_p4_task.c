// APU engine task for the Modern (P4) target: dual-instance NES APU at
// 60 Hz, NSF playback on the MAIN instance, FMSQ + note SFX on the SUB
// instance. Ported from fmruby-graphics-audio main/tasks/audio_task.c.
//
// Unlike the WROVER original (which relied on commands touching only
// flags), a mutex serializes the command path (display task context)
// against the 60 Hz frame processing, because every APU register access
// swaps the nofrendo global context and is not reentrant.

#include "audio_p4_internal.h"

#include "fmrb_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "apu_if.h"
#include "apu_helper.h"
#include "nsf_player.h"
#include "fmsq_player.h"

#include <stdio.h>
#include <string.h>
#include "fmrb_task_config.h"

static const char *TAG = "audio_p4";

// Bring-up instrumentation for the microphone (doc/mic_spectrum/report/
// track_b.md). Off: it has done its job, and it is not free -- the tone check
// plays notes on APU channel 0, which is where the desktop's boot jingle plays
// its melody, so leaving it on rewrites the start-up sound. Build with
// -DFMRB_MIC_SELFTEST=1 when the microphone path needs proving again.
#ifndef FMRB_MIC_SELFTEST
#define FMRB_MIC_SELFTEST 0
#endif

// Frames to wait before the tone check starts, when it is on at all. The boot
// jingle owns the APU channels for about half a second after the desktop
// appears; 3 seconds is well clear of it.
#define FMRB_MIC_TONE_DELAY_FRAMES 180

#define NTSC_SAMPLE 262
#define NSF_DEFAULT_FILE "/flash/data/test.nsf"

static nsf_player_t  *g_nsf_player  = NULL;
static fmsq_player_t *g_fmsq_player = NULL;
static SemaphoreHandle_t g_engine_lock = NULL;
static volatile bool g_engine_ready = false;

static void engine_lock(void)   { xSemaphoreTake(g_engine_lock, portMAX_DELAY); }
static void engine_unlock(void) { xSemaphoreGive(g_engine_lock); }

// ------------------------------------------------------------------
// Engine operations (called from the command dispatch path)
// ------------------------------------------------------------------

int audio_p4_engine_nsf_play(const char *path, int track) {
    if (!g_engine_ready) return -1;
    if (!path || path[0] == '\0') {
        FMRB_LOGW(TAG, "NSF play: empty path");
        return -1;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/flash%s", path);
    FMRB_LOGI(TAG, "NSF play: %s track=%d", full_path, track);

    engine_lock();

    if (g_nsf_player && g_nsf_player->playing) {
        g_nsf_player->playing = 0;
    }
    if (g_nsf_player) {
        nsf_player_free(g_nsf_player);
    } else {
        g_nsf_player = (nsf_player_t *)apuemu_malloc(sizeof(nsf_player_t));
        if (!g_nsf_player) {
            engine_unlock();
            FMRB_LOGE(TAG, "NSF play: malloc failed");
            return -1;
        }
    }

    if (nsf_player_load(g_nsf_player, full_path) != 0) {
        apuemu_free(g_nsf_player);
        g_nsf_player = NULL;
        engine_unlock();
        FMRB_LOGE(TAG, "NSF play: failed to load %s", full_path);
        return -1;
    }

    apuif_select(APUIF_INSTANCE_MAIN);
    nsf_player_start(g_nsf_player, track);

    engine_unlock();
    return 0;
}

void audio_p4_engine_nsf_stop(void) {
    if (!g_engine_ready) return;
    engine_lock();
    if (g_nsf_player) {
        FMRB_LOGI(TAG, "NSF stop");
        g_nsf_player->playing = 0;
        // Silence all APU channels
        apuif_select(APUIF_INSTANCE_MAIN);
        apuif_write_reg(0x4015, 0x00);
    }
    engine_unlock();
}

int audio_p4_engine_fmsq_play_slot(uint32_t music_id) {
    if (!g_engine_ready) return -1;

    const uint8_t *data = NULL;
    uint32_t size = 0;
    if (audio_p4_get_track(music_id, &data, &size) != 0) {
        FMRB_LOGE(TAG, "FMSQ play_slot: track %lu not found", (unsigned long)music_id);
        return -1;
    }

    engine_lock();

    if (!g_fmsq_player) {
        g_fmsq_player = (fmsq_player_t *)apuemu_malloc(sizeof(fmsq_player_t));
        if (!g_fmsq_player) {
            engine_unlock();
            FMRB_LOGE(TAG, "FMSQ play_slot: malloc failed");
            return -1;
        }
        memset(g_fmsq_player, 0, sizeof(fmsq_player_t));
    }

    g_fmsq_player->playing = 0;
    apuif_select(APUIF_INSTANCE_SUB);

    if (fmsq_player_load_from_memory(g_fmsq_player, data, size) != 0) {
        engine_unlock();
        FMRB_LOGE(TAG, "FMSQ play_slot: load failed for track %lu", (unsigned long)music_id);
        return -1;
    }

    fmsq_player_reset(g_fmsq_player);
    engine_unlock();

    FMRB_LOGI(TAG, "FMSQ play_slot: playing track %lu", (unsigned long)music_id);
    return 0;
}

int audio_p4_engine_note_on(uint8_t channel, uint16_t freq, uint8_t volume,
                            uint8_t duty, uint8_t sweep) {
    if (!g_engine_ready) return -1;
    // freq is a pitch in Hz for the tone channels, where 0 is meaningless, but
    // on noise it is a period index (0..15, plus the mode bit) where 0 is the
    // shortest period - a legal note. Rejecting 0 for every channel silently
    // dropped every noise hit at period 0.
    if (freq == 0 && channel != FMRB_APU_CH_NOISE) return -1;

    engine_lock();

    apuif_select(APUIF_INSTANCE_SUB);

    // Stop FMSQ playback if running (avoid conflicts on the SUB instance)
    if (g_fmsq_player && g_fmsq_player->playing) {
        g_fmsq_player->playing = 0;
    }

    int ret = 0;
    switch (channel) {
    case FMRB_APU_CH_PULSE1:
    case FMRB_APU_CH_PULSE2:
        apu_pulse_note_on(channel, freq, volume, duty, sweep);
        break;
    case FMRB_APU_CH_TRIANGLE:
        apu_triangle_note_on(freq);
        break;
    case FMRB_APU_CH_NOISE:
        apu_noise_note_on(freq & 0x0F, (freq & 0x80) ? 1 : 0, volume);
        break;
    default:
        ret = -1;
        break;
    }

    engine_unlock();
    return ret;
}

int audio_p4_engine_note_off(uint8_t channel) {
    if (!g_engine_ready) return -1;

    engine_lock();

    apuif_select(APUIF_INSTANCE_SUB);

    int ret = 0;
    switch (channel) {
    case FMRB_APU_CH_PULSE1:
    case FMRB_APU_CH_PULSE2:
        apu_pulse_note_off(channel);
        break;
    case FMRB_APU_CH_TRIANGLE:
        apu_triangle_note_off();
        break;
    case FMRB_APU_CH_NOISE:
        apu_noise_note_off();
        break;
    default:
        ret = -1;
        break;
    }

    engine_unlock();
    return ret;
}

// ------------------------------------------------------------------
// Boot beep: the "piko" the Retro machine makes while the boot screen waits
// for the kernel. On Retro the sound comes from the graphics-audio board,
// which plays it on its own initiative (play_boot_beep in its graphics task);
// Modern never inherited that, because here the APU lives in this firmware.
// This is that beep, in its place: 880 Hz then 1760 Hz, short, on PULSE1.
//
// It is driven from the frame loop rather than written straight through with
// delays, because the APU only emits while this loop feeds it -- sleeping
// inside a note would produce silence. The whole thing is over about 200 ms
// after the audio task starts, which is during "Waiting for kernel..." and
// well before the desktop's boot jingle wants the same channel.
// ------------------------------------------------------------------

#define BOOT_BEEP_HI_FREQ   1760
#define BOOT_BEEP_LO_FREQ   880

static void boot_beep_tick(void) {
    static int frame = 0;
    static bool done = false;

    if (done) return;
    frame++;

    switch (frame) {
    case 1:
        audio_p4_engine_note_on(FMRB_APU_CH_PULSE1, BOOT_BEEP_LO_FREQ, 8, 2, 0);
        break;
    case 6:     // ~83 ms
        audio_p4_engine_note_off(FMRB_APU_CH_PULSE1);
        break;
    case 8:     // ~33 ms of silence between the two
        audio_p4_engine_note_on(FMRB_APU_CH_PULSE1, BOOT_BEEP_HI_FREQ, 6, 2, 0);
        break;
    case 12:    // ~66 ms
        audio_p4_engine_note_off(FMRB_APU_CH_PULSE1);
        done = true;
        break;
    default:
        break;
    }
}

#if FMRB_MIC_SELFTEST
// Play a note, listen, report what came back. Two frequencies, a few frames
// apart, driven from the 60 Hz loop one step at a time (see the call site).
// Ends by putting the microphone and the channel back the way it found them.
static void mic_tone_check(void) {
    static const uint16_t tones[] = { 1000, 2000 };
    static int wait = FMRB_MIC_TONE_DELAY_FRAMES;
    static int step = 0;
    static int tone_idx = 0;
    static bool done = false;

    if (done) return;
    if (wait > 0) { wait--; return; }   // let the boot jingle have the APU
    step++;

    switch (step) {
    case 1:
        if (audio_p4_mic_enable(true) != FMRB_OK) { done = true; return; }
        audio_p4_engine_note_on(FMRB_APU_CH_PULSE1, tones[tone_idx], 12, 2, 0);
        break;
    case 20: {   // ~320 ms of tone: the codec has settled and the note is out
        int hz = audio_p4_mic_peak_hz();
        FMRB_LOGI(TAG, "mic tone check: played %u Hz, heard %d Hz",
                  (unsigned)tones[tone_idx], hz);
        break;
    }
    case 26:
        audio_p4_engine_note_off(FMRB_APU_CH_PULSE1);
        tone_idx++;
        if (tone_idx < (int)(sizeof(tones) / sizeof(tones[0]))) {
            step = 0;            // round two, from the top
        } else {
            audio_p4_mic_enable(false);
            done = true;         // once per boot
        }
        break;
    default:
        break;
    }
}
#endif

// ------------------------------------------------------------------
// 60 Hz frame task
// ------------------------------------------------------------------

static void audio_p4_task(void *arg) {
    (void)arg;

    // Codec bring-up happens in the display task; wait for it.
    while (!audio_p4_hw_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    FMRB_LOGI(TAG, "Audio task started on core %d", xPortGetCoreID());

    apuif_set_output_writer(audio_p4_hw_write);
    apuif_init();       // instance 0: NSF
    apuif_init_sub();   // instance 1: FMSQ + note SFX

    // Load default NSF file (playback starts on command from the kernel)
    g_nsf_player = (nsf_player_t *)apuemu_malloc(sizeof(nsf_player_t));
    if (g_nsf_player) {
        if (nsf_player_load(g_nsf_player, NSF_DEFAULT_FILE) == 0) {
            FMRB_LOGI(TAG, "NSF loaded: %d songs (waiting for play command)",
                      g_nsf_player->header.total_songs);
        } else {
            FMRB_LOGW(TAG, "No NSF file at %s", NSF_DEFAULT_FILE);
            apuemu_free(g_nsf_player);
            g_nsf_player = NULL;
        }
    }

    g_engine_ready = true;

#if FMRB_MIC_SELFTEST
    // Microphone bring-up (doc/mic_spectrum): listen for about a second and
    // log what came in. Runs once, here, because this is the first moment the
    // codec, the I2S port and the I2C service are all up -- and because a
    // serial capture of the boot is the only way to settle "the microphone
    // produces values" on hardware nothing has read from before.
    audio_p4_mic_selftest();
#endif

    // 60 Hz timing
    const uint64_t target_frame_time_us = 16667;
    uint64_t next_frame_time = esp_timer_get_time();

    while (1) {
        engine_lock();

        // Tick NSF player on the main APU instance
        if (g_nsf_player && g_nsf_player->playing) {
            apuif_select(APUIF_INSTANCE_MAIN);
            nsf_player_tick(g_nsf_player);
        }

        // Tick FMSQ player on the sub APU instance
        if (g_fmsq_player && g_fmsq_player->playing) {
            apuif_select(APUIF_INSTANCE_SUB);
            fmsq_player_tick(g_fmsq_player);
        }

        // Process both APU instances and mix output
        int16_t buffer[(NTSC_SAMPLE + 1) * 2];
        memset(buffer, 0, sizeof(buffer));
        int count = apuif_process_mix(buffer, sizeof(buffer) / sizeof(buffer[0]));

        engine_unlock();

        if (count > 0) {
            // Blocking write outside the lock: I2S DMA paces us
            apuif_audio_write(buffer, count, 1);
        }

        // The boot "piko", once, in the first few frames of this task.
        boot_beep_tick();

#if FMRB_MIC_SELFTEST
        // Tone check: play a known note through the speaker and ask the
        // microphone what it hears. Levels only prove that samples arrive;
        // this proves they are the sound in the room. Spread across frames
        // rather than done in one call, because the APU only produces the
        // tone while this loop keeps feeding it.
        mic_tone_check();
#endif

        // Frame timing safety net (the codec write normally paces us)
        next_frame_time += target_frame_time_us;
        uint64_t now = esp_timer_get_time();
        int64_t sleep_time_us = next_frame_time - now;
        if (sleep_time_us > 1000) {
            vTaskDelay(pdMS_TO_TICKS(sleep_time_us / 1000));
        } else if (sleep_time_us < 0) {
            next_frame_time = now;
        }
    }
}

fmrb_err_t audio_p4_task_init(void) {
    g_engine_lock = xSemaphoreCreateMutex();
    if (!g_engine_lock) {
        FMRB_LOGE(TAG, "Failed to create engine lock");
        return FMRB_ERR_FAILED;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        audio_p4_task, "audio_p4", FMRB_AUDIO_P4_TASK_STACK_SIZE, NULL,
        FMRB_AUDIO_P4_TASK_PRIORITY, NULL, FMRB_AUDIO_P4_TASK_CORE);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create audio_p4 task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}
