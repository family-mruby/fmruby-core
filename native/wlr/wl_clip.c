/*
 * wl_clip.c - the machine's clipboard as the Wayland selection
 * (doc/linux_native, L12a). See wl_clip.h.
 *
 * Main thread only (the compositor's), never waiting:
 *
 *   Family mruby -> Linux  The text an app copied comes through native_clip
 *       (the eventfd wakes the loop); the compositor becomes the selection's
 *       owner with a data source of its own offering the text's types. A
 *       Linux program that pastes gets the text written into its pipe as the
 *       pipe takes it (a writable event per pipe, never a blocking write).
 *   Linux -> Family mruby  A selection set by a Linux program (a Wayland
 *       one, or an X11 one through Xwayland's selection bridge, which
 *       wlroots runs) is read when it is set, if it offers text: a pipe of
 *       ours is handed to the program and read as data comes, up to
 *       FMRB_CLIPBOARD_MAX bytes (more: the reading stops and the kernel is
 *       told the text was too big), then handed to native_clip. A new
 *       selection, or one that never finishes within READ_TIMEOUT_MS,
 *       abandons the reading.
 *   Keeping it  When the selection's owner goes away (the program ended),
 *       the selection would be empty; the compositor takes it over again
 *       with the last text, so what was copied stays pasteable, as a
 *       clipboard manager does.
 *
 * The primary selection (the middle button's paste) is not served: no
 * primary-selection manager is created, so programs keep it to themselves.
 */

#define _GNU_SOURCE
#define WLR_USE_UNSTABLE 1

#include "wl_clip.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>

#include "fmrb_clipboard.h"
#include "native_clip.h"

#define READ_TIMEOUT_MS 5000
#define MAX_WRITERS 8

/* The types offered, the first the one programs ask for. */
static const char *const OFFER_TYPES[] = {
    "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING",
};
/* The types read, in this order of preference (all taken as UTF-8). */
static const char *const READ_TYPES[] = {
    "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT",
};

typedef struct {
    struct wlr_data_source base;
    char *text;
    size_t len;
} clip_source_t;

typedef struct writer {
    struct wl_list link;
    int fd;
    char *buf;
    size_t len, off;
    struct wl_event_source *ev;
} writer_t;

static struct {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wlr_seat *seat;
    struct wl_listener set_selection;
    bool ready;

    clip_source_t *own;             /* our source while it is the selection */
    char *text;                     /* the last text either way (kept) */
    size_t len;
    bool reown;                     /* take the selection again (pump) */

    /* Reading a Linux program's selection. */
    struct {
        int fd;
        struct wl_event_source *ev, *timer;
        char *buf;
        size_t len;
        bool big;
        char type[48];
    } rd;

    struct wl_list writers;         /* writer_t */
    unsigned n_writers;

    wl_clip_stats_t st;
} s = { .rd = { .fd = -1 } };

/* ------------------------------------------------------------- writing */

static void writer_end(writer_t *w)
{
    if (w->ev) wl_event_source_remove(w->ev);
    close(w->fd);
    free(w->buf);
    wl_list_remove(&w->link);
    s.n_writers--;
    free(w);
}

/* A write that never raises SIGPIPE: the program may close its end early,
 * and an ignored SIGPIPE would be inherited by every program started from
 * here, so it is blocked for the call alone and a pending one consumed. */
static ssize_t write_quiet(int fd, const char *p, size_t n)
{
    sigset_t pipe_set, old;
    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_set, &old);
    ssize_t r = write(fd, p, n);
    int e = errno;
    if (r < 0 && e == EPIPE) {
        struct timespec zero = { 0, 0 };
        sigtimedwait(&pipe_set, NULL, &zero);
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    errno = e;
    return r;
}

/* Write what the pipe takes; true when the writer is done (ended). */
static bool writer_step(writer_t *w)
{
    while (w->off < w->len) {
        ssize_t r = write_quiet(w->fd, w->buf + w->off, w->len - w->off);
        if (r > 0) {
            w->off += (size_t)r;
            continue;
        }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;
        if (r < 0 && errno == EINTR) continue;
        break;  /* the program closed its end, or another error */
    }
    writer_end(w);
    return true;
}

