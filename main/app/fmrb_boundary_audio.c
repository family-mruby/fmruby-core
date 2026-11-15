/*
 * The app boundary's FmrbAudio flat functions (doc/app_boundary): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from sig/fmrb_audio.rbs. Every
 * engine's glue (mruby, Lua, MicroPython, Spinel, WAMR) is generated and
 * calls these; nothing here knows an engine.
 *
 * FmrbAudio's methods take a receiver number, as FmrbGfx's do, so the shape
 * stays when an app gets sound state of its own (decided after W4b-2). It is
 * not read yet: every engine passes 0 and the sound is the calling app's
 * (fmrb_current()). Most of them are a command to the kernel's audio
 * handler (main/ruby/kernel/fmrb_kernel/audio_handler.rb), a MessagePack map
 * with String keys, built here straight into the message -- the same bytes
 * the Ruby wrappers sent from a Hash before (doc/app_boundary W4b-2), so the
 * kernel cannot tell the difference, and no engine builds a Hash for it.
 * note_on / note_off use the builder every guest already shared
 * (fmrb_app_build_audio_note_msg). A full message queue drops the command,
 * as it did before (logged, not an error): a note in a stream must not stop
 * an app.
 *
 * Results follow fmrb_gen/fmrb_boundary.h: 0 or more on success,
 * FMRB_BOUNDARY_ERR_FAILED, or FMRB_BOUNDARY_ERR_ARG for an argument out of
 * range (a command too big for one message).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_kernel.h"
#include "picoruby_fmrb_app.h"
#include "fmrb_boundary_msg.h"

#define ERR_FAILED FMRB_BOUNDARY_ERR_FAILED
#define ERR_ARG    FMRB_BOUNDARY_ERR_ARG

static const char *TAG = "audio";

/* ---- sending -------------------------------------------------------------- */

static fmrb_app_task_context_t *cmd_begin(fmrb_bnd_cmd_t *c, const char *cmd, int npairs)
{
    return fmrb_bnd_cmd_begin(c, FMRB_MSG_TYPE_APP_AUDIO, cmd, npairs);
}

static int32_t cmd_send(fmrb_bnd_cmd_t *c, const char *cmd)
{
    return fmrb_bnd_cmd_send(c, TAG, cmd);
}

static int32_t send_bare(const char *cmd)
{
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, cmd, 1)) {
        return ERR_FAILED;
    }
    return cmd_send(&c, cmd);
}

static __attribute__((unused)) int32_t send_path(const char *cmd, const char *path, int32_t path_len)
{
    if (!path || path_len < 0) {
        return ERR_ARG;
    }
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, cmd, 2)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "path");
    put_str(&c.out, path, (size_t)path_len);
    return cmd_send(&c, cmd);
}

/* ---- the flat functions --------------------------------------------------- */

int32_t fmrb_bnd_audio_play_raw(int32_t audio, const char *path, int32_t path_len, int32_t track)
{
    (void)audio;
    if (!path || path_len < 0) {
        return ERR_ARG;
    }
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, "play", 3)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "path");
    put_str(&c.out, path, (size_t)path_len);
    put_key(&c.out, "track");
    put_int(&c.out, track);
    return cmd_send(&c, "play");
}

int32_t fmrb_bnd_audio_stop(int32_t audio)
{
    (void)audio;
    return send_bare("stop");
}

int32_t fmrb_bnd_audio_pause(int32_t audio)
{
    (void)audio;
    return send_bare("pause");
}

int32_t fmrb_bnd_audio_resume(int32_t audio)
{
    (void)audio;
    return send_bare("resume");
}

// Asked of the audio side directly (it does not queue behind the kernel):
// 0 (nil) when it did not answer, an older graphics-audio firmware.
int32_t fmrb_bnd_audio_nsf_status(int32_t audio, fmrb_bnd_audio_nsf_status_t *out)
{
    (void)audio;
    fmrb_app_nsf_status_t st;
    if (fmrb_app_audio_nsf_status(&st, 500) != FMRB_OK) {
        return 0;
    }
    out->state = (uint8_t)st.state;
    out->reason = (uint8_t)st.reason;
    out->track = (uint8_t)st.track;
    out->laps = (uint8_t)st.laps;
    out->gen = (uint16_t)st.gen;
    out->period = (uint16_t)st.period_frames;
    out->frames = (uint32_t)st.frames;
    return 1;
}

// Inline, so a short tune only: one message holds FMRB_MAX_MSG_PAYLOAD_SIZE
// bytes. A bigger one goes by file (load_fmsq_file).
int32_t fmrb_bnd_audio_load_fmsq(int32_t audio, int32_t slot_id, const uint8_t *binary_data, int32_t binary_data_len)
{
    (void)audio;
    if (!binary_data || binary_data_len < 0) {
        return ERR_ARG;
    }
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, "load_fmsq", 3)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "slot");
    put_int(&c.out, slot_id);
    put_key(&c.out, "data");
    put_str(&c.out, (const char *)binary_data, (size_t)binary_data_len);
    return cmd_send(&c, "load_fmsq");
}

