/*
 * output_wlr.c - the program as a Wayland compositor (doc/linux_native/,
 * L8, L9a): the "wlroots" output and input of native_backends.h. Started
 * from tinywl (wlroots 0.19's smallest example).
 *
 * Threads. wlroots runs on the program's main thread, which is not a
 * FreeRTOS task (main_native.c): pump() is wl_event_loop_dispatch. The
 * machine's threads never call wlroots. The hand-overs:
 *
 *   picture  the display task puts each composited frame into native_frame
 *            (its lock, a copy) and writes an eventfd this file has added to
 *            the event loop, so the dispatch returns as soon as a frame is
 *            there; the main loop copies it out and calls present(), which
 *            turns it into a wlr_buffer for the scene.
 *   input    the seat's events meant for Family mruby go into the main-
 *            thread ring of native_input.h (the SDL window's: one writer,
 *            atomics, no lock); the machine's input task drains it.
 *   windows  comp_link.h, with Family mruby's side in wl_bridge.c: notices
 *            of the Linux windows out (appeared, retitled, asked to move or
 *            resize, gone), places, stacking, focus and close requests in.
 *            The same eventfd wakes this loop for them.
 *
 * Scene, bottom to top: black over the whole output; the Linux windows,
 * each where Family mruby's stand-in for it has its content (the content
 * rectangle times the picture's scale), clipped to it, in Family mruby's
 * stacking; the picture at the largest whole scale (nearest-neighbour, by
 * the renderer), transparent where a stand-in's content is on screen (the
 * holes, comp_route.h); the popups of the Linux windows; the IME's; the
 * screen lock (L13), over everything.
 * A Family mruby window or menu over a stand-in covers the Linux window
 * there, so the two kinds of windows stack freely.
 *
 * The screen (L9d): with screen.form "output" the frame is the output over
 * display.scale (main_native.c asks wlr_size once the output is up), so it
 * fills the output at that scale. A fullscreen app smaller than the frame
 * (Modern's 426x240) is shown alone: Family mruby's side names its
 * rectangle (the view, comp_link.h), and only that part of the picture is
 * laid on the output at its own largest whole scale (the scene buffer's
 * source box), the Linux windows hidden as under any fullscreen app.
 * display.mode sets the output's mode (a custom one on the headless and
 * nested backends, for the tests and a window of a chosen size).
 *
 * Pointer: over a hole (or a popup) it goes to that Linux window at the
 * output's own resolution, with wlroots' cursor (Family mruby's is hidden);
 * a press there also goes to Family mruby, whose window manager raises and
 * focuses the stand-in as for any click. Anywhere else it goes to Family
 * mruby. Keys go where Family mruby's focus is (a stand-in's: its Linux
 * window), except Ctrl+Tab / Ctrl+comma, which are always Family mruby's
 * (switching windows). The windows' keymap follows keyboard_layout.
 *
 * Dialogs (L9c): a window with a parent (xdg set_parent, X11
 * WM_TRANSIENT_FOR) is a Linux window like the others, with its stand-in;
 * the notice of it names the parent, and Family mruby's side puts it in the
 * middle of the parent's and keeps it above (wl_bridge.c). Menus and
 * tooltips (xdg popups, X11 override-redirect windows) have no stand-in and
 * lie over the picture, but never outside its rectangle: an xdg popup is
 * unconstrained against it (its positioner's slide, flip and resize), cut
 * at its edge if it still reaches out; an X11 menu is moved into it. A
 * button pressed anywhere but on a window's popups closes them (popup_done),
 * as a click outside a menu does on any desktop.
 *
 * Linux windows are told to draw no decoration of their own
 * (xdg-decoration, server side): the stand-in draws the frame. Move and
 * resize requests become moves and resizes of the stand-in.
 *
 * The PC's windows and screen (L13):
 *   maximize    a window's request (xdg, X11 _NET_WM_STATE) is passed to
 *               Family mruby's side, whose window operations
 *               (native_winops.h: also a title's double-click and
 *               Super+arrows, which wl_input.c takes from the keyboard
 *               whoever has it) lay the stand-in out; each window is told
 *               how it is laid out (maximized, tiled left / right).
 *               Fullscreen requests are answered without a change.
 *   lock        ext-session-lock: see wl_screen_lock.c. The lock
 *               program (compositor.locker) is started by the power menu's
 *               Lock and by logind's Lock and PrepareForSleep
 *               (native/sys/linux_logind.c).
 *
 * The clipboard (L12a, wl_clip.c): the machine's clipboard is the seat's
 * selection, both ways -- what a Family mruby app copies becomes the
 * selection (the compositor its owner), and a selection a Linux program sets
 * (X11 ones too, through Xwayland's bridge) is read and handed to Family
 * mruby, never waiting. The primary selection is not served.
 *
 * Also served: wlr-screencopy and xdg-output, so grim takes the composited
 * screen; ext-image-copy-capture with the output capture source, so the
 * desktop portal's wlroots backend shares the screen (L14, a browser's
 * meeting, a recorder), the portal told where the screen is
 * (activation_env); and requests on the control socket ("comp ...", see
 * wlr_ctl in wl_tools.c) that drive a keyboard and a pointer of the tools'
 * own through the same handlers as real devices, and report sizes and
 * timings.
 *
 * X11 programs (L9b): Xwayland, started by wlroots when the first X11
 * program connects (lazy), never at start. Its windows are Linux windows
 * like the xdg ones: the same numbering in comp_route, a stand-in each,
 * placed with wlr_xwayland_surface_configure (at their place on the output,
 * so their menus land where the program means them). Every X11 window gets
 * the stand-in's frame, whatever _MOTIF_WM_HINTS says: one that draws its
 * own title bar shows two (logged with its decorations), since without the
 * frame it could not be moved or closed the Family mruby way. Override-
 * redirect windows (X11 menus, tooltips) go into the popup layer, over the
 * picture, at their own place. No Xwayland installed: one line says X11
 * programs cannot run, DISPLAY is unset, the rest goes on.
 *
 * A window that settles on a size of its own (foot to its cell grid, an X11
 * program asking for one) is told to Family mruby (COMP_NOTICE_FIT), whose
 * stand-in then fits it; what the scale still leaves between the two (under
 * one frame pixel) shows the stand-in's frame colour (the backdrop under
 * each window), not black.
 *
 * compositor.autostart, when set, is started once through /bin/sh -c in a
 * session of its own with WAYLAND_DISPLAY naming this compositor (also set
 * in the program's environment, so the Shell's "!" programs inherit it, and
 * DISPLAY naming Xwayland's). The launcher's Linux applications
 * (native/sys/linux_apps.h, L9b) are started from this thread too, and give
 * their windows' stand-ins the entries' names.
 *
 * On a console (the login screen's session) the backend takes the seat
 * through logind or seatd (libseat); Ctrl+Alt+F1..F12 switch virtual
 * terminals. display.rotate turns the output (its transform), the pointer
 * with it.
 *
 * The end (close): every Linux window is asked to close, and the loop goes
 * on -- Family mruby still on screen -- until they have all gone or
 * LINGER_MS has passed, so a program can ask about unsaved work. Then the
 * clients still connected (and the X11 programs) get SIGTERM, and SIGKILL
 * half a second later; then whatever the launcher started that is still
 * there, windowless or not; the autostart's session last.
 *
 * The files (L16): this one has the state, the output, the autostart and
 * the output interface; wl_window.c the windows and the focus, wl_picture.c
 * the picture and the layout, wl_xwayland.c the X11 windows, wl_input.c the
 * keyboard and the pointer, wl_screen_lock.c the screen lock, wl_tools.c the
 * tools' devices and wlr_ctl. What they share: output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

/* ------------------------------------------------------------- state */

