/*
 * x11_dialog.c - an X11 client for the compositor's checks
 * (test/native/wl/wl_dialog_check.rb, doc/linux_native/ L9c): a window with
 * a transient one (WM_TRANSIENT_FOR, a dialog) and an override-redirect one
 * (a menu), driven by lines on a FIFO, telling what it gets on a log.
 *
 *   x11_dialog FIFO LOG
 *
 * Starts one window, "XParent" (class XParent, 300x150, green). The lines:
 *
 *   dialog W H          a window "XDialog" (class XDialog), transient for the
 *                       parent, W x H, blue, with a red button of 40x24 in
 *                       its bottom-right corner: a press and release on the
 *                       button closes it
 *   menu X Y W H        an override-redirect window at (X, Y) of the screen,
 *                       W x H, yellow
 *   grabmenu X Y W H    the same as a toolkit's menu (L9e): typed a popup
 *                       menu (_NET_WM_WINDOW_TYPE_POPUP_MENU), holding the
 *                       pointer (a grab) once it shows, and closed by a press
 *                       outside it
 *   unmenu              the menu destroyed
 *   quit                end
 *
 * The log: "configure parent|dialog|menu X Y W H" (each ConfigureNotify),
 * "key parent|dialog CODE 1|0" (X key codes), "button parent|dialog|menu B
 * 1|0 X Y", "focus parent|dialog", "dialog closed by its button", "close
 * parent|dialog" (WM_DELETE_WINDOW), "menu grab N" (XGrabPointer's answer,
 * 0 = GrabSuccess), "menu closed by a press outside".
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

#define BTN_W 40
#define BTN_H 24

static Display *d;
static Window parent, dialog, menu;
static int dlg_w, dlg_h;
static Atom wm_delete;
static FILE *logf;
static bool quit;
static bool btn_down_on_button;
static bool menu_wants_grab;
static int menu_w, menu_h;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

static const char *name_of(Window w)
{
    if (w == parent) return "parent";
    if (w == dialog && dialog != 0) return "dialog";
    if (w == menu && menu != 0) return "menu";
    return "?";
}

static unsigned long colour(unsigned r, unsigned g, unsigned b)
{
    XColor c = { .red = (unsigned short)(r * 257), .green = (unsigned short)(g * 257),
                 .blue = (unsigned short)(b * 257), .flags = DoRed | DoGreen | DoBlue };
    XAllocColor(d, DefaultColormap(d, DefaultScreen(d)), &c);
    return c.pixel;
}

static Window make(const char *title, const char *cls, int x, int y, int w, int h, unsigned long bg, bool or)
{
    XSetWindowAttributes a = { .background_pixel = bg, .override_redirect = or ? True : False,
                               .event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                                             ButtonReleaseMask | StructureNotifyMask | FocusChangeMask };
    Window win = XCreateWindow(d, DefaultRootWindow(d), x, y, (unsigned)w, (unsigned)h, 0, CopyFromParent,
                               InputOutput, CopyFromParent, CWBackPixel | CWOverrideRedirect | CWEventMask, &a);
    XStoreName(d, win, title);
    XClassHint ch = { .res_name = (char *)cls, .res_class = (char *)cls };
    XSetClassHint(d, win, &ch);
    if (!or) XSetWMProtocols(d, win, &wm_delete, 1);
    return win;
}

static void draw_dialog(void)
{
    if (dialog == 0) return;
    GC gc = XCreateGC(d, dialog, 0, NULL);
    XSetForeground(d, gc, colour(255, 0, 0));
    XFillRectangle(d, dialog, gc, dlg_w - BTN_W, dlg_h - BTN_H, BTN_W, BTN_H);
    XFreeGC(d, gc);
}

static void command(char *line)
{
    int x = 0, y = 0, w = 0, h = 0;
    if (sscanf(line, "dialog %d %d", &w, &h) == 2) {
        if (dialog != 0) XDestroyWindow(d, dialog);
        dlg_w = w;
        dlg_h = h;
        dialog = make("XDialog", "XDialog", 0, 0, w, h, colour(0, 0, 255), false);
        XSetTransientForHint(d, dialog, parent);
        XMapWindow(d, dialog);
    } else if (sscanf(line, "menu %d %d %d %d", &x, &y, &w, &h) == 4) {
        if (menu != 0) XDestroyWindow(d, menu);
        menu = make("XMenu", "XMenu", x, y, w, h, colour(255, 255, 0), true);
        menu_w = menu_h = 0;
        XMapRaised(d, menu);
    } else if (sscanf(line, "grabmenu %d %d %d %d", &x, &y, &w, &h) == 4) {
        if (menu != 0) XDestroyWindow(d, menu);
        menu = make("XMenu", "XMenu", x, y, w, h, colour(255, 255, 0), true);
        Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
        Atom popup = XInternAtom(d, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
        XChangeProperty(d, menu, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&popup, 1);
        menu_w = w;
        menu_h = h;
        menu_wants_grab = true;
        XMapRaised(d, menu);
    } else if (strcmp(line, "unmenu") == 0) {
        if (menu != 0) XDestroyWindow(d, menu);
        menu = 0;
        menu_w = menu_h = 0;
    } else if (strcmp(line, "quit") == 0) {
        quit = true;
    } else {
        say("error: unknown line %s", line);
    }
    XFlush(d);
}

static void event(XEvent *e)
{
    Window w = e->xany.window;
    switch (e->type) {
    case MapNotify:
        if (w == menu && menu != 0 && menu_wants_grab) {
            menu_wants_grab = false;
            int st = XGrabPointer(d, menu, False, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                                  None, None, CurrentTime);
            say("menu grab %d", st);
        }
        break;
    case Expose:
        if (w == dialog) draw_dialog();
        break;
    case ConfigureNotify:
        if (w == dialog) {
            dlg_w = e->xconfigure.width;
            dlg_h = e->xconfigure.height;
        }
        say("configure %s %d %d %d %d", name_of(w), e->xconfigure.x, e->xconfigure.y, e->xconfigure.width,
            e->xconfigure.height);
        break;
    case KeyPress:
    case KeyRelease:
        say("key %s %u %d", name_of(w), e->xkey.keycode, e->type == KeyPress ? 1 : 0);
        break;
    case ButtonPress:
    case ButtonRelease: {
        bool down = e->type == ButtonPress;
        int bx = e->xbutton.x, by = e->xbutton.y;
        say("button %s %u %d %d %d", name_of(w), e->xbutton.button, down ? 1 : 0, bx, by);
        if (down && w == menu && menu != 0 && menu_w > 0 && (bx < 0 || by < 0 || bx >= menu_w || by >= menu_h)) {
            say("menu closed by a press outside");
            XUngrabPointer(d, CurrentTime);
            XDestroyWindow(d, menu);
            menu = 0;
            menu_w = menu_h = 0;
            break;
        }
        bool on_button = w == dialog && dialog != 0 && bx >= dlg_w - BTN_W && by >= dlg_h - BTN_H;
        if (down) {
            btn_down_on_button = on_button;
        } else if (on_button && btn_down_on_button) {
            btn_down_on_button = false;
            say("dialog closed by its button");
            XDestroyWindow(d, dialog);
            dialog = 0;
        }
        break;
    }
    case FocusIn:
        say("focus %s", name_of(w));
        break;
    case ClientMessage:
        if ((Atom)e->xclient.data.l[0] == wm_delete) {
            say("close %s", name_of(w));
            if (w == parent) {
                quit = true;
            } else if (w == dialog) {
                XDestroyWindow(d, dialog);
                dialog = 0;
            }
        }
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: x11_dialog FIFO LOG\n");
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
    wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    parent = make("XParent", "XParent", 0, 0, 300, 150, colour(0, 192, 0), false);
    XMapWindow(d, parent);
    XFlush(d);
    say("started");

    char buf[512];
    size_t used = 0;
    while (!quit) {
        while (XPending(d) > 0) {
            XEvent e;
            XNextEvent(d, &e);
            event(&e);
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
    say("end");
    XCloseDisplay(d);
    return 0;
}
