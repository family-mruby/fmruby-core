/*
 * x11_clip.c - an X11 client for the compositor's clipboard check
 * (test/native/wl/wl_clip_check.rb, doc/linux_native/ L12a): it takes the
 * CLIPBOARD selection with a text, or asks for the CLIPBOARD's text, driven
 * by lines on a FIFO, telling what happens on a log.
 *
 *   x11_clip FIFO LOG
 *
 * Starts one window, "XClip" (class XClip, 200x100, grey). The lines:
 *
 *   set TEXT     own CLIPBOARD with TEXT (the rest of the line, UTF-8),
 *                answering TARGETS, UTF8_STRING, text/plain;charset=utf-8
 *                and STRING
 *   get          ask for CLIPBOARD as UTF8_STRING; the answer is logged
 *   quit         end
 *
 * The log: "started", "owner N" (1 when XSetSelectionOwner held), "served
 * TARGET" (each request answered), "lost" (SelectionClear), "got TEXT" or
 * "got none" (the answer to get: no owner, or no conversion), "focus".
 *
 * A test program, not part of the product: plain libc and Xlib.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Display *d;
static Window win;
static FILE *logf;
static bool quit;
static char *text;
static Atom a_clipboard, a_targets, a_utf8, a_text_plain, a_prop;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

static void command(char *line)
{
    if (strncmp(line, "set ", 4) == 0) {
        free(text);
        text = strdup(line + 4);
        XSetSelectionOwner(d, a_clipboard, win, CurrentTime);
        say("owner %d", XGetSelectionOwner(d, a_clipboard) == win ? 1 : 0);
    } else if (strcmp(line, "get") == 0) {
        XDeleteProperty(d, win, a_prop);
        XConvertSelection(d, a_clipboard, a_utf8, a_prop, win, CurrentTime);
    } else if (strcmp(line, "quit") == 0) {
        quit = true;
    } else {
        say("error: unknown line %s", line);
    }
    XFlush(d);
}

static void serve(XSelectionRequestEvent *r)
{
    XSelectionEvent ev = { .type = SelectionNotify, .display = d, .requestor = r->requestor,
                           .selection = r->selection, .target = r->target, .property = None, .time = r->time };
    Atom prop = r->property != None ? r->property : r->target;
    if (text != NULL && r->target == a_targets) {
        Atom t[] = { a_targets, a_utf8, a_text_plain, XA_STRING };
        XChangeProperty(d, r->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)t, 4);
        ev.property = prop;
    } else if (text != NULL && (r->target == a_utf8 || r->target == a_text_plain || r->target == XA_STRING)) {
        XChangeProperty(d, r->requestor, prop, r->target, 8, PropModeReplace, (unsigned char *)text,
                        (int)strlen(text));
        ev.property = prop;
    }
    char *name = XGetAtomName(d, r->target);
    say("served %s", name ? name : "?");
    if (name) XFree(name);
    XSendEvent(d, r->requestor, False, 0, (XEvent *)&ev);
    XFlush(d);
}

static void got(XSelectionEvent *e)
{
    if (e->property == None) {
        say("got none");
        return;
    }
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(d, win, a_prop, 0, 1 << 20, True, AnyPropertyType, &type, &format, &n, &after, &data) ==
            Success && data != NULL) {
        say("got %.*s", (int)n, (char *)data);
        XFree(data);
    } else {
        say("got none");
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: x11_clip FIFO LOG\n");
        return 2;
    }
    logf = fopen(argv[2], "a");
    if (logf == NULL) return 1;
    int fifo = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fifo < 0) {
        say("error: %s: %s", argv[1], strerror(errno));
        return 1;
    }
    d = XOpenDisplay(NULL);
    if (d == NULL) {
        say("error: no X display");
        return 1;
    }
    a_clipboard = XInternAtom(d, "CLIPBOARD", False);
    a_targets = XInternAtom(d, "TARGETS", False);
    a_utf8 = XInternAtom(d, "UTF8_STRING", False);
    a_text_plain = XInternAtom(d, "text/plain;charset=utf-8", False);
    a_prop = XInternAtom(d, "XCLIP_GOT", False);
    XSetWindowAttributes a = { .background_pixel = 0x808080,
                               .event_mask = ExposureMask | KeyPressMask | ButtonPressMask | FocusChangeMask |
                                             PropertyChangeMask };
    win = XCreateWindow(d, DefaultRootWindow(d), 0, 0, 200, 100, 0, CopyFromParent, InputOutput, CopyFromParent,
                        CWBackPixel | CWEventMask, &a);
    XStoreName(d, win, "XClip");
    XClassHint ch = { .res_name = "XClip", .res_class = "XClip" };
    XSetClassHint(d, win, &ch);
    XMapWindow(d, win);
    XFlush(d);
    say("started");

    char buf[1024];
    size_t used = 0;
    while (!quit) {
        while (XPending(d) > 0) {
            XEvent e;
            XNextEvent(d, &e);
            if (e.type == SelectionRequest) serve(&e.xselectionrequest);
            else if (e.type == SelectionNotify) got(&e.xselection);
            else if (e.type == SelectionClear) say("lost");
            else if (e.type == FocusIn) say("focus");
        }
        if (quit) break;
        struct pollfd pf[2] = { { ConnectionNumber(d), POLLIN, 0 }, { fifo, POLLIN, 0 } };
        if (poll(pf, 2, 200) < 0 && errno != EINTR) break;
        if (pf[0].revents & (POLLERR | POLLHUP)) break;
        if (pf[1].revents & POLLIN) {
            ssize_t n = read(fifo, buf + used, sizeof(buf) - 1 - used);
            if (n > 0) used += (size_t)n;
            char *nl;
            while ((nl = memchr(buf, '\n', used)) != NULL) {
                *nl = '\0';
                command(buf);
                size_t rest = used - (size_t)(nl + 1 - buf);
                memmove(buf, nl + 1, rest);
                used = rest;
            }
            if (used == sizeof(buf) - 1) used = 0;
        }
    }
    XCloseDisplay(d);
    return 0;
}