struct output_wlr_state s;

uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

const char *renderer_name(void)
{
    if (s.renderer == NULL) return "none";
    if (wlr_renderer_is_pixman(s.renderer)) return "pixman";
    if (wlr_renderer_is_gles2(s.renderer)) return "gles2";
    if (wlr_renderer_is_vk(s.renderer)) return "vulkan";
    return "other";
}

window_t *window_by_id(int id)
{
    if (id <= 0) return NULL;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->id == id) return w;
    }
    return NULL;
}

/* The id of the window an xdg toplevel is, 0 when it has none (not mapped). */
int toplevel_id(const struct wlr_xdg_toplevel *t)
{
    if (t == NULL) return 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->kind == WIN_XDG && w->toplevel == t) return w->mapped ? w->id : 0;
    }
    return 0;
}

/* ------------------------------------------------------------- output */

static void output_frame(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    if (s.scene_output == NULL) return;
    if (wlr_scene_output_commit(s.scene_output, NULL)) s.stats.commits++;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wlr_scene_output_send_frame_done(s.scene_output, &ts);
}

static void output_request_state(struct wl_listener *l, void *data)
{
    (void)l;
    const struct wlr_output_event_request_state *ev = data;
    wlr_output_commit_state(ev->output, ev->state);
    relayout();
}

