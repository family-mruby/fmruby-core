/*
 * linux_notify.h - the small notices the desktop shows at the top right
 * (doc/linux_native, L14), and the freedesktop notification service that
 * feeds them from Linux programs.
 *
 *   the store   up to LINUX_NOTIFY_MAX notices on screen at once, each with
 *               its end time; the oldest goes when a fifth comes. Kinds:
 *               "linux" (a Linux program, through the service), "app" (a
 *               Family mruby app, FmrbApp.notify), "volume" (the volume
 *               keys: summary is the level in percent, body "muted" or ""),
 *               "restarted" (the session started Family mruby again after
 *               it had ended abnormally: body is how it ended). No history:
 *               a notice that ended is gone.
 *   the service org.freedesktop.Notifications on the session bus, a thread
 *               of its own (sd-bus, linux_bus.h): Notify, CloseNotification,
 *               GetCapabilities ("body"), GetServerInformation, and the
 *               NotificationClosed signal (1 expired, 2 dismissed, 3 closed
 *               by a call). Taken only when nobody has the name (a desktop
 *               the program runs inside keeps its own; the notices of the
 *               Family mruby apps still show here).
 *
 * The desktop reads the store through fmrb_platform_sys (notice_gen,
 * notice_list); a FreeRTOS task and the service's thread share it under a
 * lock held only to copy. Plain POSIX: the host tests run the store as it
 * is (the *_at calls take the time).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINUX_NOTIFY_MAX 4

/* How long a notice stays, in ms: timeout_ms as the freedesktop rules give
 * it (-1 the server's choice, 0 never) made a few seconds. */
int linux_notify_duration(int timeout_ms);

/* Add one (or replace the one with id replaces, when it is still there).
 * Returns its id (never 0). Fields are cut to fit; tabs become spaces,
 * line breaks in the body are kept. */
uint32_t linux_notify_post(const char *kind, const char *app, const char *summary, const char *body,
                           int timeout_ms, uint32_t replaces);
uint32_t linux_notify_post_at(const char *kind, const char *app, const char *summary, const char *body,
                              int timeout_ms, uint32_t replaces, uint64_t now_ms);

/* Changes whenever the notices on screen change (ending ones end here). */
uint32_t linux_notify_gen(void);
uint32_t linux_notify_gen_at(uint64_t now_ms);

/* The notices on screen, newest first, as text: one line each,
 * "id\tkind\tapp\tsummary\tbody", a line break in the body written as
 * 0x1F. Returns the length (more than cap: nothing written). */
int32_t linux_notify_list(char *buf, size_t cap);

/* The user closed it (a click). false: no such notice. */
bool linux_notify_dismiss(uint32_t id);
/* A program closed it (CloseNotification). */
bool linux_notify_close(uint32_t id);

/* The NotificationClosed signals the service still has to send: one
 * (id, reason) per call, false when none. */
bool linux_notify_take_closed(uint32_t *id, uint32_t *reason);

/* Forget everything (the tests). */
void linux_notify_reset(void);

/* Start the service on the session bus (once). false and one line when
 * there is no session bus or the name is taken. */
bool linux_notify_serve(void);
/* Whether the service has the name. */
bool linux_notify_serving(void);

#ifdef __cplusplus
}
#endif
