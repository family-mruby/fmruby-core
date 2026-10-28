/*
 * fake_bluez.c - BlueZ's part the settings window uses (doc/linux_native/
 * L14), standing in for it on a bus of the check's own (a dbus-daemon given
 * as DBUS_SYSTEM_BUS_ADDRESS): never the machine's.
 *
 *   fake_bluez LOG
 *
 * It owns org.bluez and serves the object manager at / with
 *
 *   /org/bluez/hci0                 Adapter1: Powered (read-write, starts
 *                                   off), Address, Alias
 *   /org/bluez/hci0/dev_..._01      "FM Headphones": paired, not connected
 *   /org/bluez/hci0/dev_..._02      "FM Keyboard": paired, connected
 *   /org/bluez/hci0/dev_..._03      "FM Speaker": paired; Connect fails
 *                                   (org.bluez.Error.Failed)
 *   /org/bluez/hci0/dev_..._04      "Stranger": seen, never paired
 *
 * Device1: Address, Alias, Name, Icon, Paired, Bonded, Trusted, Connected,
 * Adapter; Connect and Disconnect (both refused while the adapter is off:
 * org.bluez.Error.NotReady).
 *
 * The log: "ready", "Powered 0|1", "Connect <name>: ok|<error>",
 * "Disconnect <name>".
 *
 * A test program, not part of the product: libc and sd-bus.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <systemd/sd-bus.h>

#define ADAPTER "/org/bluez/hci0"

static FILE *g_log;
static sd_bus *g_bus;
static int g_powered = 1;

typedef struct {
    const char *path;
    const char *addr;
    const char *name;
    const char *icon;
    int paired;
    int connected;
    int fails;
} dev_t_;

static dev_t_ g_dev[] = {
    { ADAPTER "/dev_F0_00_00_00_00_01", "F0:00:00:00:00:01", "FM Headphones", "audio-headphones", 1, 0, 0 },
    { ADAPTER "/dev_F0_00_00_00_00_02", "F0:00:00:00:00:02", "FM Keyboard", "input-keyboard", 1, 1, 0 },
    { ADAPTER "/dev_F0_00_00_00_00_03", "F0:00:00:00:00:03", "FM Speaker", "audio-card", 1, 0, 1 },
    { ADAPTER "/dev_F0_00_00_00_00_04", "F0:00:00:00:00:04", "Stranger", "phone", 0, 0, 0 },
};
#define NDEV (sizeof(g_dev) / sizeof(g_dev[0]))

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static int p_adapter(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
                     void *ud, sd_bus_error *e)
{
    (void)b; (void)path; (void)iface; (void)ud; (void)e;
    if (strcmp(prop, "Powered") == 0) return sd_bus_message_append(reply, "b", g_powered);
    if (strcmp(prop, "Address") == 0) return sd_bus_message_append(reply, "s", "F0:00:00:00:00:AA");
    return sd_bus_message_append(reply, "s", "fmrb-check");
}

static int set_powered(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *value,
                       void *ud, sd_bus_error *e)
{
    (void)b; (void)path; (void)iface; (void)prop; (void)ud; (void)e;
    int v = 0;
    int r = sd_bus_message_read(value, "b", &v);
    if (r < 0) return r;
    g_powered = v != 0;
    say("Powered %d", g_powered);
    if (!g_powered) {
        for (size_t i = 0; i < NDEV; i++) g_dev[i].connected = 0;
    }
    return 0;
}

static const sd_bus_vtable adapter_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_WRITABLE_PROPERTY("Powered", "b", p_adapter, set_powered, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Address", "s", p_adapter, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Alias", "s", p_adapter, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static dev_t_ *dev_of(const char *path)
{
    for (size_t i = 0; i < NDEV; i++) {
        if (strcmp(g_dev[i].path, path) == 0) return &g_dev[i];
    }
    return NULL;
}

static int p_dev(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply, void *ud,
                 sd_bus_error *e)
{
    (void)b; (void)iface; (void)ud; (void)e;
    dev_t_ *d = dev_of(path);
    if (d == NULL) return -ENOENT;
    if (strcmp(prop, "Address") == 0) return sd_bus_message_append(reply, "s", d->addr);
    if (strcmp(prop, "Alias") == 0 || strcmp(prop, "Name") == 0) return sd_bus_message_append(reply, "s", d->name);
    if (strcmp(prop, "Icon") == 0) return sd_bus_message_append(reply, "s", d->icon);
    if (strcmp(prop, "Paired") == 0 || strcmp(prop, "Bonded") == 0) return sd_bus_message_append(reply, "b", d->paired);
    if (strcmp(prop, "Trusted") == 0) return sd_bus_message_append(reply, "b", 0);
    if (strcmp(prop, "Connected") == 0) return sd_bus_message_append(reply, "b", d->connected);
    return sd_bus_message_append(reply, "o", ADAPTER);
}

static int m_connect(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    dev_t_ *d = dev_of(sd_bus_message_get_path(m));
    if (d == NULL) return sd_bus_error_set(e, "org.bluez.Error.DoesNotExist", "no device");
    if (!g_powered) {
        say("Connect %s: NotReady", d->name);
        return sd_bus_error_set(e, "org.bluez.Error.NotReady", "Resource Not Ready");
    }
    if (d->fails) {
        say("Connect %s: Failed", d->name);
        return sd_bus_error_set(e, "org.bluez.Error.Failed", "br-connection-page-timeout");
    }
    d->connected = 1;
    say("Connect %s: ok", d->name);
    return sd_bus_reply_method_return(m, "");
}

static int m_disconnect(sd_bus_message *m, void *ud, sd_bus_error *e)
{
    (void)ud;
    dev_t_ *d = dev_of(sd_bus_message_get_path(m));
    if (d == NULL) return sd_bus_error_set(e, "org.bluez.Error.DoesNotExist", "no device");
    d->connected = 0;
    say("Disconnect %s", d->name);
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable dev_vt[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Address", "s", p_dev, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Alias", "s", p_dev, 0, 0),
    SD_BUS_PROPERTY("Name", "s", p_dev, 0, 0),
    SD_BUS_PROPERTY("Icon", "s", p_dev, 0, 0),
    SD_BUS_PROPERTY("Paired", "b", p_dev, 0, 0),
    SD_BUS_PROPERTY("Bonded", "b", p_dev, 0, 0),
    SD_BUS_PROPERTY("Trusted", "b", p_dev, 0, 0),
    SD_BUS_PROPERTY("Connected", "b", p_dev, 0, 0),
    SD_BUS_PROPERTY("Adapter", "o", p_dev, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Connect", "", "", m_connect, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Disconnect", "", "", m_disconnect, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fake_bluez LOG\n");
        return 2;
    }
    g_log = fopen(argv[1], "a");
    if (!g_log) return 1;
    if (sd_bus_open_system(&g_bus) < 0) {
        say("error: no bus");
        return 1;
    }
    sd_bus_add_object_manager(g_bus, NULL, "/");
    sd_bus_add_object_vtable(g_bus, NULL, ADAPTER, "org.bluez.Adapter1", adapter_vt, NULL);
    for (size_t i = 0; i < NDEV; i++) {
        sd_bus_add_object_vtable(g_bus, NULL, g_dev[i].path, "org.bluez.Device1", dev_vt, NULL);
    }
    if (sd_bus_request_name(g_bus, "org.bluez", 0) < 0) {
        say("error: the name");
        return 1;
    }
    say("ready");
    for (;;) {
        while (sd_bus_process(g_bus, NULL) > 0) {
        }
        sd_bus_wait(g_bus, (uint64_t)-1);
    }
}
