/*
 * sys_linux_mixer.c - the machine's volume as the ALSA mixer has it
 * (sys_linux.h). The user's decision (2026-10-26): the desktop's volume and
 * mute move the speaker's own volume, the OS's, so other programs and the
 * OS's own controls see the same value. Family mruby's internal volume stays
 * at full while a mixer is in charge (fmrb_kernel.c, host_task.c).
 *
 * A simple-mixer element with a playback volume: the one audio.mixer names,
 * or for "auto" the first of the usual names, else the first element that
 * has a playback volume at all. Mute is the element's playback switch when
 * it has one, else the volume goes to its minimum and comes back.
 */

#include <stdio.h>
#include <string.h>

#include "sys_linux.h"
#include "sys_linux_read.h"

#if NATIVE_HAVE_ALSA
#include <alsa/asoundlib.h>

static snd_mixer_t *s_mixer;
static snd_mixer_elem_t *s_elem;
static long s_min, s_max;
static bool s_has_switch;
static long s_saved;          /* the volume a mute without a switch put away */
static bool s_soft_muted;
static char s_name[96] = "none";

static const char *const AUTO_NAMES[] = {
    "Master", "PCM", "Speaker", "Headphone", "Digital", "DAC", NULL,
};

/* The mixer's card for a PCM name: "hw:1,0" / "plughw:1" -> "hw:1";
 * anything else (default, a plugin) -> "default". */
static void card_of(const char *device, char *out, size_t cap)
{
    const char *p = NULL;
    if (strncmp(device, "hw:", 3) == 0) p = device + 3;
    else if (strncmp(device, "plughw:", 7) == 0) p = device + 7;
    if (p == NULL) {
        snprintf(out, cap, "default");
        return;
    }
    size_t n = strcspn(p, ",");
    snprintf(out, cap, "hw:%.*s", (int)n, p);
}

static snd_mixer_elem_t *find_elem(const char *name)
{
    for (snd_mixer_elem_t *e = snd_mixer_first_elem(s_mixer); e; e = snd_mixer_elem_next(e)) {
        if (!snd_mixer_selem_is_active(e) || !snd_mixer_selem_has_playback_volume(e)) continue;
        if (name == NULL || strcmp(snd_mixer_selem_get_name(e), name) == 0) return e;
    }
    return NULL;
}

bool sys_linux_mixer_open(const char *device, const char *element, char *why, size_t cap)
{
    sys_linux_mixer_close();
    if (strcmp(element, "none") == 0) {
        snprintf(why, cap, "audio.mixer = \"none\"");
        return false;
    }
    char card[64];
    card_of(device, card, sizeof(card));
    int rc = snd_mixer_open(&s_mixer, 0);
    if (rc == 0) rc = snd_mixer_attach(s_mixer, card);
    if (rc == 0) rc = snd_mixer_selem_register(s_mixer, NULL, NULL);
    if (rc == 0) rc = snd_mixer_load(s_mixer);
    if (rc < 0) {
        snprintf(why, cap, "mixer %s: %s", card, snd_strerror(rc));
        sys_linux_mixer_close();
        return false;
    }
    if (strcmp(element, "auto") == 0) {
        for (int i = 0; AUTO_NAMES[i] && s_elem == NULL; i++) s_elem = find_elem(AUTO_NAMES[i]);
        if (s_elem == NULL) s_elem = find_elem(NULL);
    } else {
        s_elem = find_elem(element);
    }
    if (s_elem == NULL) {
        snprintf(why, cap, "mixer %s: no element %s with a playback volume", card,
                 strcmp(element, "auto") == 0 ? "(auto)" : element);
        sys_linux_mixer_close();
        return false;
    }
    snd_mixer_selem_get_playback_volume_range(s_elem, &s_min, &s_max);
    if (s_max <= s_min) {
        snprintf(why, cap, "mixer %s: %s has no volume range", card, snd_mixer_selem_get_name(s_elem));
        sys_linux_mixer_close();
        return false;
    }
    s_has_switch = snd_mixer_selem_has_playback_switch(s_elem);
    snprintf(s_name, sizeof(s_name), "%s on %s", snd_mixer_selem_get_name(s_elem), card);
    return true;
}

void sys_linux_mixer_close(void)
{
    if (s_mixer) snd_mixer_close(s_mixer);
    s_mixer = NULL;
    s_elem = NULL;
    s_soft_muted = false;
    snprintf(s_name, sizeof(s_name), "none");
}

const char *sys_linux_mixer_name(void)
{
    return s_name;
}

/* Another program (the OS's own controls) may have moved it. */
static void refresh(void)
{
    snd_mixer_handle_events(s_mixer);
}

bool sys_linux_mixer_get(int *step, bool *muted, int max_step)
{
    if (s_elem == NULL) return false;
    refresh();
    long v = s_min;
    snd_mixer_selem_get_playback_volume(s_elem, SND_MIXER_SCHN_FRONT_LEFT, &v);
    bool off = false;
    if (s_has_switch) {
        int sw = 1;
        snd_mixer_selem_get_playback_switch(s_elem, SND_MIXER_SCHN_FRONT_LEFT, &sw);
        off = (sw == 0);
    } else if (s_soft_muted) {
        off = true;
        v = s_saved;
    }
    if (step) *step = sys_linux_mixer_to_step(v, s_min, s_max, max_step);
    if (muted) *muted = off;
    return true;
}

bool sys_linux_mixer_set(int step, int max_step)
{
    if (s_elem == NULL) return false;
    long v = sys_linux_step_to_mixer(step, s_min, s_max, max_step);
    if (!s_has_switch && s_soft_muted) {
        s_saved = v;     /* applied when the mute comes off */
        return true;
    }
    snd_mixer_selem_set_playback_volume_all(s_elem, v);
    return true;
}

bool sys_linux_mixer_mute(bool on)
{
    if (s_elem == NULL) return false;
    if (s_has_switch) {
        snd_mixer_selem_set_playback_switch_all(s_elem, on ? 0 : 1);
        return true;
    }
    if (on && !s_soft_muted) {
        snd_mixer_selem_get_playback_volume(s_elem, SND_MIXER_SCHN_FRONT_LEFT, &s_saved);
        snd_mixer_selem_set_playback_volume_all(s_elem, s_min);
        s_soft_muted = true;
    } else if (!on && s_soft_muted) {
        snd_mixer_selem_set_playback_volume_all(s_elem, s_saved);
        s_soft_muted = false;
    }
    return true;
}

#else  /* a build without ALSA: no mixer, the internal volume */

bool sys_linux_mixer_open(const char *device, const char *element, char *why, size_t cap)
{
    (void)device;
    (void)element;
    snprintf(why, cap, "this build has no ALSA");
    return false;
}

void sys_linux_mixer_close(void) {}
const char *sys_linux_mixer_name(void) { return "none"; }
bool sys_linux_mixer_get(int *step, bool *muted, int max_step) { (void)step; (void)muted; (void)max_step; return false; }
bool sys_linux_mixer_set(int step, int max_step) { (void)step; (void)max_step; return false; }
bool sys_linux_mixer_mute(bool on) { (void)on; return false; }

#endif