static void output_destroy(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wl_list_remove(&s.output_frame.link);
    wl_list_remove(&s.output_request_state.link);
    wl_list_remove(&s.output_destroy.link);
    s.output = NULL;
    s.scene_output = NULL;
    /* The nested window closed (or the display went): the session ends. */
    fprintf(stderr, "native: compositor: the output went away; quitting\n");
    s.quit = true;
}

static void new_output(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_output *out = data;
    if (s.output != NULL) {
        fprintf(stderr, "native: compositor: output %s not used (one output in L8)\n", out->name);
        return;
    }
    wlr_output_init_render(out, s.allocator, s.renderer);
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_enabled(&st, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(out);
    if (s.mode_w > 0) {
        /* display.mode: that mode when the output lists it, else a custom
         * one (the headless and nested outputs take any size). */
        struct wlr_output_mode *m, *pick = NULL;
        wl_list_for_each(m, &out->modes, link) {
            if (m->width == s.mode_w && m->height == s.mode_h &&
                (s.mode_hz == 0 || (m->refresh + 500) / 1000 == s.mode_hz)) {
                pick = m;
                break;
            }
        }
        if (pick != NULL) wlr_output_state_set_mode(&st, pick);
        else wlr_output_state_set_custom_mode(&st, s.mode_w, s.mode_h, s.mode_hz * 1000);
    } else if (mode != NULL) {
        wlr_output_state_set_mode(&st, mode);
    }
    /* display.rotate turns the output clockwise, as wlr-randr --transform
     * did in the cage session (L7); the pointer turns with it. */
    static const enum wl_output_transform turns[] = { WL_OUTPUT_TRANSFORM_NORMAL, WL_OUTPUT_TRANSFORM_90,
                                                      WL_OUTPUT_TRANSFORM_180, WL_OUTPUT_TRANSFORM_270 };
    int turn = (s.rotate / 90) & 3;
    if (turn != 0) wlr_output_state_set_transform(&st, turns[turn]);
    wlr_output_commit_state(out, &st);
    wlr_output_state_finish(&st);

    s.output = out;
    s.output_frame.notify = output_frame;
    wl_signal_add(&out->events.frame, &s.output_frame);
    s.output_request_state.notify = output_request_state;
    wl_signal_add(&out->events.request_state, &s.output_request_state);
    s.output_destroy.notify = output_destroy;
    wl_signal_add(&out->events.destroy, &s.output_destroy);

    struct wlr_output_layout_output *lo = wlr_output_layout_add_auto(s.layout, out);
    s.scene_output = wlr_scene_output_create(s.scene, out);
    wlr_scene_output_layout_add_output(s.scene_layout, lo, s.scene_output);
    fprintf(stderr, "native: compositor: output %s %dx%d, rotate %d\n", out->name, out->width, out->height,
            s.rotate);
    relayout();
}

/* ----------------------------------------------------- the autostart */

static void start_child(void)
{
    if (s.autostart[0] == '\0') return;
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "native: compositor: autostart: fork: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", s.autostart, (char *)NULL);
        _exit(127);
    }
    s.child = pid;
    fprintf(stderr, "native: compositor: started \"%s\" (pid %d) on %s\n", s.autostart, (int)pid, s.socket);
}

