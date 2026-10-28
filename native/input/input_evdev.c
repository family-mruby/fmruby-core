/*
 * input_evdev.c - see input_evdev.h.
 *
 * The open half is the main thread's (stdio for its messages, before the
 * machine runs); the task half is a FreeRTOS task and logs through fmrb_log.
 * The task never blocks in read(): the descriptors are non-blocking and the
 * task waits with a FreeRTOS delay (the cooperative port, input_source.h).
 */

#include "input_evdev.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "boot.h"
#include "evdev_decode.h"
#include "fmrb_err.h"
#include "fmrb_kernel.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "host_task.h"
#include "keymap_extra.h"
#include "native_winops.h"

static const char *TAG = "input_evdev";

#define MAX_DEVICES 16
#define POLL_MS 10

typedef struct {
    int fd;
    char path[NATIVE_MACHINE_STR];
    char name[128];
    bool keyboard, mouse;
    evdev_decoder_t dec;
} evdev_dev_t;

static evdev_dev_t s_devs[MAX_DEVICES];
static int s_n_devs = 0;
static evdev_pointer_t s_pointer;

#define BITS_LONG (sizeof(unsigned long) * 8)
#define NLONGS(n) (((n) + BITS_LONG - 1) / BITS_LONG)
static bool has_bit(const unsigned long *bits, unsigned bit)
{
    return (bits[bit / BITS_LONG] >> (bit % BITS_LONG)) & 1UL;
}

/* What a device is, from the event types and codes it declares. */
static void classify(int fd, bool *keyboard, bool *mouse)
{
    unsigned long ev[NLONGS(EV_MAX + 1)] = { 0 };
    unsigned long key[NLONGS(KEY_MAX + 1)] = { 0 };
    unsigned long rel[NLONGS(REL_MAX + 1)] = { 0 };
    *keyboard = *mouse = false;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0) return;
    if (has_bit(ev, EV_KEY)) ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key);
    if (has_bit(ev, EV_REL)) ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel);
    *keyboard = has_bit(ev, EV_KEY) && has_bit(key, KEY_A) && has_bit(key, KEY_Z);
    *mouse = has_bit(ev, EV_REL) && has_bit(rel, REL_X) && has_bit(rel, REL_Y) && has_bit(key, BTN_LEFT);
}

typedef struct {
    int seen, denied, other;
    char first_error[160];
} scan_stats_t;

static bool add_device(const native_machine_t *m, const char *path, bool explicit_path,
                       scan_stats_t *st, char *why, size_t why_len)
{
    if (s_n_devs >= MAX_DEVICES) return true;
    st->seen++;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        if (e == EACCES || e == EPERM) st->denied++;
        if (st->first_error[0] == '\0') {
            snprintf(st->first_error, sizeof(st->first_error), "%s: %s%s", path, strerror(e),
                     (e == EACCES || e == EPERM) ? " (the user needs the input group, or root)" : "");
        }
        if (explicit_path) {
            snprintf(why, why_len, "input device %s", st->first_error);
            return false;
        }
        return true;
    }
    bool kbd = false, mouse = false;
    classify(fd, &kbd, &mouse);
    if (!kbd && !mouse) {
        st->other++;
        if (explicit_path) {
            snprintf(why, why_len, "input device %s is neither a keyboard nor a mouse", path);
            close(fd);
            return false;
        }
        close(fd);
        return true;
    }
    evdev_dev_t *d = &s_devs[s_n_devs++];
    memset(d, 0, sizeof(*d));
    d->fd = fd;
    d->keyboard = kbd;
    d->mouse = mouse;
    snprintf(d->path, sizeof(d->path), "%s", path);
    if (ioctl(fd, EVIOCGNAME(sizeof(d->name)), d->name) < 0) snprintf(d->name, sizeof(d->name), "?");
    fmrb_keymap_extra_fn extra = NULL;
    if (m->input.keymap[0] != '\0' && m->input.keymap_device[0] != '\0' &&
        strstr(d->name, m->input.keymap_device) != NULL) {
        extra = fmrb_keymap_extra_find(m->input.keymap);
    }
    evdev_decoder_init(&d->dec, &s_pointer, extra);
    fprintf(stderr, "native: evdev %s \"%s\":%s%s%s%s\n", path, d->name,
            kbd ? " keyboard" : "", mouse ? " mouse" : "",
            extra ? ", keymap " : "", extra ? m->input.keymap : "");
    return true;
}

