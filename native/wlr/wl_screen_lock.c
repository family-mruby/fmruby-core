/*
 * wl_screen_lock.c - the compositor's screen lock (ext-session-lock, L13).
 * Split from output_wlr.c (L16), which has the overview; what the files
 * share is in output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

/* ------------------------------------------------- the screen lock (L13) */

/* ext-session-lock-v1: a lock program (swaylock, compositor.locker) asks to
 * lock; the screen is covered at once (a plain colour), and the program's
 * lock surface goes over it. While locked, nothing reaches Family mruby or a
 * Linux window: keys and the pointer go to the lock surface alone (the
 * virtual-terminal switch stays the machine's). The
 * program unlocks when it is satisfied (it checks the password; Family mruby
 * never sees one). A program that goes away without unlocking leaves the
 * screen locked, as the protocol says: the cover stays, and a key (or
 * logind's Lock, or the power menu's) starts the program again. */

/* The used output's lock surface takes the keyboard and the pointer (the
 * others are not shown). */
static bool lock_primary(const lock_surface_t *ls)
{
    return ls->wlr->output == s.output;
}

static void lock_cover_size(void)
{
    if (s.lk.cover == NULL) return;
    struct wlr_box box;
    wlr_output_layout_get_box(s.layout, NULL, &box);
    wlr_scene_node_set_position(&s.lk.cover->node, box.x, box.y);
    wlr_scene_rect_set_size(s.lk.cover, box.width, box.height);
}

void lock_relayout(void)
{
    lock_cover_size();
    lock_surface_t *ls;
    wl_list_for_each(ls, &s.lk.surfaces, link) {
        int ew = 0, eh = 0;
        if (ls->wlr->output != NULL) wlr_output_effective_resolution(ls->wlr->output, &ew, &eh);
        if (ew > 0 && eh > 0) wlr_session_lock_surface_v1_configure(ls->wlr, (uint32_t)ew, (uint32_t)eh);
    }
}

static void lock_publish(void)
{
    comp_lock_state_t st = s.lk.locked ? COMP_LOCK_LOCKED : (s.lk.awaiting ? COMP_LOCK_LOCKING : COMP_LOCK_NONE);
    comp_link_set_lock(s.lk.available, st);
}

/* Locked or not: the cover, and the keyboard and the pointer taken from (or
 * given back to) Family mruby and the windows. */
static void lock_set(bool on)
{
    if (on == s.lk.locked) return;
    s.lk.locked = on;
    wlr_scene_node_set_enabled(&s.lock_tree->node, on);
    if (on) {
        lock_cover_size();
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        wlr_seat_pointer_clear_focus(s.seat);
        wl_ime_focus(NULL, false);
        s.buttons_held = 0;
        s.pointer_target = COMP_FMRB;
        s.grab.mode = GRAB_NONE;
        s.motion_pending = false;
        wlr_cursor_set_xcursor(s.cursor, s.xcursor, "default");
        fmrb_cursor(false);
    } else {
        s.lk.focus = NULL;
        apply_focus();
        process_motion((uint32_t)now_ms());
    }
    lock_publish();
}

static void lock_focus(struct wlr_surface *surf)
{
    s.lk.focus = surf;
    if (surf == NULL) {
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        return;
    }
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s.seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(s.seat, surf, kb->keycodes, kb->num_keycodes, &kb->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(s.seat, surf, NULL, 0, NULL);
    }
}

/* Whether the first word of a command is a program found on PATH. */
static bool program_found(const char *cmd)
{
    char first[256];
    if (sscanf(cmd, "%255s", first) != 1) return false;
    if (strchr(first, '/') != NULL) return access(first, X_OK) == 0;
    const char *path = getenv("PATH");
    if (path == NULL) path = "/usr/local/bin:/usr/bin:/bin";
    while (*path) {
        size_t n = strcspn(path, ":");
        if (n > 0 && n < 512) {
            char full[800];
            snprintf(full, sizeof(full), "%.*s/%s", (int)n, path, first);
            if (access(full, X_OK) == 0) return true;
        }
        path += n;
        if (*path == ':') path++;
    }
    return false;
}

