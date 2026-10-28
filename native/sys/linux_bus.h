/*
 * linux_bus.h - sd-bus (libsystemd's D-Bus), opened at run time, for the
 * PC's settings and notifications (doc/linux_native, L14): NetworkManager
 * (linux_net.c) and BlueZ (linux_bt.c) on the system bus, the
 * notifications we serve (linux_notify.c) on the session bus.
 *
 * libsystemd0 is on every systemd machine (the package depends on it) and
 * nothing of it is needed to build: the calls are looked up once, here,
 * with the types sd-bus documents. A message, a bus and a slot are opaque
 * pointers; an error is sd_bus_error's layout (lbus_error_t).
 *
 * Each user is a thread of its own with a bus connection of its own (sd-bus
 * connections are not shared between threads), outside FreeRTOS. It waits
 * in lbus_wait: the bus descriptor and a wake descriptor (an eventfd the
 * FreeRTOS side writes to when it queues a request), never spinning.
 *
 * DBUS_SYSTEM_BUS_ADDRESS and DBUS_SESSION_BUS_ADDRESS move the buses (the
 * checks' stand-ins, never the machine's services).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const char *message;
    int need_free;
} lbus_error_t;

#define LBUS_ERROR_NULL { NULL, NULL, 0 }

typedef int (*lbus_handler_t)(void *m, void *userdata, lbus_error_t *ret_error);

typedef struct {
    int (*open_system)(void **bus);
    int (*open_user)(void **bus);
    void *(*flush_close_unref)(void *bus);
    int (*get_fd)(void *bus);
    int (*get_events)(void *bus);
    int (*get_timeout)(void *bus, uint64_t *usec);
    int (*process)(void *bus, void **r);
    int (*flush)(void *bus);
    int (*request_name)(void *bus, const char *name, uint64_t flags);
    int (*add_object)(void *bus, void **slot, const char *path, lbus_handler_t cb, void *userdata);
    int (*add_fallback)(void *bus, void **slot, const char *prefix, lbus_handler_t cb, void *userdata);
    int (*match_signal)(void *bus, void **slot, const char *sender, const char *path, const char *interface,
                        const char *member, lbus_handler_t cb, void *userdata);
    int (*call_method)(void *bus, const char *destination, const char *path, const char *interface,
                       const char *member, lbus_error_t *ret_error, void **reply, const char *types, ...);
    int (*call)(void *bus, void *m, uint64_t usec, lbus_error_t *ret_error, void **reply);
    int (*send)(void *bus, void *m, uint64_t *cookie);
    int (*get_property)(void *bus, const char *destination, const char *path, const char *interface,
                        const char *member, lbus_error_t *ret_error, void **reply, const char *type);
    int (*set_property)(void *bus, const char *destination, const char *path, const char *interface,
                        const char *member, lbus_error_t *ret_error, const char *type, ...);
    int (*emit_signal)(void *bus, const char *path, const char *interface, const char *member,
                       const char *types, ...);
    int (*new_method_call)(void *bus, void **m, const char *destination, const char *path,
                           const char *interface, const char *member);
    int (*new_method_return)(void *call, void **m);
    int (*reply_method_return)(void *call, const char *types, ...);
    int (*reply_method_errorf)(void *call, const char *name, const char *format, ...);
    int (*message_append)(void *m, const char *types, ...);
    int (*message_append_basic)(void *m, char type, const void *p);
    int (*message_open_container)(void *m, char type, const char *contents);
    int (*message_close_container)(void *m);
    int (*message_read)(void *m, const char *types, ...);
    int (*message_read_basic)(void *m, char type, void *p);
    int (*message_read_array)(void *m, char type, const void **ptr, size_t *size);
    int (*message_enter_container)(void *m, char type, const char *contents);
    int (*message_exit_container)(void *m);
    int (*message_skip)(void *m, const char *types);
    int (*message_peek_type)(void *m, char *type, const char **contents);
    int (*message_rewind)(void *m, int complete);
    void *(*message_ref)(void *m);
    void *(*message_unref)(void *m);
    const char *(*message_get_member)(void *m);
    const char *(*message_get_interface)(void *m);
    const char *(*message_get_path)(void *m);
    int (*message_is_method_call)(void *m, const char *interface, const char *member);
    void (*error_free)(lbus_error_t *e);
    void *(*slot_unref)(void *slot);
} lbus_t;

/* The calls, loaded on the first use (any thread). NULL (and the reason in
 * why) when there is no libsystemd or it lacks a call. */
const lbus_t *lbus(char *why, size_t cap);

/* Wait for the bus (its events and timeout) or the wake descriptor
 * (drained), at most timeout_ms (-1: no limit). */
void lbus_wait(const lbus_t *sd, void *bus, int wake_fd, int timeout_ms);

/* Run every queued message on the bus. */
void lbus_drain(const lbus_t *sd, void *bus);

/* A wake descriptor (eventfd) and writing to it. */
int lbus_wake_fd(void);
void lbus_wake(int fd);

/* An error's text for a log line: "name: message", or strerror(-rc). */
const char *lbus_why(const lbus_error_t *e, int rc, char *buf, size_t cap);

/* Reads a variant holding a basic type (s, o, b, y, u, i, n, q, x, t) into
 * the given place, or a byte array ("ay") into a string of cap bytes.
 * Returns false (and skips it) for any other type. The message is at the
 * variant. */
bool lbus_read_variant(const lbus_t *sd, void *m, char want, void *out, size_t cap);

#ifdef __cplusplus
}
#endif