static bool scan_auto(const native_machine_t *m, scan_stats_t *st, char *why, size_t why_len)
{
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        snprintf(why, why_len, "no /dev/input (%s): this system has no evdev devices", strerror(errno));
        return false;
    }
    /* event0, event1 ... in number order, so the choice is the same every start. */
    int nums[256];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL && n < 256) {
        int k = 0;
        char tail = 0;
        if (sscanf(e->d_name, "event%d%c", &k, &tail) == 1) nums[n++] = k;
    }
    closedir(dir);
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && nums[j - 1] > nums[j]; j--) {
            int t = nums[j]; nums[j] = nums[j - 1]; nums[j - 1] = t;
        }
    }
    for (int i = 0; i < n; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", nums[i]);
        if (!add_device(m, path, false, st, why, why_len)) return false;
    }
    return true;
}

bool native_evdev_open(const native_machine_t *m, char *why, size_t why_len)
{
    scan_stats_t st;
    memset(&st, 0, sizeof(st));
    for (int i = 0; i < m->input.n_devices; i++) {
        const char *dev = m->input.devices[i];
        bool ok = strcmp(dev, "auto") == 0 ? scan_auto(m, &st, why, why_len)
                                           : add_device(m, dev, true, &st, why, why_len);
        if (!ok) return false;
    }
    if (s_n_devs == 0) {
        if (st.seen == 0) {
            snprintf(why, why_len, "no evdev devices: /dev/input has no event* nodes "
                                   "(no keyboard or mouse is attached, or this is not a console system)");
        } else if (st.denied > 0) {
            snprintf(why, why_len, "no evdev keyboard or mouse could be opened: %d of %d devices "
                                   "refused (%s)", st.denied, st.seen, st.first_error);
        } else {
            snprintf(why, why_len, "no keyboard or mouse among %d evdev devices", st.seen);
        }
        return false;
    }
    return true;
}

/* ---- the task ---------------------------------------------------------- */

static void sink_key(void *ud, bool down, uint8_t usage, uint8_t mods, bool force_us)
{
    (void)ud;
    if (native_winops_key(usage, down)) return;   /* Super and an arrow (L13) */
    if (force_us) {
        if (down) fmrb_host_send_key_down_us(usage, usage, mods);
        else fmrb_host_send_key_up_us(usage, usage, mods);
    } else {
        if (down) fmrb_host_send_key_down(usage, usage, mods);
        else fmrb_host_send_key_up(usage, usage, mods);
    }
}

static void sink_move(void *ud, int x, int y)
{
    (void)ud;
    fmrb_host_send_mouse_move(x, y);
}

static void sink_button(void *ud, int x, int y, int button, bool down)
{
    (void)ud;
    if (native_winops_button(x, y, button, down)) return;   /* a title's double-click (L13) */
    fmrb_host_send_mouse_click(x, y, button, down ? 1 : 0);
}

static void sink_wheel(void *ud, int x, int y, int notches)
{
    (void)ud;
    fmrb_host_send_mouse_wheel(x, y, notches);
}

static const evdev_sink_t s_sink = {
    .key = sink_key,
    .move = sink_move,
    .button = sink_button,
    .wheel = sink_wheel,
};

static void read_geometry(evdev_geometry_t *geo)
{
    memset(geo, 0, sizeof(*geo));
    geo->scale_x = geo->scale_y = 1.0;
    if (!fmrb_kernel_is_ready()) return;
    const fmrb_system_config_t *conf = fmrb_kernel_get_config();
    if (conf == NULL) return;
    geo->base_w = conf->display_width;
    geo->base_h = conf->display_height;
    geo->scale_x = conf->mouse_scale_x;
    geo->scale_y = conf->mouse_scale_y;
    fmrb_host_get_screen_size(&geo->screen_w, &geo->screen_h);
    if (geo->screen_w <= 0 || geo->screen_h <= 0) {
        geo->screen_w = geo->base_w;
        geo->screen_h = geo->base_h;
    }
}

static void evdev_task(void *arg)
{
    (void)arg;
    for (;;) {
        evdev_geometry_t geo;
        read_geometry(&geo);
        for (int i = 0; i < s_n_devs; i++) {
            evdev_dev_t *d = &s_devs[i];
            if (d->fd < 0) continue;
            if (evdev_drain_fd(d->fd, &d->dec, &geo, &s_sink, NULL) < 0) {
                FMRB_LOGW(TAG, "%s (%s) is gone", d->path, d->name);
                close(d->fd);
                d->fd = -1;
            }
        }
        fmrb_task_delay_ms(POLL_MS);
    }
}

static fmrb_err_t evdev_start(void)
{
    if (s_n_devs == 0) return FMRB_ERR_INVALID_STATE;
    if (fmrb_task_create(evdev_task, "input_evdev", 16384, NULL, 5, NULL) != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to create the evdev task");
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "%d evdev device(s)", s_n_devs);
    return FMRB_OK;
}

const fmrb_input_source_t fmrb_input_source_evdev = {
    .name  = "evdev",
    .start = evdev_start,
};
