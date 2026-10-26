/*
 * host_sdl.c - see host_sdl.h. Main thread only; never calls into FreeRTOS
 * or the fmrb_mem pools (this thread is not a task).
 *
 * SDL's own scancodes are the USB HID usage IDs of the keyboard page for
 * every key the firmware's tables know (letters at 4, Enter 40, F1 58,
 * International1 135 ...), so a key goes through unchanged, as the
 * simulation's sdl2-display passes it. Unlike that path, the modifier byte is
 * built here from SDL's full mask, so Alt reaches the machine.
 */

#include "host_sdl.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fmrb_keymap.h"
#include "fmrb_present.h"
#include "native_audio.h"
#include "native_config.h"
#include "native_ctl.h"
#include "native_frame.h"
#include "native_input.h"

/* Largest frame the loop copies out (any size the display task can make). */
#define FRAME_CAP_PX (1920 * 1080)
/* How often pointer motion is passed on: the machine redraws its cursor per
 * event, and a mouse reports at hundreds of Hz. */
#define MOTION_INTERVAL_MS 16

static SDL_Window *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture *s_texture = NULL;
static int s_out_w = 0, s_out_h = 0;          /* texture (output surface) size */
static uint16_t *s_frame = NULL;              /* last frame copied out */
static int s_fb_w = 0, s_fb_h = 0;
static uint32_t s_shown_seq = 0;
static int s_dirty = 1;
static fmrb_present_place_t s_place;
static SDL_AudioDeviceID s_audio = 0;

/* Pending pointer motion, coalesced. */
static int s_motion_pending = 0;
static int s_motion_x = 0, s_motion_y = 0;
static uint32_t s_motion_last_ms = 0;

static void audio_callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    native_audio_read((int16_t *)stream, len / (int)sizeof(int16_t));
}

static void open_audio(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "native: no audio (%s); running silent\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = NATIVE_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    /* No allowed changes: SDL converts the APU's rate to the device's. */
    s_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_audio == 0) {
        fprintf(stderr, "native: cannot open audio (%s); running silent\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(s_audio, 0);
    fprintf(stderr, "native: audio on '%s' (%d Hz from %d Hz)\n",
            SDL_GetCurrentAudioDriver(), have.freq, NATIVE_AUDIO_RATE);
}

int host_sdl_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "native: SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    open_audio();
    s_frame = (uint16_t *)malloc((size_t)FRAME_CAP_PX * sizeof(uint16_t));
    return s_frame != NULL;
}

static int ensure_texture(int w, int h)
{
    if (s_texture != NULL && w == s_out_w && h == s_out_h) return 1;
    if (s_texture != NULL) SDL_DestroyTexture(s_texture);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING, w, h);
    if (s_texture == NULL) {
        fprintf(stderr, "native: texture %dx%d: %s\n", w, h, SDL_GetError());
        return 0;
    }
    s_out_w = w;
    s_out_h = h;
    s_dirty = 1;
    return 1;
}