/* Start the lock program (compositor.locker), unless it runs. */
void lock_start(const char *why)
{
    if (!s.lk.available) {
        fprintf(stderr, "native: compositor: lock asked (%s), but there is no lock program (%s)\n", why,
                s.lk.program[0] ? s.lk.program : "compositor.locker is empty");
        return;
    }
    if (s.lk.pid > 0) return;
    if (s.lk.locked && s.lk.lock != NULL) return;
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "native: compositor: lock program: fork: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* Async-signal-safe calls only: signals as new, a session of its
         * own, none of the program's descriptors (the control socket's
         * client would wait for this child to close it). */
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (int sig = 1; sig < NSIG; sig++) signal(sig, SIG_DFL);
        setsid();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        for (int fd = 3; fd < 4096; fd++) close(fd);
        execl("/bin/sh", "sh", "-c", s.lk.program, (char *)NULL);
        _exit(127);
    }
    s.lk.pid = pid;
    s.lk.awaiting = true;
    s.lk.started_ms = now_ms();
    s.lk.starts++;
    fprintf(stderr, "native: compositor: started the lock program \"%s\" (pid %d; %s)\n", s.lk.program, (int)pid, why);
    lock_publish();
}

void lock_reap(void)
{
    if (s.lk.pid <= 0) return;
    int st = 0;
    if (waitpid(s.lk.pid, &st, WNOHANG) != s.lk.pid) return;
    fprintf(stderr, "native: compositor: the lock program ended (%d)%s\n",
            WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st),
            s.lk.locked && s.lk.lock == NULL ? "; the screen stays locked" : "");
    s.lk.pid = 0;
    s.lk.awaiting = false;
    lock_publish();
}

/* A key while locked: the lock surface's, or (the program gone) a new start. */
void lock_key(keyboard_t *k, struct wlr_keyboard_key_event *ev)
{
    bool down = ev->state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (s.lk.focus != NULL) {
        wlr_seat_set_keyboard(s.seat, k->wlr);
        wlr_seat_keyboard_notify_key(s.seat, ev->time_msec, ev->keycode, ev->state);
        return;
    }
    s.lk.keys_held_back++;
    if (down && s.lk.lock == NULL && s.lk.pid <= 0 && now_ms() - s.lk.started_ms >= 2000) {
        lock_start("a key, the lock program being gone");
    }
}

/* The pointer while locked: the first output's lock surface only (it lies
 * at the output's origin, the layout's). */
void lock_motion(uint32_t time_msec)
{
    lock_surface_t *ls;
    wl_list_for_each(ls, &s.lk.surfaces, link) {
        if (!lock_primary(ls) || !ls->wlr->surface->mapped) continue;
        wlr_seat_pointer_notify_enter(s.seat, ls->wlr->surface, s.cursor->x, s.cursor->y);
        wlr_seat_pointer_notify_motion(s.seat, time_msec, s.cursor->x, s.cursor->y);
        return;
    }
    wlr_seat_pointer_clear_focus(s.seat);
}

static void lock_surface_map(struct wl_listener *l, void *data)
{
    (void)data;
    lock_surface_t *ls = wl_container_of(l, ls, map);
    if (!lock_primary(ls)) return;
    lock_focus(ls->wlr->surface);
    lock_motion((uint32_t)now_ms());
    if (s.lk.lock != NULL && !s.lk.sent_locked) {
        wlr_session_lock_v1_send_locked(s.lk.lock);
        s.lk.sent_locked = true;
        fprintf(stderr, "native: compositor: the screen is locked (the lock program's surface is up)\n");
    }
}

static void lock_surface_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    lock_surface_t *ls = wl_container_of(l, ls, destroy);
    if (s.lk.focus == ls->wlr->surface) lock_focus(NULL);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->link);
    free(ls);
}

