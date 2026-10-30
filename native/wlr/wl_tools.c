/*
 * wl_tools.c - the tools' own devices in the compositor, and the
 * control socket's requests (comp ...).
 * Split from output_wlr.c (L16), which has the overview; what the files
 * share is in output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

/* ---------------------------------------------------- the tools' devices */

static const struct wlr_keyboard_impl s_tool_kb_impl = { .name = "fmrb-tools-keyboard" };
static const struct wlr_pointer_impl s_tool_ptr_impl = { .name = "fmrb-tools-pointer" };

void tool_devices_init(void)
{
    wlr_keyboard_init(&s.tool_kb, &s_tool_kb_impl, "fmrb-tools-keyboard");
    add_keyboard(&s.tool_kb);
    wlr_pointer_init(&s.tool_ptr, &s_tool_ptr_impl, "fmrb-tools-pointer");
    wlr_cursor_attach_input_device(s.cursor, &s.tool_ptr.base);
    wlr_seat_set_capabilities(s.seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

static void tool_key(unsigned code, bool down)
{
    struct wlr_keyboard_key_event ev = {
        .time_msec = (uint32_t)now_ms(),
        .keycode = code,
        .update_state = true,
        .state = down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
    };
    wlr_keyboard_notify_key(&s.tool_kb, &ev);
}

static void tool_move(int x, int y)
{
    if (s.route.out_w <= 0 || s.route.out_h <= 0) return;
    struct wlr_pointer_motion_absolute_event ev = {
        .pointer = &s.tool_ptr,
        .time_msec = (uint32_t)now_ms(),
        .x = ((double)x + 0.5) / s.route.out_w,
        .y = ((double)y + 0.5) / s.route.out_h,
    };
    wl_signal_emit_mutable(&s.tool_ptr.events.motion_absolute, &ev);
    wl_signal_emit_mutable(&s.tool_ptr.events.frame, &s.tool_ptr);
}

static void tool_button(int b, bool down)
{
    static const uint32_t map[] = { 0, BTN_LEFT, BTN_MIDDLE, BTN_RIGHT };
    if (b < 1 || b > 3) return;
    struct wlr_pointer_button_event ev = {
        .pointer = &s.tool_ptr,
        .time_msec = (uint32_t)now_ms(),
        .button = map[b],
        .state = down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
    };
    wlr_pointer_notify_button(&s.tool_ptr, &ev);
    wl_signal_emit_mutable(&s.tool_ptr.events.frame, &s.tool_ptr);
}

static void tool_wheel(int notches)
{
    struct wlr_pointer_axis_event ev = {
        .pointer = &s.tool_ptr,
        .time_msec = (uint32_t)now_ms(),
        .source = WL_POINTER_AXIS_SOURCE_WHEEL,
        .orientation = WL_POINTER_AXIS_VERTICAL_SCROLL,
        .relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL,
        .delta = -15.0 * notches,
        .delta_discrete = -WLR_POINTER_AXIS_DISCRETE_STEP * notches,
    };
    wl_signal_emit_mutable(&s.tool_ptr.events.axis, &ev);
    wl_signal_emit_mutable(&s.tool_ptr.events.frame, &s.tool_ptr);
}

/*
 * The control socket's "comp ..." (tools/fmrb_native.rb comp):
 *
 *   comp info            "OK out=WxH frame=WxH scale=S at=X,Y
 *                        view=VX,VY,VWxVH vscale=S vat=X,Y focus=F
 *                        windows=ID:WxH+X+Y/FXxFY+FWxFH/V,... ptr=ID:SX,SY
 *                        cursor=fmrb|window keymap=us|jp socket=NAME
 *                        renderer=R": each window's size and place on the
 *                        output, its content rectangle in frame coordinates
 *                        and whether it shows (V 1/0), bottom to top; the
 *                        last pointer position sent to a window
 *                        (surface-local, output pixels)
 *   comp stats [reset]   pictures (count, mean and longest gap), output
 *                        commits, keys sent to a window and the time from
 *                        each to the window's next commit, and how often
 *                        the shown part of the frame changed with a frame
 *                        (a fullscreen app's view, L9e), since the last
 *                        reset
 *   comp key CODE 1|0    a Linux key code down / up, through the seat
 *   comp move X Y        the pointer to output coordinates
 *   comp button B 1|0    1 left, 2 middle, 3 right
 *   comp click X Y       move, then left down and up
 *   comp wheel N         notches, + away from the user
 *   comp apps LANG       the launcher's Linux list (linux_apps.h) as LANG
 *                        names it: "OK ready=0|1 count=N"
 *   comp app ID LANG     one entry of it: "OK index=K name=NAME icon=PATH"
 *                        (K from 0 in the list's order, -1 when not listed;
 *                        icon "-" for none)
 *
 * The tools' keyboard and pointer go through the same handlers (focus,
 * routing) as the backend's devices.
 */
/* comp apps / comp app (see wlr_ctl). */
static void apps_ctl(const char *a, char *out, size_t cap)
{
    char id[256] = "", lang[16] = "en";
    bool one = strncmp(a, "app ", 4) == 0;
    if (one) {
        if (sscanf(a + 4, "%255s %15s", id, lang) < 1) {
            snprintf(out, cap, "ERR comp app ID LANG\n");
            return;
        }
    } else {
        sscanf(a + 4, " %15s", lang);
    }
    int32_t need = linux_apps_list(LINUX_APPS_LIST, lang, NULL, 0);
    char *buf = need > 0 ? malloc((size_t)need + 1) : NULL;
    int32_t got = buf ? linux_apps_list(LINUX_APPS_LIST, lang, buf, (size_t)need) : -1;
    if (got < 0 || got > need) {
        snprintf(out, cap, "ERR no Linux apps here\n");
        free(buf);
        return;
    }
    buf[got] = '\0';
    bool ready = strncmp(buf, "ready\n", 6) == 0;
    int count = 0, index = -1;
    char name[160] = "", icon[300] = "-";
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);   /* the header */
    for (line = strtok_r(NULL, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save), count++) {
        char *t1 = strchr(line, '\t');
        char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (t1 == NULL || t2 == NULL || !one || index >= 0) continue;
        *t1 = *t2 = '\0';
        if (strcmp(t1 + 1, id) != 0) continue;
        index = count;
        snprintf(name, sizeof(name), "%s", line);
        if (t2[1] != '\0') snprintf(icon, sizeof(icon), "%s", t2 + 1);
    }
    free(buf);
    if (one) {
        snprintf(out, cap, "OK index=%d name=%s icon=%s\n", index, name, icon);
    } else {
        snprintf(out, cap, "OK ready=%d count=%d\n", ready ? 1 : 0, count);
    }
}

bool wlr_ctl(const char *req, char *out, size_t cap)
{
    if (strncmp(req, "comp ", 5) != 0 && strcmp(req, "comp") != 0) return false;
    const char *a = req + 4;
    while (*a == ' ') a++;
    int x = 0, y = 0;
    unsigned code = 0;
    if (strcmp(a, "info") == 0 || a[0] == '\0') {
        const comp_route_t *r = &s.route;
        int n = snprintf(out, cap, "OK out=%dx%d frame=%dx%d scale=%d at=%d,%d view=%d,%d,%dx%d vscale=%d vat=%d,%d "
                         "focus=%d windows=",
                         r->out_w, r->out_h, r->fb_w, r->fb_h, r->place.scale, r->place.x, r->place.y,
                         r->viewing ? r->view_x : 0, r->viewing ? r->view_y : 0, r->viewing ? r->view_w : r->fb_w,
                         r->viewing ? r->view_h : r->fb_h, r->show.scale, r->show.x, r->show.y, r->focus);
        for (int i = 0; i < r->n && n > 0 && (size_t)n < cap; i++) {
            const comp_window_t *w = &r->win[i];
            n += snprintf(out + n, cap - (size_t)n, "%s%d:%dx%d+%d+%d/%dx%d+%dx%d/%d", i ? "," : "", w->id, w->w,
                          w->h, w->x, w->y, w->fx, w->fy, w->fw, w->fh, w->visible ? 1 : 0);
        }
        if (n > 0 && (size_t)n < cap) {
            n += snprintf(out + n, cap - (size_t)n, " ptr=%d:%.2f,%.2f cursor=%s keymap=%s socket=%s renderer=%s x11=",
                          s.ptr_id, s.ptr_sx, s.ptr_sy, s.fmrb_cursor_hidden ? "window" : "fmrb",
                          s.layout_kb == FMRB_KEYMAP_LAYOUT_JP ? "jp" : "us", s.socket, renderer_name());
        }
        /* The X11 windows' ids (menus marked m), and Xwayland: off (none
         * here), lazy (not started yet) or up. */
        const char *xw = "off";
        int k = 0;
#if WLR_HAS_XWAYLAND
        window_t *w;
        wl_list_for_each_reverse(w, &s.windows, link) {
            if (w->kind != WIN_X11 || !w->mapped || w->id <= 0 || n <= 0 || (size_t)n >= cap) continue;
            n += snprintf(out + n, cap - (size_t)n, "%s%d%s", k++ ? "," : "", w->id, w->or_win ? "m" : "");
        }
        if (s.xwayland != NULL) xw = s.xwayland_up ? "up" : "lazy";
#endif
        if (n > 0 && (size_t)n < cap) {
            n += snprintf(out + n, cap - (size_t)n, "%s xwayland=%s rotate=%d", k ? "" : "-", xw, s.rotate);
        }
        /* L13: how each window is laid out (m maximized, l / r a half, f
         * - as placed), the lock. */
        if (n > 0 && (size_t)n < cap) n += snprintf(out + n, cap - (size_t)n, " states=");
        for (int i = 0; i < r->n && n > 0 && (size_t)n < cap; i++) {
            const comp_window_t *cw = &r->win[i];
            const char *st = cw->state == COMP_STATE_MAX ? "m" : cw->state == COMP_STATE_LEFT ? "l"
                           : cw->state == COMP_STATE_RIGHT ? "r" : "-";
            n += snprintf(out + n, cap - (size_t)n, "%s%d:%s", i ? "," : "", cw->id, st);
        }
        if (n > 0 && (size_t)n < cap && r->n == 0) n += snprintf(out + n, cap - (size_t)n, "-");
        const char *lk = s.lk.locked ? (s.lk.lock != NULL ? "locked" : "dead") : (s.lk.awaiting ? "locking" : "none");
        if (n > 0 && (size_t)n < cap) {
            n += snprintf(out + n, cap - (size_t)n, " lock=%s locker=%s", lk, s.lk.available ? "yes" : "no");
        }
        if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "\n");
        return true;
    }
    if (strncmp(a, "stats", 5) == 0) {
        uint64_t t = now_ms();
        double span = (double)(t - s.stats.since_ms) / 1000.0;
        unsigned gaps = s.stats.pictures > 1 ? s.stats.pictures - 1 : 0;
        snprintf(out, cap,
                 "OK span_s=%.1f pictures=%u gap_mean_ms=%.1f gap_max_ms=%llu commits=%u keys=%u "
                 "key_commit_n=%u key_commit_mean_ms=%.1f key_commit_max_ms=%llu view_changes=%u pointer_rechecks=%u "
                 "x11_menu_presses=%u to_fmrb=%u winops=%u locks=%u unlocks=%u lock_died=%u "
                 "lock_keys_held=%u lock_starts=%u\n",
                 span, s.stats.pictures,
                 gaps ? (double)s.stats.picture_gap_sum_ms / gaps : 0.0,
                 (unsigned long long)s.stats.picture_gap_max_ms, s.stats.commits, s.stats.keys,
                 s.stats.key_lat_n, s.stats.key_lat_n ? (double)s.stats.key_lat_sum_ms / s.stats.key_lat_n : 0.0,
                 (unsigned long long)s.stats.key_lat_max_ms, s.stats.view_changes, s.stats.pointer_rechecks,
                 s.stats.x11_menu_presses, s.stats.to_fmrb, s.stats.winops, s.lk.locks,
                 s.lk.unlocks, s.lk.died, s.lk.keys_held_back, s.lk.starts);
        if (strcmp(a, "stats reset") == 0) {
            memset(&s.stats, 0, sizeof(s.stats));
            s.stats.since_ms = t;
        }
        return true;
    }
    if (strcmp(a, "ime") == 0) {
        /* The IME (L12b): whom it serves, the composing text, the popups. */
        wl_ime_describe(out, cap);
        return true;
    }
    if (strcmp(a, "clip") == 0) {
        /* The clipboard (L12a): who owns the selection and the counts. */
        wl_clip_stats_t c;
        wl_clip_stats(&c);
        native_clip_stats_t k;
        native_clip_stats(&k);
        snprintf(out, cap,
                 "OK owner=%s kept=%zu gave=%u last_gave=%zu served=%u linux_sets=%u took=%u last_took=%zu "
                 "refused=%u not_text=%u timeouts=%u owned=%u kept_again=%u fmrb_out=%u fmrb_taken=%u\n",
                 c.owner == 0 ? "none" : c.owner == 1 ? "fmrb" : "linux", c.kept_len, c.gave, c.last_gave,
                 c.served, c.linux_sets, c.took, c.last_took, c.refused, c.not_text, c.timeouts, c.owned,
                 c.kept, k.out, k.taken);
        return true;
    }
    if (strncmp(a, "apps", 4) == 0 || strncmp(a, "app ", 4) == 0) {
        apps_ctl(a, out, cap);
        return true;
    }
    int b = 0, st = 0, n = 0;
    if (sscanf(a, "key %u %d", &code, &st) == 2) {
        tool_key(code, st != 0);
    } else if (sscanf(a, "move %d %d", &x, &y) == 2) {
        tool_move(x, y);
    } else if (sscanf(a, "button %d %d", &b, &st) == 2) {
        tool_button(b, st != 0);
    } else if (sscanf(a, "click %d %d", &x, &y) == 2) {
        tool_move(x, y);
        tool_button(1, true);
        tool_button(1, false);
    } else if (sscanf(a, "wheel %d", &n) == 1) {
        tool_wheel(n);
    } else {
        snprintf(out, cap, "ERR comp: info, stats [reset], clip, key CODE 1|0, move X Y, button B 1|0, click X Y, wheel N\n");
        return true;
    }
    wl_display_flush_clients(s.display);
    snprintf(out, cap, "OK\n");
    return true;
}