static void reap_child(void)
{
    if (s.child <= 0) return;
    int st = 0;
    if (waitpid(s.child, &st, WNOHANG) == s.child) {
        fprintf(stderr, "native: compositor: \"%s\" ended (%d)\n", s.autostart,
                WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
        s.child = 0;
    }
}

static void stop_child(void)
{
    if (s.child <= 0) return;
    kill(-s.child, SIGTERM);
    for (int i = 0; i < 50; i++) {
        if (waitpid(s.child, NULL, WNOHANG) == s.child) {
            fprintf(stderr, "native: compositor: \"%s\" stopped with the program\n", s.autostart);
            s.child = 0;
            return;
        }
        usleep(10 * 1000);
    }
    kill(-s.child, SIGKILL);
    waitpid(s.child, NULL, 0);
    fprintf(stderr, "native: compositor: \"%s\" killed with the program\n", s.autostart);
    s.child = 0;
}

/* ------------------------------------------------- the output interface */

static int on_wake(int fd, uint32_t mask, void *data)
{
    (void)mask;
    (void)data;
    uint64_t v;
    ssize_t n = read(fd, &v, sizeof(v));
    (void)n;
    return 0;
}

/* The session's programs started by D-Bus or systemd (the desktop portal
 * and its wlroots backend, L14: a browser's screen sharing) learn where
 * the screen is: WAYLAND_DISPLAY (and DISPLAY, Xwayland's) and the desktop
 * name the portal picks its backends by (family-mruby-portals.conf). On
 * the console only (the login screen's session), or when the checks ask
 * (FMRB_COMPOSITOR_ACTIVATION_ENV=1): nested in another desktop, its
 * session's environment is that desktop's, not ours to change. The tool
 * runs on a thread of its own and is waited for there. */
static void *activation_env_run(void *arg)
{
    char **argv = arg;
    pid_t pid = 0;
    extern char **environ;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    int status = 0;
    if (rc == 0) waitpid(pid, &status, 0);
    fprintf(stderr, "native: compositor: the session's activation environment: %s%s\n",
            rc != 0 ? "dbus-update-activation-environment is missing" :
            (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? "WAYLAND_DISPLAY and XDG_CURRENT_DESKTOP" : "not updated",
            rc == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && getenv("DISPLAY") ? " (and DISPLAY)" : "");
    for (char **a = argv; *a; a++) free(*a);
    free(argv);
    return NULL;
}

static void activation_env(void)
{
    const char *ask = getenv("FMRB_COMPOSITOR_ACTIVATION_ENV");
    if (s.session == NULL && !(ask != NULL && strcmp(ask, "1") == 0)) return;
    char **argv = calloc(8, sizeof(char *));
    if (argv == NULL) return;
    int n = 0;
    char buf[160];
    argv[n++] = strdup("dbus-update-activation-environment");
    argv[n++] = strdup("--systemd");
    snprintf(buf, sizeof(buf), "WAYLAND_DISPLAY=%s", s.socket);
    argv[n++] = strdup(buf);
    snprintf(buf, sizeof(buf), "XDG_CURRENT_DESKTOP=%s", DESKTOP_NAME);
    argv[n++] = strdup(buf);
    const char *d = getenv("DISPLAY");
    if (d != NULL && d[0] != '\0') {
        snprintf(buf, sizeof(buf), "DISPLAY=%s", d);
        argv[n++] = strdup(buf);
    }
    argv[n] = NULL;
    pthread_t t;
    if (pthread_create(&t, NULL, activation_env_run, argv) == 0) {
        pthread_detach(t);
    } else {
        for (char **a = argv; *a; a++) free(*a);
        free(argv);
    }
}

/* Qt programs look as they do in Plasma (L9e). Qt picks its platform theme
 * from XDG_CURRENT_DESKTOP, which is ours (family-mruby: the launcher's
 * OnlyShowIn / NotShowIn rules and the programs' own see that name), so it
 * never picks Plasma's. Where Plasma's is installed and nothing chose
 * otherwise, it is named outright for every program started from here (the
 * launcher, the autostart, the Shell's "!", Xwayland's). */
static void qt_look(void)
{
    const char *have = getenv("QT_QPA_PLATFORMTHEME");
    if (have != NULL && have[0] != '\0') {
        fprintf(stderr, "native: compositor: Qt's look: QT_QPA_PLATFORMTHEME=%s (as set)\n", have);
        return;
    }
    char found[512];
    if (linux_apps_plasma_qt("", found, sizeof(found))) {
        setenv("QT_QPA_PLATFORMTHEME", "kde", 1);
        fprintf(stderr, "native: compositor: Qt's look: Plasma's (QT_QPA_PLATFORMTHEME=kde; %s)\n", found);
    }
}

/* The clipboard's wake (native_clip.h): from the task that copied. */
static void clip_wake(void)
{
    int fd = s.wake_fd;
    if (fd < 0) return;
    uint64_t one = 1;
    ssize_t n = write(fd, &one, sizeof(one));
    (void)n;
}

static bool wlr_open(const native_machine_t *m, char *why, size_t why_len)
{
    memset(&s, 0, sizeof(s));
    s.wake_fd = -1;
    s.layout_kb = -1;
    comp_route_init(&s.route);
    wl_list_init(&s.windows);
    wl_list_init(&s.popups);
    wl_list_init(&s.keyboards);
    snprintf(s.autostart, sizeof(s.autostart), "%s", m->compositor.autostart);
    s.rotate = m->display.rotate;
    if (strcmp(m->display.mode, "auto") != 0 &&
        sscanf(m->display.mode, "%dx%d@%d", &s.mode_w, &s.mode_h, &s.mode_hz) < 2) {
        snprintf(why, why_len, "display.mode \"%s\" is not auto, WxH or WxH@Hz", m->display.mode);
        return false;
    }
    s.stats.since_ms = now_ms();
    if (getenv("XDG_RUNTIME_DIR") == NULL) {
        snprintf(why, why_len, "XDG_RUNTIME_DIR is not set (the compositor's socket goes there)");
        return false;
    }
    wlr_log_init(getenv("FMRB_WLR_DEBUG") ? WLR_DEBUG : WLR_ERROR, NULL);

    s.display = wl_display_create();
    s.loop = wl_display_get_event_loop(s.display);
    /* Nested in another compositor when WAYLAND_DISPLAY or DISPLAY is set,
     * DRM/KMS on a console; WLR_BACKENDS chooses (headless for the tests). */
    s.backend = wlr_backend_autocreate(s.loop, &s.session);
    if (s.backend == NULL) {
        snprintf(why, why_len, "no wlroots backend (WAYLAND_DISPLAY=%s, WLR_BACKENDS=%s; FMRB_WLR_DEBUG=1 says why)",
                 getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "",
                 getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "");
        return false;
    }
    s.renderer = wlr_renderer_autocreate(s.backend);
    if (s.renderer == NULL) {
        snprintf(why, why_len, "no wlroots renderer (WLR_RENDERER=pixman works without a GPU)");
        return false;
    }
    wlr_renderer_init_wl_display(s.renderer, s.display);
    s.allocator = wlr_allocator_autocreate(s.backend, s.renderer);
    if (s.allocator == NULL) {
        snprintf(why, why_len, "no wlroots allocator");
        return false;
    }
    s.compositor = wlr_compositor_create(s.display, 5, s.renderer);
    wlr_subcompositor_create(s.display);
    wlr_data_device_manager_create(s.display);

    s.layout = wlr_output_layout_create(s.display);
    s.scene = wlr_scene_create();
    s.scene_layout = wlr_scene_attach_output_layout(s.scene, s.layout);
    static const float black[4] = { 0, 0, 0, 1 };
    s.black = wlr_scene_rect_create(&s.scene->tree, 0, 0, black);
    /* The Linux windows under the picture, their popups over it. */
    s.windows_tree = wlr_scene_tree_create(&s.scene->tree);
    s.picture = wlr_scene_buffer_create(&s.scene->tree, NULL);
    wlr_scene_buffer_set_filter_mode(s.picture, WLR_SCALE_FILTER_NEAREST);
    s.popups_tree = wlr_scene_tree_create(&s.scene->tree);
    /* The IME's candidates and Family mruby's composing text over all. */
    s.ime_tree = wlr_scene_tree_create(&s.scene->tree);
    s.ime = strcmp(m->input.ime, "wayland") == 0;

    s.new_output.notify = new_output;
    wl_signal_add(&s.backend->events.new_output, &s.new_output);
    /* Version 5: the window is told which window-manager features there
     * are (maximize, L13; no fullscreen, minimize or window menu). */
    s.xdg_shell = wlr_xdg_shell_create(s.display, 5);
    s.new_toplevel.notify = new_toplevel;
    wl_signal_add(&s.xdg_shell->events.new_toplevel, &s.new_toplevel);
    s.new_popup.notify = new_popup;
    wl_signal_add(&s.xdg_shell->events.new_popup, &s.new_popup);
    s.deco_mgr = wlr_xdg_decoration_manager_v1_create(s.display);
    s.new_decoration.notify = new_decoration;
    wl_signal_add(&s.deco_mgr->events.new_toplevel_decoration, &s.new_decoration);

    s.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s.cursor, s.layout);
    s.xcursor = wlr_xcursor_manager_create(NULL, 24);
    s.cursor_motion.notify = cursor_motion;
    wl_signal_add(&s.cursor->events.motion, &s.cursor_motion);
    s.cursor_motion_absolute.notify = cursor_motion_absolute;
    wl_signal_add(&s.cursor->events.motion_absolute, &s.cursor_motion_absolute);
    s.cursor_button.notify = cursor_button;
    wl_signal_add(&s.cursor->events.button, &s.cursor_button);
    s.cursor_axis.notify = cursor_axis;
    wl_signal_add(&s.cursor->events.axis, &s.cursor_axis);
    s.cursor_frame.notify = cursor_frame;
    wl_signal_add(&s.cursor->events.frame, &s.cursor_frame);

    s.new_input.notify = new_input;
    wl_signal_add(&s.backend->events.new_input, &s.new_input);
    s.seat = wlr_seat_create(s.display, "seat0");
    s.request_cursor.notify = request_cursor;
    wl_signal_add(&s.seat->events.request_set_cursor, &s.request_cursor);
    s.request_set_selection.notify = request_set_selection;
    wl_signal_add(&s.seat->events.request_set_selection, &s.request_set_selection);
    tool_devices_init();
    if (s.ime) {
        static const wl_ime_host_t ime_host = { .surface_origin = surface_origin, .add_keyboard = add_ime_keyboard };
        wl_ime_init(s.display, s.seat, s.ime_tree, &s.route, &ime_host);
    }

    /* The screen lock over everything (L13). */
    lock_init(m);
    wlr_screencopy_manager_v1_create(s.display);
    /* The screen's capture for xdg-desktop-portal-wlr (L14: sharing the
     * screen from a browser's meeting, a recorder): it takes an output
     * through ext-image-copy-capture, or wlr-screencopy only with dmabuf,
     * which the pixman renderer (a virtual machine) does not give. */
    wlr_ext_image_copy_capture_manager_v1_create(s.display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(s.display, 1);
    wlr_xdg_output_manager_v1_create(s.display, s.layout);

    s.socket = wl_display_add_socket_auto(s.display);
    if (s.socket == NULL) {
        snprintf(why, why_len, "cannot make a Wayland socket in %s", getenv("XDG_RUNTIME_DIR"));
        return false;
    }
    if (!wlr_backend_start(s.backend)) {
        snprintf(why, why_len, "the wlroots backend did not start");
        return false;
    }
    /* The parent compositor (nested) is connected by now: from here on the
     * program's own children, and the Shell's "!" programs, see this one
     * (and this one's Xwayland, never the parent's X server). */
    setenv("WAYLAND_DISPLAY", s.socket, 1);
    qt_look();
    xwayland_init();
    if (s.ime) wl_ime_start(m->compositor.ime, s.socket);
    activation_env();

    s.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (s.wake_fd >= 0) {
        s.wake_source = wl_event_loop_add_fd(s.loop, s.wake_fd, WL_EVENT_READABLE, on_wake, NULL);
        native_frame_set_wake_fd(s.wake_fd);
    }
    comp_link_attach(&s.route, s.wake_fd);
    wl_clip_init(s.display, s.loop, s.seat);
    native_clip_install(clip_wake);
    fprintf(stderr, "native: compositor: wlroots %s on WAYLAND_DISPLAY=%s (backends %s, renderer %s%s)\n",
            WLR_VERSION_STR, s.socket, getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "auto",
            renderer_name(), s.session != NULL ? ", seat from the session" : "");
    /* The launcher's Linux applications (L9b): icons into the cache the
     * file namespace shows at LINUX_ICON_DIR. */
    char icon_dir[512];
    if (fmrb_hal_file_resolve_path(LINUX_ICON_DIR, icon_dir, sizeof(icon_dir)) == FMRB_OK) {
        linux_apps_config_t ac = { .icon_dir = icon_dir,
                                   .icon_fm_dir = LINUX_ICON_DIR,
                                   .desktop_name = DESKTOP_NAME,
                                   .mime_desktops = m->compositor.mime_desktops };
        linux_apps_start(&ac);
    } else {
        fprintf(stderr, "native: compositor: no place for the Linux icons (%s); no Linux apps\n", LINUX_ICON_DIR);
    }
    start_child();
    /* logind's Lock and PrepareForSleep lock the screen (L13). */
    if (s.lk.available) linux_logind_watch_start();
    return true;
}

/* The output as the picture sees it (turned by display.rotate): the size the
 * screen form "output" divides (main_native.c). */
static bool wlr_size(int *w, int *h)
{
    if (s.output == NULL) return false;
    wlr_output_effective_resolution(s.output, w, h);
    return *w > 0 && *h > 0;
}

static void take_base_size(void)
{
    if (s.route.base_w > 0) return;
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    if (bw <= 0 || bh <= 0) return;
    comp_route_set_base(&s.route, bw, bh);
    comp_link_set_geometry(s.route.place.scale, bw, bh);
}

static bool wlr_pump(int wait_ms)
{
    wl_display_flush_clients(s.display);
    wl_event_loop_dispatch(s.loop, wait_ms);
    take_base_size();
    follow_layout();
    take_from_fmrb();
    wl_clip_pump();
    wl_ime_pump();
    linux_apps_pump();
    wl_display_flush_clients(s.display);
    flush_motion(false);
    reap_child();
    lock_reap();
    return s.quit;
}

static void wlr_present(const uint16_t *px, int w, int h, bool changed)
{
    if (s.picture == NULL) return;
    s.frame = px;
    comp_route_set_frame(&s.route, px);
    bool relaid = false;
    if (w != s.route.fb_w || h != s.route.fb_h) {
        int ow = s.route.out_w, oh = s.route.out_h;
        relaid = comp_route_layout(&s.route, ow, oh, w, h);
    }
    if (changed) {
        /* The part of the frame to show came with it (L9e): the view
         * changes with the frame that needs it, not a frame later. */
        int v[4];
        native_frame_copied_view(v);
        if (comp_route_set_view(&s.route, v[0], v[1], v[2], v[3])) {
            relaid = true;
            s.stats.view_changes++;
        }
    }
    if (relaid) apply_layout();
    if (!changed && !s.recompose) return;
    compose();
    if (!changed) return;
    /* The backdrops take their colour from the frame, which may have come
     * after the place it was read for (a window moved over where a hole had
     * been): read again from each new frame. */
    window_t *bw;
    wl_list_for_each(bw, &s.windows, link) {
        const comp_window_t *cw = comp_route_window(&s.route, bw->id);
        if (cw != NULL && cw->placed && bw->backdrop != NULL) set_backdrop_colour(bw, cw);
    }
    recheck_pointer();
    uint64_t t = now_ms();
    if (s.stats.pictures > 0) {
        uint64_t gap = t - s.stats.last_picture_ms;
        s.stats.picture_gap_sum_ms += gap;
        if (gap > s.stats.picture_gap_max_ms) s.stats.picture_gap_max_ms = gap;
    }
    s.stats.last_picture_ms = t;
    s.stats.pictures++;
}

/* The windows a close is asked of and waited for: every xdg toplevel, and
 * the X11 windows that are on screen (not menus). */
static bool closable(const window_t *w)
{
    if (w->kind == WIN_XDG) return w->toplevel->base->initialized;
    return w->mapped && !is_or(w);
}

static int window_count(void)
{
    int n = 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) n += w->kind == WIN_XDG || closable(w);
    return n;
}

/* Xwayland's own connection: not a program to wait for or signal (it ends
 * with wlr_xwayland_destroy, after its X11 programs). */
static bool is_xwayland(struct wl_client *c)
{
#if WLR_HAS_XWAYLAND
    return s.xwayland != NULL && s.xwayland->server != NULL && s.xwayland->server->client == c;
#else
    (void)c;
    return false;
#endif
}

static int client_count(void)
{
    int n = 0;
    struct wl_list *clients = wl_display_get_client_list(s.display);
    for (struct wl_list *it = clients->next; it != clients; it = it->next) {
        n += !is_xwayland(wl_client_from_link(it));
    }
    return n;
}

/* Signal every client process still connected (Xwayland among them), and
 * the X11 programs whose windows are still there. */
static int signal_clients(int sig)
{
    int n = 0;
#if WLR_HAS_XWAYLAND
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->kind == WIN_X11 && w->xs->pid > 0 && w->xs->pid != getpid() && kill(w->xs->pid, sig) == 0) n++;
    }