static void lock_new_surface(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_session_lock_surface_v1 *wls = data;
    lock_surface_t *ls = calloc(1, sizeof(*ls));
    if (ls == NULL) return;
    ls->wlr = wls;
    ls->tree = wlr_scene_subsurface_tree_create(s.lock_tree, wls->surface);
    int ox = 0, oy = 0;
    if (wls->output != NULL) {
        struct wlr_box b;
        wlr_output_layout_get_box(s.layout, wls->output, &b);
        ox = b.x;
        oy = b.y;
    }
    wlr_scene_node_set_position(&ls->tree->node, ox, oy);
    wlr_scene_node_set_enabled(&ls->tree->node, lock_primary(ls));
    ls->map.notify = lock_surface_map;
    wl_signal_add(&wls->surface->events.map, &ls->map);
    ls->destroy.notify = lock_surface_destroy;
    wl_signal_add(&wls->events.destroy, &ls->destroy);
    wl_list_insert(&s.lk.surfaces, &ls->link);
    int ew = 0, eh = 0;
    if (wls->output != NULL) wlr_output_effective_resolution(wls->output, &ew, &eh);
    wlr_session_lock_surface_v1_configure(wls, (uint32_t)(ew > 0 ? ew : 1), (uint32_t)(eh > 0 ? eh : 1));
}

static void lock_forget(void)
{
    wl_list_remove(&s.lk.lock_unlock.link);
    wl_list_remove(&s.lk.lock_destroy.link);
    wl_list_remove(&s.lk.lock_new_surface.link);
    s.lk.lock = NULL;
}

static void lock_unlock(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    s.lk.unlocks++;
    fprintf(stderr, "native: compositor: the lock program unlocked the screen\n");
    lock_forget();
    lock_set(false);
}

static void lock_destroy(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    lock_forget();
    if (s.lk.locked) {
        s.lk.died++;
        s.lk.focus = NULL;
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        wlr_seat_pointer_clear_focus(s.seat);
        fprintf(stderr, "native: compositor: the lock went away without unlocking: the screen stays locked "
                        "(a key, or logind's Lock, starts the lock program again)\n");
    }
    lock_publish();
}

static void new_lock(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_session_lock_v1 *lock = data;
    if (s.lk.lock != NULL) {
        fprintf(stderr, "native: compositor: a second lock refused (one is there)\n");
        wlr_session_lock_v1_destroy(lock);
        return;
    }
    s.lk.lock = lock;
    s.lk.awaiting = false;
    s.lk.sent_locked = false;
    s.lk.locks++;
    s.lk.lock_unlock.notify = lock_unlock;
    wl_signal_add(&lock->events.unlock, &s.lk.lock_unlock);
    s.lk.lock_destroy.notify = lock_destroy;
    wl_signal_add(&lock->events.destroy, &s.lk.lock_destroy);
    s.lk.lock_new_surface.notify = lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &s.lk.lock_new_surface);
    fprintf(stderr, "native: compositor: locking the screen (a lock program asked)\n");
    lock_set(true);
}

void lock_init(const native_machine_t *m)
{
    wl_list_init(&s.lk.surfaces);
    snprintf(s.lk.program, sizeof(s.lk.program), "%s", m->compositor.locker);
    s.lk.available = s.lk.program[0] != '\0' && program_found(s.lk.program);
    s.lock_tree = wlr_scene_tree_create(&s.scene->tree);
    static const float cover[4] = { 0.10f, 0.11f, 0.14f, 1.0f };
    s.lk.cover = wlr_scene_rect_create(s.lock_tree, 0, 0, cover);
    wlr_scene_node_set_enabled(&s.lock_tree->node, false);
    s.lk.mgr = wlr_session_lock_manager_v1_create(s.display);
    s.lk.new_lock.notify = new_lock;
    wl_signal_add(&s.lk.mgr->events.new_lock, &s.lk.new_lock);
    if (s.lk.available) {
        fprintf(stderr, "native: compositor: the screen lock: \"%s\" (ext-session-lock)\n", s.lk.program);
    } else if (s.lk.program[0] != '\0') {
        fprintf(stderr, "native: compositor: the screen lock: %s is not installed (no Lock entry; "
                        "ext-session-lock is served for any lock program)\n", s.lk.program);
    }
    lock_publish();
}