/* The window, once the display task has said what its base frame is. */
static void ensure_window(void)
{
    if (s_window != NULL) return;
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    if (bw <= 0 || bh <= 0) return;
    int w = 0, h = 0;
    fmrb_present_surface_size(bw, bh, g_native_config.scale, &w, &h);
    s_window = SDL_CreateWindow("Family mruby", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, w, h,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (s_window == NULL) {
        fprintf(stderr, "native: window: %s\n", SDL_GetError());
        return;
    }
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (s_renderer == NULL) {
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (s_renderer == NULL) {
        fprintf(stderr, "native: renderer: %s\n", SDL_GetError());
        return;
    }
    /* The machine draws its own cursor. */
    SDL_ShowCursor(SDL_DISABLE);
    fprintf(stderr, "native: window %dx%d (frame %dx%d at %dx), video '%s'\n",
            w, h, bw, bh, g_native_config.scale, SDL_GetCurrentVideoDriver());
}

static void present(void)
{
    if (s_renderer == NULL) return;
    uint32_t seq = native_frame_seq();
    if (seq != s_shown_seq) {
        int w = 0, h = 0;
        uint32_t got = 0;
        if (native_frame_copy(s_frame, FRAME_CAP_PX, &w, &h, &got)) {
            s_fb_w = w;
            s_fb_h = h;
            s_shown_seq = got;
            s_dirty = 1;
        }
    }
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(s_renderer, &ow, &oh);
    if (ow <= 0 || oh <= 0 || s_fb_w <= 0) return;
    if (!ensure_texture(ow, oh)) return;
    if (!s_dirty) return;
    s_dirty = 0;

    fmrb_present_place(s_out_w, s_out_h, s_fb_w, s_fb_h, &s_place);
    void *pixels = NULL;
    int pitch = 0;
    if (SDL_LockTexture(s_texture, NULL, &pixels, &pitch) != 0) return;
    fmrb_present_blit_rgb565(s_frame, s_fb_w, s_fb_h, (uint16_t *)pixels,
                             s_out_w, s_out_h, pitch / (int)sizeof(uint16_t), &s_place);
    SDL_UnlockTexture(s_texture);
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

/* Window coordinates (what SDL's mouse events carry) to frame coordinates. */
static int to_frame(int wx, int wy, int *fx, int *fy)
{
    if (s_window == NULL || s_fb_w <= 0) return 0;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(s_window, &ww, &wh);
    int sx = (ww > 0) ? (int)((long)wx * s_out_w / ww) : wx;
    int sy = (wh > 0) ? (int)((long)wy * s_out_h / wh) : wy;
    fmrb_present_to_frame(&s_place, s_fb_w, s_fb_h, sx, sy, fx, fy);
    return 1;
}

static void flush_motion(int force)
{
    if (!s_motion_pending) return;
    uint32_t now = SDL_GetTicks();
    if (!force && now - s_motion_last_ms < MOTION_INTERVAL_MS) return;
    native_input_push(NATIVE_INPUT_MOUSE_MOVE, (uint32_t)s_motion_x, (uint32_t)s_motion_y, 0, 0);
    s_motion_pending = 0;
    s_motion_last_ms = now;
}

static uint32_t sdl_mod_to_fmrb(SDL_Keymod mod)
{
    uint32_t f = 0;
    if (mod & KMOD_LSHIFT) f |= FMRB_KEYMAP_MOD_LSHIFT;
    if (mod & KMOD_RSHIFT) f |= FMRB_KEYMAP_MOD_RSHIFT;
    if (mod & KMOD_LCTRL)  f |= FMRB_KEYMAP_MOD_LCTRL;
    if (mod & KMOD_RCTRL)  f |= FMRB_KEYMAP_MOD_RCTRL;
    if (mod & KMOD_LALT)   f |= FMRB_KEYMAP_MOD_LALT;
    if (mod & KMOD_RALT)   f |= FMRB_KEYMAP_MOD_RALT;
    return f;
}

/* The JIS mode keys (half/full-width, katakana) arrive under X11 as a press
 * and then an endless auto-repeat, with the release only when the window
 * loses focus; sdl2-display drops them for the simulation for the same
 * reason (its comment has the logs). Ctrl+Space and the mode indicator still
 * switch kana input, and the inject socket still carries the keys. */
static int is_jis_mode_key(SDL_Scancode sc)
{
    return sc == SDL_SCANCODE_GRAVE || sc == SDL_SCANCODE_INTERNATIONAL2;
}

static int handle_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        return 1;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_EXPOSED ||
            e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            s_dirty = 1;
        }
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        SDL_Scancode sc = e->key.keysym.scancode;
        if (e->type == SDL_KEYDOWN && e->key.repeat) break;
        if (is_jis_mode_key(sc) && SDL_GetCurrentVideoDriver() &&
            strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
            break;
        }
        if (sc <= 0 || sc > SDL_SCANCODE_RGUI) break;   /* outside the HID keyboard page */
        flush_motion(1);
        native_input_push(e->type == SDL_KEYDOWN ? NATIVE_INPUT_KEY_DOWN : NATIVE_INPUT_KEY_UP,
                          (uint32_t)sc, sdl_mod_to_fmrb(e->key.keysym.mod), 0, 0);
        break;
    }
    case SDL_MOUSEMOTION: {
        int fx, fy;
        if (to_frame(e->motion.x, e->motion.y, &fx, &fy)) {
            s_motion_x = fx;
            s_motion_y = fy;
            s_motion_pending = 1;
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        int fx, fy;
        if (to_frame(e->button.x, e->button.y, &fx, &fy)) {
            s_motion_x = fx;
            s_motion_y = fy;
            s_motion_pending = 1;
            flush_motion(1);
            native_input_push(NATIVE_INPUT_MOUSE_BUTTON, (uint32_t)fx, (uint32_t)fy,
                              e->button.button, e->type == SDL_MOUSEBUTTONDOWN ? 1 : 0);
        }
        break;
    }
    case SDL_MOUSEWHEEL: {
        /* Notches, positive away from the user, which is the sign the rest of
         * the system uses; a flipped (natural scrolling) device is put back. */
        int delta = e->wheel.y;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
        if (delta == 0) break;
        if (delta > 127) delta = 127;
        if (delta < -127) delta = -127;
        int mx = 0, my = 0, fx = 0, fy = 0;
        SDL_GetMouseState(&mx, &my);
        if (to_frame(mx, my, &fx, &fy)) {
            flush_motion(1);
            native_input_push(NATIVE_INPUT_MOUSE_WHEEL, (uint32_t)fx, (uint32_t)fy,
                              (uint32_t)(int32_t)delta, 0);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

int host_sdl_run(void)
{
    int quit = 0;
    while (!quit) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 5)) {
            quit |= handle_event(&e);
            while (!quit && SDL_PollEvent(&e)) {
                quit |= handle_event(&e);
            }
        }
        flush_motion(0);
        ensure_window();
        present();
        if (native_ctl_poll()) quit = 1;
    }
    native_ctl_close();
    if (s_audio != 0) SDL_CloseAudioDevice(s_audio);
    return 0;
}