#endif
    struct wl_list *clients = wl_display_get_client_list(s.display);
    for (struct wl_list *it = clients->next; it != clients; it = it->next) {
        struct wl_client *c = wl_client_from_link(it);
        if (is_xwayland(c)) continue;
        pid_t pid = 0;
        wl_client_get_credentials(c, &pid, NULL, NULL);
        if (pid > 0 && pid != getpid()) {
            kill(pid, sig);
            n++;
        }
    }
    return n;
}

/* The end, step one: ask every Linux window to close and keep going --
 * Family mruby's picture still on screen, so a program can ask about
 * unsaved work and be answered -- until they have gone or LINGER_MS. The
 * control socket is still answered meanwhile (a shot shows the picture; a
 * quit ends the wait at once). */
static void linger(void)
{
    int asked = 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (closable(w)) {
            win_close(w);
            asked++;
        }
    }
    if (asked == 0) {
        /* The main loop's copy is gone: nothing may read it any more. */
        s.frame = NULL;
        comp_route_set_frame(&s.route, NULL);
        return;
    }
    fprintf(stderr, "native: compositor: asked %d window%s to close\n", asked, asked == 1 ? "" : "s");
    /* A copy of the frame of its own: the main loop's is gone by now. As
     * large as the frame can be (the base screen, which may be larger than
     * the high-resolution one since L9d), and taken at once, so the picture
     * never points at the main loop's copy. */
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    int cap = bw * bh > 640 * 360 ? bw * bh : 640 * 360;
    uint16_t *frame = malloc((size_t)cap * sizeof(uint16_t));
    uint64_t t0 = now_ms();
    uint64_t closed_at = 0;
    uint32_t shown = 0;
    int fw0 = 0, fh0 = 0;
    if (frame != NULL && native_frame_copy(frame, cap, &fw0, &fh0, &shown)) {
        wlr_present(frame, fw0, fh0, true);
    } else {
        s.frame = NULL;
        comp_route_set_frame(&s.route, NULL);
    }
    while (now_ms() - t0 < LINGER_MS && s.output != NULL) {
        /* Once the windows have gone, a moment more for their programs to
         * leave on their own. */
        if (window_count() == 0) {
            if (closed_at == 0) closed_at = now_ms();
            if (client_count() == 0 || now_ms() - closed_at >= CLIENT_LEAVE_MS) break;
        }
        wl_display_flush_clients(s.display);
        wl_event_loop_dispatch(s.loop, 5);
        take_from_fmrb();
        linux_apps_pump();
        wl_display_flush_clients(s.display);
        reap_child();
        if (native_ctl_poll()) {
            fprintf(stderr, "native: compositor: quit asked for while waiting for the windows: not waiting\n");
            break;
        }
        if (frame != NULL && native_frame_seq() != shown) {
            int fw = 0, fh = 0;
            if (native_frame_copy(frame, cap, &fw, &fh, &shown)) wlr_present(frame, fw, fh, true);
        } else if (frame != NULL && s.frame == frame && s.recompose && s.route.fb_w > 0) {
            wlr_present(frame, s.route.fb_w, s.route.fb_h, false);
        }
    }
    int left = window_count();
    fprintf(stderr, "native: compositor: %s after %llu ms (%d client%s still connected)\n",
            left == 0 ? "every window closed" : "windows still open", (unsigned long long)(now_ms() - t0),
            client_count(), client_count() == 1 ? "" : "s");
    /* The picture keeps pointing at this copy until the scene goes; it is
     * not freed (the program ends right after). */
    s.frame = NULL;
}

