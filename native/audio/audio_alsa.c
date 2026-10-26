/*
 * audio_alsa.c - ALSA as a sink (native_backends.h): the Cardputer Zero's
 * ES8389, HDMI audio, a PC's sound card.
 *
 * The scheduler is cooperative (hosted/port/), so nothing on the machine's
 * side may wait for the device. The APU task only writes into the ring
 * (native_audio.h, audio_backend_native.c); a thread of this file's own --
 * outside FreeRTOS -- takes period-sized pieces from it and hands them to
 * snd_pcm_writei, which is the one call here allowed to block.
 *
 *   rate       the APU's 15720 Hz mono S16, as opened; alsa-lib's plug layer
 *              converts for a device that cannot take it (soft resample on)
 *   latency    audio.latency_ms of the definition (100 by default): the
 *              device buffer
 *   underrun   snd_pcm_recover() and go on (a late start, a busy system)
 *   pacing     the thread also keeps itself to the wall clock: a sink that
 *              does not block -- a file, the null device -- would otherwise
 *              be filled as fast as the CPU goes
 *
 * The device is named by audio.device ("default", "hw:0,0", "plughw:1" ...).
 * The host test runs it against alsa-lib's file plugin (ALSA_CONFIG_PATH,
 * tools/fmrb_native.rb up --alsa-file), which writes what was played.
 */

#include <alsa/asoundlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "native_audio.h"
#include "native_backends.h"

#define PERIOD_SAMPLES 256

static snd_pcm_t *s_pcm = NULL;
static pthread_t s_thread;
static atomic_int s_run;
static int s_latency_ms = 100;
static char s_device[128];

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void *alsa_thread(void *arg)
{
    (void)arg;
    int16_t buf[PERIOD_SAMPLES];
    double t0 = now_s();
    double written = 0;          /* samples handed to the device */
    unsigned underruns = 0;
    const double ahead_max = s_latency_ms / 1000.0;
    while (atomic_load(&s_run)) {
        /* Ahead of the wall clock by more than the buffer: the device is not
         * pacing us (a file); wait the difference. */
        double ahead = written / NATIVE_AUDIO_RATE - (now_s() - t0);
        if (ahead > ahead_max) {
            double wait = ahead - ahead_max;
            struct timespec ts = { (time_t)wait, (long)((wait - (time_t)wait) * 1e9) };
            nanosleep(&ts, NULL);
            continue;
        }
        native_audio_read(buf, PERIOD_SAMPLES);
        snd_pcm_sframes_t n = snd_pcm_writei(s_pcm, buf, PERIOD_SAMPLES);
        if (n < 0) {
            if (n == -EPIPE) underruns++;
            n = snd_pcm_recover(s_pcm, (int)n, 1);
            if (n < 0) {
                fprintf(stderr, "native: alsa %s: %s; stopping the sound\n", s_device, snd_strerror((int)n));
                break;
            }
            /* After a recovery the clock starts again from here. */
            t0 = now_s();
            written = 0;
            continue;
        }
        written += (double)n;
    }
    if (underruns > 0) fprintf(stderr, "native: alsa %s: %u underruns recovered\n", s_device, underruns);
    return NULL;
}

static bool alsa_open(const native_machine_t *m, char *why, size_t why_len)
{
    snprintf(s_device, sizeof(s_device), "%s", m->audio.device[0] ? m->audio.device : "default");
    s_latency_ms = m->audio.latency_ms > 0 ? m->audio.latency_ms : 100;
    int err = snd_pcm_open(&s_pcm, s_device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        snprintf(why, why_len, "cannot open the ALSA device \"%s\": %s%s", s_device, snd_strerror(err),
                 err == -ENOENT ? " (no sound card, or the name is not in the ALSA configuration)" : "");
        s_pcm = NULL;
        return false;
    }
    err = snd_pcm_set_params(s_pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                             1, NATIVE_AUDIO_RATE, 1, (unsigned)s_latency_ms * 1000u);
    if (err < 0) {
        snprintf(why, why_len, "the ALSA device \"%s\" does not take %d Hz mono S16: %s",
                 s_device, NATIVE_AUDIO_RATE, snd_strerror(err));
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return false;
    }
    atomic_store(&s_run, 1);
    if (pthread_create(&s_thread, NULL, alsa_thread, NULL) != 0) {
        snprintf(why, why_len, "cannot start the ALSA thread");
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return false;
    }
    fprintf(stderr, "native: audio on ALSA \"%s\" (%d Hz mono, %d ms)\n",
            s_device, NATIVE_AUDIO_RATE, s_latency_ms);
    return true;
}

static void alsa_close(void)
{
    if (s_pcm == NULL) return;
    atomic_store(&s_run, 0);
    pthread_join(s_thread, NULL);
    snd_pcm_drop(s_pcm);
    snd_pcm_close(s_pcm);
    s_pcm = NULL;
}

const native_audio_sink_t native_audio_alsa = {
    .name  = "alsa",
    .open  = alsa_open,
    .close = alsa_close,
};
