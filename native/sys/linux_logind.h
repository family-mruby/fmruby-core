/*
 * linux_logind.h - logind's signals the compositor locks the screen on
 * (doc/linux_native/, L13): the session's Lock (loginctl lock-session, a
 * lid closed when logind is set to lock) and the manager's
 * PrepareForSleep(true) (just before a suspend). Before a suspend the lock
 * is given a moment: a "delay" sleep inhibitor is held, and let go once the
 * screen is locked (or after a few seconds), so the screen is never seen
 * unlocked on waking.
 *
 * A thread of its own on the system bus (sd-bus, libsystemd opened at run
 * time, as the power entries do); no FreeRTOS, no wlroots. The lock itself
 * is asked through comp_link (COMP_CMD_LOCK), which wakes the compositor.
 * DBUS_SYSTEM_BUS_ADDRESS moves the bus (the tests' own logind stand-in).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start watching (once); false (and one line) when there is no bus or no
 * libsystemd. */
bool linux_logind_watch_start(void);
void linux_logind_watch_stop(void);

#ifdef __cplusplus
}
#endif