/* Step two: the clients still connected are ended. */
/* The X11 programs that can still be told apart (their windows give a
 * pid), which end when signalled or when Xwayland goes. */
static int x11_programs(void)
{
    int n = 0;
#if WLR_HAS_XWAYLAND
    window_t *w;
    wl_list_for_each(w, &s.windows, link) n += w->kind == WIN_X11 && w->xs->pid > 0;
#endif
    return n;
}

static bool anyone_left(void)
{
    return client_count() > 0 || x11_programs() > 0;
}

static void end_clients(void)
{
    if (!anyone_left()) return;
    int n = signal_clients(SIGTERM);
    fprintf(stderr, "native: compositor: SIGTERM to %d client%s still connected\n", n, n == 1 ? "" : "s");
    uint64_t t0 = now_ms();
    while (anyone_left() && now_ms() - t0 < TERM_GRACE_MS) {
        wl_event_loop_dispatch(s.loop, 5);
        wl_display_flush_clients(s.display);
    }
    if (anyone_left()) {
        n = signal_clients(SIGKILL);
        fprintf(stderr, "native: compositor: SIGKILL to %d client%s\n", n, n == 1 ? "" : "s");
        t0 = now_ms();
        while (anyone_left() && now_ms() - t0 < TERM_GRACE_MS) {
            wl_event_loop_dispatch(s.loop, 5);
        }
    }
}

