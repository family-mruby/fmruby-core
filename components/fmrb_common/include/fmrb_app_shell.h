/**
 * @file fmrb_app_shell.h
 * @brief The app shell: the window, its frame and the app's life, run in C
 *        for an engine that only exports functions (doc/app_boundary W3).
 *
 * An app on the shell does not own a loop. It exports on_create, on_update
 * (returning the ms until the next call), on_event and, if it wants them,
 * on_destroy, on_suspend, on_resume, on_resize and on_quit_request. The shell
 * makes the window and its canvas, draws the frame, answers the frame's own
 * events (the close button, the window menu), works out the area the app owns
 * and its drawing origin, waits for messages and parses them, keeps the
 * update timer, suspends and resumes, and ends the app. It is the same life
 * the Ruby base class FmrbApp gives a Ruby app, and draws the same frame.
 *
 * The shell knows no engine. An engine hands it a table of calls
 * (fmrb_shell_engine_t); every call runs app code and returns 0 when it went
 * through, FMRB_SHELL_CLOSED when the app was unwound because it was asked to
 * end, and FMRB_SHELL_RAISED when the app raised (the engine keeps the text).
 *
 * The shell's state lives on the app task's stack for as long as
 * fmrb_app_shell_run runs; ctx->shell points at it, which is how an engine's
 * binding reaches the shell on behalf of the app (FmrbApp.stop and the like).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"

typedef struct fmrb_app_task_context_s fmrb_app_task_context_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Results of an engine call. */
#define FMRB_SHELL_OK      0
#define FMRB_SHELL_CLOSED  1   /**< unwound because the app was asked to end */
#define FMRB_SHELL_RAISED  (-1)

/** The update interval when the app has no on_update or returns no number
 *  (the same default as FmrbApp#on_update). */
#define FMRB_SHELL_DEFAULT_UPDATE_MS 330

/** The functions an app may export. */
typedef enum {
    FMRB_SHELL_HOOK_LOAD = 0,      /**< run the program's top level once */
    FMRB_SHELL_HOOK_CREATE,
    FMRB_SHELL_HOOK_UPDATE,
    FMRB_SHELL_HOOK_EVENT,
    FMRB_SHELL_HOOK_DESTROY,
    FMRB_SHELL_HOOK_SUSPEND,
    FMRB_SHELL_HOOK_RESUME,
    FMRB_SHELL_HOOK_RESIZE,
    FMRB_SHELL_HOOK_QUIT_REQUEST,
    FMRB_SHELL_HOOK_COUNT
} fmrb_shell_hook_t;

/**
 * @brief One input event, as on_event receives it.
 *
 * kind is the fmrb_hid_event_type_t of fmrb_hid_decode.h. Only the fields of
 * that kind mean anything. x and y are in the area the app owns; wx and wy in
 * the window (the same as ev[:x] / ev[:wx] of a Ruby app).
 */
typedef struct {
    int kind;
    int x, y, wx, wy;
    int button;
    int keycode, scancode, modifier, character;
    int delta;
    int gamepad_id, axis, value;
    int mode;
} fmrb_shell_event_t;

/**
 * @brief Where the app draws: its canvas, the window, and the area it owns.
 *
 * canvas is -1 for a headless app. area_x / area_y is the drawing origin in
 * the canvas: the app's (0, 0).
 */
typedef struct {
    int      canvas;
    uint16_t window_w, window_h;
    int16_t  area_x, area_y;
    uint16_t area_w, area_h;
    bool     fullscreen;
} fmrb_shell_layout_t;

/** What the shell asks of an engine. ud is the engine's own. */
typedef struct {
    /** Whether the app exports this function. */
    bool (*has_hook)(void *ud, fmrb_shell_hook_t hook);
    /** A hook without arguments or result (LOAD, CREATE, DESTROY, SUSPEND,
     *  RESUME, QUIT_REQUEST). */
    int  (*call)(void *ud, fmrb_shell_hook_t hook);
    /** on_update; *next_ms gets its result, or -1 when it was not a number. */
    int  (*call_update)(void *ud, int *next_ms);
    int  (*call_event)(void *ud, const fmrb_shell_event_t *ev);
    int  (*call_resize)(void *ud, int w, int h);
    /** The layout changed (before on_create, on a resize). No app code. */
    void (*layout)(void *ud, const fmrb_shell_layout_t *layout);
} fmrb_shell_engine_t;

/**
 * @brief Run an app on the shell, from its top level to its end.
 *
 * @param why On FMRB_ERR_FAILED, the reason when the shell itself refused to
 *            start the app (it exports none of on_create, on_update,
 *            on_event); empty when the app raised (the engine has the text).
 * @return FMRB_OK when the app ended as asked (closed, stopped, killed),
 *         FMRB_ERR_FAILED when it raised or could not start.
 */
fmrb_err_t fmrb_app_shell_run(fmrb_app_task_context_t *ctx,
                              const fmrb_shell_engine_t *engine, void *ud,
                              char *why, size_t why_cap);

/* ---- for an engine's binding, on behalf of the app ---------------------- */

/** Whether the app runs on the shell; *out (when not NULL) gets the layout
 *  as the shell gave it last (FmrbApp.window_width and the rest). */
bool fmrb_app_shell_layout(fmrb_app_task_context_t *ctx, fmrb_shell_layout_t *out);
/** End the app after the current call (FmrbApp.stop). */
void fmrb_app_shell_stop(fmrb_app_task_context_t *ctx);
/** Draw the frame again (FmrbApp.draw_window_frame). */
void fmrb_app_shell_draw_frame(fmrb_app_task_context_t *ctx);
/** Fill the area the app owns and draw the frame again
 *  (FmrbApp.clear_user_area). color < 0 takes the default: the theme's page
 *  colour, or the colour key for a frameless window. */
void fmrb_app_shell_clear_user_area(fmrb_app_task_context_t *ctx, int color);

/**
 * @brief Whether the engine should unwind the app now (its stop hook).
 *
 * A call that returns in time is never cut short: the shell reads the stop
 * between calls, so the input that arrives during a call stays queued for it.
 * Only a call that has run for FMRB_SHELL_LONG_CALL_MS or more is looked at,
 * and then the way the engines without a shell do it: the queue is drained
 * for a stop (the close button of a busy app sends only that) and the
 * context's should_exit (the kernel's kill) is read.
 */
#define FMRB_SHELL_LONG_CALL_MS 1000
bool fmrb_app_shell_unwind_now(fmrb_app_task_context_t *ctx);

#ifdef __cplusplus
}
#endif