static int writer_ready(int fd, uint32_t mask, void *data)
{
    (void)fd;
    writer_t *w = data;
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        writer_end(w);
        return 0;
    }
    writer_step(w);
    return 0;
}

static void source_send(struct wlr_data_source *ws, const char *mime_type, int32_t fd)
{
    clip_source_t *c = wl_container_of(ws, c, base);
    (void)mime_type;
    if (s.n_writers >= MAX_WRITERS) {
        close(fd);
        return;
    }
    writer_t *w = calloc(1, sizeof(*w));
    char *buf = c->len > 0 ? malloc(c->len) : NULL;
    if (w == NULL || (c->len > 0 && buf == NULL)) {
        free(w);
        free(buf);
        close(fd);
        return;
    }
    if (c->len > 0) memcpy(buf, c->text, c->len);
    w->fd = fd;
    w->buf = buf;
    w->len = c->len;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    wl_list_insert(&s.writers, &w->link);
    s.n_writers++;
    s.st.served++;
    if (!writer_step(w)) {
        w->ev = wl_event_loop_add_fd(s.loop, fd, WL_EVENT_WRITABLE, writer_ready, w);
        if (w->ev == NULL) writer_end(w);
    }
}

static void source_destroy(struct wlr_data_source *ws)
{
    clip_source_t *c = wl_container_of(ws, c, base);
    if (s.own == c) s.own = NULL;
    free(c->text);
    free(c);
}

static const struct wlr_data_source_impl s_source_impl = {
    .send = source_send,
    .destroy = source_destroy,
};

/* Become the selection's owner with the kept text. */
static void own_selection(void)
{
    if (s.text == NULL) return;
    clip_source_t *c = calloc(1, sizeof(*c));
    char *copy = malloc(s.len + 1);
    if (c == NULL || copy == NULL) {
        free(c);
        free(copy);
        return;
    }
    memcpy(copy, s.text, s.len);
    copy[s.len] = '\0';
    wlr_data_source_init(&c->base, &s_source_impl);
    c->text = copy;
    c->len = s.len;
    for (size_t i = 0; i < sizeof(OFFER_TYPES) / sizeof(OFFER_TYPES[0]); i++) {
        char **slot = wl_array_add(&c->base.mime_types, sizeof(char *));
        if (slot != NULL) *slot = strdup(OFFER_TYPES[i]);
    }
    s.own = c;
    s.st.owned++;
    wlr_seat_set_selection(s.seat, &c->base, wl_display_next_serial(s.display));
}

static void keep(char *text, size_t len)
{
    free(s.text);
    s.text = text;
    s.len = len;
}

/* ------------------------------------------------------------- reading */

static void read_end(void)
{
    if (s.rd.ev) wl_event_source_remove(s.rd.ev);
    if (s.rd.timer) wl_event_source_remove(s.rd.timer);
    if (s.rd.fd >= 0) close(s.rd.fd);
    free(s.rd.buf);
    s.rd.ev = NULL;
    s.rd.timer = NULL;
    s.rd.fd = -1;
    s.rd.buf = NULL;
    s.rd.len = 0;
    s.rd.big = false;
}

static void read_done(void)
{
    if (s.rd.big) {
        fprintf(stderr, "native: clipboard: a Linux program's text (%s) is over %d bytes; "
                        "not taken\n", s.rd.type, FMRB_CLIPBOARD_MAX);
        native_clip_offer(NULL, s.rd.len);
        s.st.refused++;
        read_end();
        return;
    }
    char *text = malloc(s.rd.len + 1);
    if (text != NULL) {
        if (s.rd.len > 0) memcpy(text, s.rd.buf, s.rd.len);
        text[s.rd.len] = '\0';
        native_clip_offer(text, s.rd.len);
        keep(text, s.rd.len);
        s.st.took++;
        s.st.last_took = s.rd.len;
        fprintf(stderr, "native: clipboard: took %zu bytes from a Linux program (%s)\n", s.rd.len, s.rd.type);
    }
    read_end();
}