static void wlr_close(void)
{
    if (s.display == NULL) return;
    linux_logind_watch_stop();
    linger();
    wl_clip_fini();
    wl_ime_fini();
    end_clients();
    /* What the launcher started and is still there, with a window or not. */
    int left = linux_apps_end_all(TERM_GRACE_MS);
    if (left > 0) {
        fprintf(stderr, "native: compositor: ended %d process%s started from the launcher\n", left,
                left == 1 ? "" : "es");
    }
#if WLR_HAS_XWAYLAND
    if (s.xwayland != NULL) {
        /* wlroots wants its signals free of listeners by then. */
        wl_list_remove(&s.xw_new_surface.link);
        wl_list_remove(&s.xw_ready.link);
        wlr_xwayland_destroy(s.xwayland);
        s.xwayland = NULL;
    }
#endif
    comp_link_detach();
    native_frame_set_wake_fd(-1);
    stop_child();
    wl_display_destroy_clients(s.display);
    if (s.wake_source != NULL) wl_event_source_remove(s.wake_source);
    if (s.wake_fd >= 0) close(s.wake_fd);
    /* Not torn down further: the program _exits right after, and the
     * machine's threads may still be mid-frame. */
    s.display = NULL;
}

const native_output_t native_output_wlroots = {
    .name    = "wlroots",
    .open    = wlr_open,
    .pump    = wlr_pump,
    .present = wlr_present,
    .close   = wlr_close,
    .ctl     = wlr_ctl,
    .size    = wlr_size,
};