int32_t fmrb_bnd_audio_load_fmsq_file(int32_t audio, int32_t slot_id, const char *path, int32_t path_len)
{
    (void)audio;
    if (!path || path_len < 0) {
        return ERR_ARG;
    }
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, "load_fmsq_file", 3)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "slot");
    put_int(&c.out, slot_id);
    put_key(&c.out, "path");
    put_str(&c.out, path, (size_t)path_len);
    return cmd_send(&c, "load_fmsq_file");
}

int32_t fmrb_bnd_audio_play_slot_raw(int32_t audio, int32_t slot_id, int32_t instance)
{
    (void)audio;
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, "play_slot", 3)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "slot");
    put_int(&c.out, slot_id);
    put_key(&c.out, "instance");
    put_int(&c.out, instance);
    return cmd_send(&c, "play_slot");
}

// The MAIN instance's own volume, 0-100 (%): out of range is clamped.
int32_t fmrb_bnd_audio_set_main_volume(int32_t audio, int32_t percent)
{
    (void)audio;
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    fmrb_bnd_cmd_t c;
    if (!cmd_begin(&c, "main_volume", 2)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "volume");
    put_int(&c.out, percent);
    return cmd_send(&c, "main_volume");
}

// Modern only: the audio side reads the file. On the devices that is the one
// filesystem; the simulator's audio lives in another process with its own
// storage, so the file is synced into /cache/audio there first (the source
// tree mirrored, so two files of one name in different directories do not
// share an entry). Retro's audio is on the WROVER at the far end of a serial
// link, too slow and too small for clips: false, with nothing sent.
#define WAV_CACHE_DIR "/cache/audio"
#define WAV_PATH_MAX 120

int32_t fmrb_bnd_audio_play_wav(int32_t audio, const char *path, int32_t path_len)
{
    (void)audio;
    if (!path || path_len < 0) {
        return ERR_ARG;
    }
#if !defined(FMRB_HW_FAMILY_MODERN)
    FMRB_LOGI(TAG, "play_wav: not supported on this machine (%.*s)", (int)path_len, path);
    return 0;
#else
    char src[256];
    if ((size_t)path_len >= sizeof(src)) {
        return ERR_ARG;
    }
    memcpy(src, path, (size_t)path_len);
    src[path_len] = '\0';
#ifdef CONFIG_IDF_TARGET_LINUX
    char dest[WAV_PATH_MAX];
    int n = snprintf(dest, sizeof(dest), src[0] == '/' ? "%s%s" : "%s/%s", WAV_CACHE_DIR, src);
    if (n < 0 || (size_t)n >= sizeof(dest)) {
        return ERR_ARG;
    }
    if (fmrb_kernel_sync_file(src, dest) != FMRB_OK) {
        FMRB_LOGW(TAG, "play_wav: cannot put %s where the audio side can read it", src);
        return 0;
    }
    int32_t ret = send_path("play_wav", dest, (int32_t)strlen(dest));
#else
    int32_t ret = send_path("play_wav", src, path_len);
#endif
    return ret < 0 ? ret : 1;
#endif
}

int32_t fmrb_bnd_audio_stop_wav(int32_t audio)
{
    (void)audio;
#if !defined(FMRB_HW_FAMILY_MODERN)
    return 0;
#else
    int32_t ret = send_bare("stop_wav");
    return ret < 0 ? ret : 1;
#endif
}

// The two sent in a stream (a MIDI song sends one every few milliseconds):
// built by the shared builder, nothing allocated.
int32_t fmrb_bnd_audio_note_on_raw(int32_t audio, int32_t channel, int32_t freq, int32_t volume, int32_t duty, int32_t sweep)
{
    (void)audio;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return ERR_FAILED;
    }
    if (fmrb_app_send_audio_note(ctx->app_id, true, channel, freq, volume, duty, sweep, 1000) != FMRB_OK) {
        FMRB_LOGE(TAG, "App %s failed to send audio note", ctx->app_name);
    }
    return 0;
}

int32_t fmrb_bnd_audio_note_off(int32_t audio, int32_t channel)
{
    (void)audio;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return ERR_FAILED;
    }
    if (fmrb_app_send_audio_note(ctx->app_id, false, channel, 0, 0, 0, 0, 1000) != FMRB_OK) {
        FMRB_LOGE(TAG, "App %s failed to send audio note", ctx->app_name);
    }
    return 0;
}