static int read_ready(int fd, uint32_t mask, void *data)
{
    (void)data;
    (void)mask;
    for (;;) {
        char chunk[4096];
        ssize_t r = read(fd, chunk, sizeof(chunk));
        if (r > 0) {
            if (s.rd.len + (size_t)r > FMRB_CLIPBOARD_MAX) {
                /* Over the maximum: count it and stop reading. */
                s.rd.len += (size_t)r;
                s.rd.big = true;
                read_done();
                return 0;
            }
            memcpy(s.rd.buf + s.rd.len, chunk, (size_t)r);
            s.rd.len += (size_t)r;
            continue;
        }
        if (r == 0) {
            read_done();
            return 0;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        fprintf(stderr, "native: clipboard: reading a Linux program's text: %s\n", strerror(errno));
        read_end();
        return 0;
    }
}

static int read_timeout(void *data)
{
    (void)data;
    fprintf(stderr, "native: clipboard: a Linux program did not hand over its text within %d ms\n",
            READ_TIMEOUT_MS);
    s.st.timeouts++;
    read_end();
    return 0;
}

static const char *text_type(struct wlr_data_source *src)
{
    for (size_t k = 0; k < sizeof(READ_TYPES) / sizeof(READ_TYPES[0]); k++) {
        char **t;
        wl_array_for_each(t, &src->mime_types) {
            if (*t != NULL && strcmp(*t, READ_TYPES[k]) == 0) return READ_TYPES[k];
        }
    }
    return NULL;
}

static void read_selection(struct wlr_data_source *src)
{
    read_end();
    const char *type = text_type(src);
    if (type == NULL) {
        s.st.not_text++;
        return;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) return;
    s.rd.buf = malloc(FMRB_CLIPBOARD_MAX);
    if (s.rd.buf == NULL) {
        close(fds[0]);
        close(fds[1]);
        return;
    }
    snprintf(s.rd.type, sizeof(s.rd.type), "%s", type);
    s.rd.fd = fds[0];
    s.rd.ev = wl_event_loop_add_fd(s.loop, fds[0], WL_EVENT_READABLE, read_ready, NULL);
    s.rd.timer = wl_event_loop_add_timer(s.loop, read_timeout, NULL);
    if (s.rd.timer) wl_event_source_timer_update(s.rd.timer, READ_TIMEOUT_MS);
    /* The source writes into the other end and closes it (the program's, or
     * Xwayland's bridge on behalf of an X11 program). */
    wlr_data_source_send(src, type, fds[1]);
}

static void set_selection(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    struct wlr_data_source *src = s.seat->selection_source;
    if (src == NULL) {
        /* The owner went away: keep what was copied (in the pump, not inside
         * this signal). */
        read_end();
        if (s.text != NULL) s.reown = true;
        return;
    }
    if (src->impl == &s_source_impl) return;
    s.st.linux_sets++;
    read_selection(src);
}

/* ------------------------------------------------------------- the face */

void wl_clip_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_seat *seat)
{
    s.display = display;
    s.loop = loop;
    s.seat = seat;
    wl_list_init(&s.writers);
    s.set_selection.notify = set_selection;
    wl_signal_add(&seat->events.set_selection, &s.set_selection);
    s.ready = true;
}

void wl_clip_pump(void)
{
    if (!s.ready) return;
    char *text = NULL;
    size_t len = 0;
    if (native_clip_take_out(&text, &len)) {
        /* Family mruby's copy is newer than anything being read. */
        read_end();
        keep(text, len);
        s.st.gave++;
        s.st.last_gave = len;
        own_selection();
        s.reown = false;
        return;
    }
    if (s.reown) {
        s.reown = false;
        if (s.seat->selection_source == NULL && s.text != NULL) {
            s.st.kept++;
            own_selection();
        }
    }
}

void wl_clip_fini(void)
{
    if (!s.ready) return;
    read_end();
    while (!wl_list_empty(&s.writers)) {
        writer_t *w = wl_container_of(s.writers.next, w, link);
        writer_end(w);
    }
    wl_list_remove(&s.set_selection.link);
    s.ready = false;
}

void wl_clip_stats(wl_clip_stats_t *out)
{
    *out = s.st;
    out->owner = s.seat == NULL ? 0
               : s.seat->selection_source == NULL ? 0
               : s.seat->selection_source->impl == &s_source_impl ? 1 : 2;
    out->kept_len = s.len;
}
