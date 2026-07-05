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

static const char *TAG = "audio_p4";

#define NTSC_SAMPLE 262
#define AUDIO_P4_TASK_STACK  8192
#define AUDIO_P4_TASK_PRIO   6
#define AUDIO_P4_TASK_CORE   0

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
    if (freq == 0) return -1;

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

    // 60 Hz timing
    const uint64_t target_frame_time_us = 16667;
    uint64_t next_frame_time = esp_timer_get_time();
    uint32_t frame_count = 0;

    while (1) {
        // Headphone jack polling: every 30 frames (~0.5 s)
        if ((frame_count++ % 30) == 0) {
            audio_p4_hw_poll_headphone();
        }

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
        audio_p4_task, "audio_p4", AUDIO_P4_TASK_STACK, NULL,
        AUDIO_P4_TASK_PRIO, NULL, AUDIO_P4_TASK_CORE);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create audio_p4 task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}
