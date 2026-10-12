// audio_none.c - audio_p4.h for a board that draws locally but has no sound
//
// The local display task (display_local_task.cpp) hands every link message of
// type AUDIO to audio_p4_process_command() and brings the output up through
// audio_backend() at boot. A board with neither a codec nor a speaker (the
// AtomS3R) builds this file in place of the APU driver: the commands are
// accepted and dropped, so the apps and the kernel that send them behave as
// they do on a muted machine, and no APU task or sample buffer is created.

#include "audio_p4.h"

static fmrb_err_t audio_none_init(void)
{
    return FMRB_OK;
}

static bool audio_none_ready(void)
{
    return true;
}

static void audio_none_write(const int16_t *samples, int len, int channels)
{
    (void)samples;
    (void)len;
    (void)channels;
}

static const audio_backend_t s_backend_none = {
    .name  = "none",
    .init  = audio_none_init,
    .ready = audio_none_ready,
    .write = audio_none_write,
};

const audio_backend_t *audio_backend(void)
{
    return &s_backend_none;
}

fmrb_err_t audio_p4_task_init(void)
{
    return FMRB_OK;
}

int audio_p4_process_command(const uint8_t *data, size_t size)
{
    (void)data;
    (void)size;
    return 0;
}

bool audio_p4_mic_available(void)
{
    return false;
}

int audio_p4_mic_sample_rate(void)
{
    return 0;
}

fmrb_err_t audio_p4_mic_enable(bool on)
{
    (void)on;
    return FMRB_ERR_NOT_SUPPORTED;
}

int audio_p4_mic_read(int16_t *dst, int max_samples, int timeout_ms)
{
    (void)dst;
    (void)max_samples;
    (void)timeout_ms;
    return -1;
}
