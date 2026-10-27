/*
 * native_backends.c - see native_backends.h. Main thread, before the
 * machine starts.
 */

#include "native_backends.h"

#include <stdio.h>
#include <string.h>

#include "input_evdev.h"
#include "native_input.h"
#if NATIVE_HAVE_WLROOTS
#include "wl_bridge.h"
#endif

static const native_output_t *const s_outputs[] = {
    &native_output_sdl,
    &native_output_fbdev,
#if NATIVE_HAVE_DRM
    &native_output_drm,
#endif
#if NATIVE_HAVE_WLROOTS
    &native_output_wlroots,
#endif
    NULL,
};

static const native_audio_sink_t *const s_sinks[] = {
    &native_audio_sdl,
#if NATIVE_HAVE_ALSA
    &native_audio_alsa,
#endif
    &native_audio_none,
    NULL,
};

#define WLROOTS_MISSING "this build has no wlroots compositor (built without wlroots 0.19; " \
                        "rake native:wl builds it in the Ubuntu 26.04 image)"

/* Names a definition may use that some build has. */
static const char *later(const char *kind, const char *name)
{
    if (strcmp(kind, "display") == 0) {
        if (strcmp(name, "drm") == 0) return "this build has no DRM output (built without libdrm; install its development files and rebuild)";
        if (strcmp(name, "wlroots") == 0) return WLROOTS_MISSING;
    } else if (strcmp(kind, "audio") == 0) {
        if (strcmp(name, "alsa") == 0) return "this build has no ALSA output (built without libasound; install its development files and rebuild)";
    } else if (strcmp(kind, "input") == 0) {
        if (strcmp(name, "wlroots") == 0) return WLROOTS_MISSING;
    }
    return NULL;
}

const char *native_backends_outputs_built(void)
{
#if NATIVE_HAVE_DRM && NATIVE_HAVE_WLROOTS
    return "sdl, fbdev, drm, wlroots";
#elif NATIVE_HAVE_DRM
    return "sdl, fbdev, drm";
#elif NATIVE_HAVE_WLROOTS
    return "sdl, fbdev, wlroots";
#else
    return "sdl, fbdev";
#endif
}

const char *native_backends_inputs_built(void)
{
#if NATIVE_HAVE_WLROOTS
    return "sdl, evdev, wlroots";
#else
    return "sdl, evdev";
#endif
}

const char *native_backends_audio_built(void)
{
#if NATIVE_HAVE_ALSA
    return "sdl, alsa, none";
#else
    return "sdl, none";
#endif
}

bool native_backends_select(const native_machine_t *m, native_backends_t *out,
                            char *why, size_t why_len)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; s_outputs[i] != NULL; i++) {
        if (strcmp(s_outputs[i]->name, m->display.backend) == 0) out->output = s_outputs[i];
    }
    if (out->output == NULL) {
        const char *l = later("display", m->display.backend);
        snprintf(why, why_len, "display.backend \"%s\": %s", m->display.backend,
                 l ? l : "no such output");
        if (l == NULL) {
            size_t n = strlen(why);
            snprintf(why + n, why_len - n, " (this build has: %s)", native_backends_outputs_built());
        }
        return false;
    }
    for (int i = 0; s_sinks[i] != NULL; i++) {
        if (strcmp(s_sinks[i]->name, m->audio.backend) == 0) out->audio = s_sinks[i];
    }
    if (out->audio == NULL) {
        const char *l = later("audio", m->audio.backend);
        snprintf(why, why_len, "audio.backend \"%s\": %s", m->audio.backend, l ? l : "no such output");
        if (l == NULL) {
            size_t n = strlen(why);
            snprintf(why + n, why_len - n, " (this build has: %s)", native_backends_audio_built());
        }
        return false;
    }
    bool wl_out = strcmp(out->output->name, "wlroots") == 0;
    if (strcmp(m->input.backend, "sdl") == 0) {
        if (out->output != &native_output_sdl) {
            snprintf(why, why_len, "input.backend sdl reads the SDL window, and display.backend is %s "
                                   "(use evdev)", m->display.backend);
            return false;
        }
    } else if (strcmp(m->input.backend, "wlroots") == 0 && NATIVE_HAVE_WLROOTS) {
        if (!wl_out) {
            snprintf(why, why_len, "input.backend wlroots is the compositor's seat, and display.backend is %s "
                                   "(use wlroots for both)", m->display.backend);
            return false;
        }
    } else if (strcmp(m->input.backend, "evdev") != 0) {
        const char *l = later("input", m->input.backend);
        snprintf(why, why_len, "input.backend \"%s\": %s", m->input.backend,
                 l ? l : "no such input");
        if (l == NULL) {
            size_t n = strlen(why);
            snprintf(why + n, why_len - n, " (this build has: %s)", native_backends_inputs_built());
        }
        return false;
    }
    if (wl_out && strcmp(m->input.backend, "wlroots") != 0) {
        snprintf(why, why_len, "display.backend wlroots takes its input from its own seat "
                               "(input.backend must be wlroots, not %s)", m->input.backend);
        return false;
    }
    return true;
}

/* The machine's sources (input_source.h), NULL-terminated. */
static const fmrb_input_source_t *s_sources[5];
const fmrb_input_source_t *const *fmrb_input_sources = s_sources;

bool native_backends_open_input(const native_machine_t *m, char *why, size_t why_len)
{
    int n = 0;
    if (strcmp(m->input.backend, "evdev") == 0) {
        if (!native_evdev_open(m, why, why_len)) return false;
        s_sources[n++] = &fmrb_input_source_evdev;
    } else {
        /* The SDL window's ring; the compositor (wlroots) fills the same
         * one from the same main thread. */
        s_sources[n++] = &fmrb_input_source_sdl;
    }
#if NATIVE_HAVE_WLROOTS
    /* The compositor's Linux windows as Family mruby windows (L9a). */
    if (strcmp(m->input.backend, "wlroots") == 0) s_sources[n++] = &fmrb_input_source_wl_bridge;
#endif
    /* The headless tools' way in, on every machine. */
    s_sources[n++] = &fmrb_input_source_inject;
    s_sources[n] = NULL;
    return true;
}
