// Host tests of the compositor's hand-overs (doc/linux_native/, L8), the
// parts that need no wlroots:
//
//   route   native/wlr/comp_route.c: the picture's place on outputs of
//           several sizes (whole scale, centred, black around; the
//           high-resolution frame; a fullscreen app's part of a wide frame
//           alone, L9d), windows placed by Family mruby (its
//           stand-ins' content rectangles times the scale; hidden when not
//           placed, not shown, or in the high-resolution mode), the holes
//           (the hole colour inside a shown window's rectangle: what the
//           pointer reaches and what the picture leaves transparent), the
//           stacking Family mruby gives, the keyboard moving with presses
//           and with Family mruby's focus, a window going away, and the
//           notices taken from another thread
//   link    native/wlr/comp_link.c: places (only changes wake the loop) and
//           close commands from Family mruby's side
//   frame   native/backend/native_frame.c: a frame put by one thread wakes
//           an eventfd the output's thread waits on with poll, the newest
//           frame wins, and a put while the reader copies never tears (the
//           copy is always one whole frame)
//
// Usage: native_comp_test

#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include "comp_link.h"
#include "comp_route.h"
#include "native_frame.h"

static int g_fail = 0;
static int g_count = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_count++;                                                    \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: ", __func__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            g_fail++;                                                 \
        }                                                             \
    } while (0)

// ---------------------------------------------------------------- route

static void test_place(void)
{
    comp_route_t r;
    comp_route_init(&r);
    // WSLg's nested window: 1280x720 -> 3x, 1278x720 at (1,0).
    CHECK(comp_route_layout(&r, 1280, 720, 426, 240), "layout changed");
    CHECK(r.place.scale == 3 && r.place.x == 1 && r.place.y == 0 && r.place.w == 1278 && r.place.h == 720,
          "1280x720: %dx at %d,%d %dx%d", r.place.scale, r.place.x, r.place.y, r.place.w, r.place.h);
    CHECK(!comp_route_layout(&r, 1280, 720, 426, 240), "same layout: no change");
    // A 1920x1080 panel -> 4x, black around.
    comp_route_layout(&r, 1920, 1080, 426, 240);
    CHECK(r.place.scale == 4 && r.place.x == 108 && r.place.y == 60, "1920x1080: %dx at %d,%d",
          r.place.scale, r.place.x, r.place.y);
    // The high-resolution frame on the same panel -> 3x.
    comp_route_layout(&r, 1920, 1080, 640, 360);
    CHECK(r.place.scale == 3 && r.place.x == 0 && r.place.y == 0, "640x360 on 1920x1080: %dx at %d,%d",
          r.place.scale, r.place.x, r.place.y);
    // An output smaller than the frame: 1x, cropped from the top-left.
    comp_route_layout(&r, 400, 200, 426, 240);
    CHECK(r.place.scale == 1 && r.place.x == 0 && r.place.y == 0, "small output: %dx", r.place.scale);
    comp_route_destroy(&r);
}

// A frame of FW x FH with a stand-in's content filled with the hole colour.
#define TW 426
#define TH 240
static uint16_t g_frame[TW * TH];

static void frame_clear(void)
{
    for (int i = 0; i < TW * TH; i++) g_frame[i] = 0x1234;
}

static void frame_fill(int x, int y, int w, int h, uint16_t c)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) g_frame[yy * TW + xx] = c;
    }
}

static void test_hits(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    comp_route_set_base(&r, 426, 240);
    frame_clear();
    comp_route_set_frame(&r, g_frame);
    // Before any window: the picture, frame coordinates.
    comp_hit_t h = comp_route_at(&r, 1 + 3 * 100 + 1, 3 * 50 + 2);
    CHECK(h.target == COMP_FMRB && h.in_picture && h.x == 100 && h.y == 50, "picture hit %d,%d", h.x, h.y);
    // The border (x 0 is left of the picture at x 1): Family mruby, clamped.
    h = comp_route_at(&r, 0, 700);
    CHECK(h.target == COMP_FMRB && !h.in_picture && h.x == 0 && h.y == 233, "border hit %d,%d", h.x, h.y);

    // A window that is not placed yet is nowhere.
    CHECK(comp_route_window_add(&r, 1, 600, 400, 0, "foot"), "add 1");
    CHECK(!comp_route_window_add(&r, 1, 10, 10, 0, NULL), "the same id twice refused");
    const comp_window_t *w = comp_route_window(&r, 1);
    CHECK(w != NULL && !w->placed && !w->visible, "not placed: hidden");
    frame_fill(100, 50, 200, 100, COMP_HOLE_565);
    h = comp_route_at(&r, 1 + 3 * 150, 3 * 100);
    CHECK(h.target == COMP_FMRB, "a hole of no placed window: Family mruby (%d)", h.target);

    // Placed: content (100,50) 200x100 -> output (301,150) 600x300.
    comp_place_t p1 = { .id = 1, .x = 100, .y = 50, .w = 200, .h = 100, .shown = true };
    int ch = comp_route_place(&r, &p1, 1, COMP_FMRB);
    w = comp_route_window(&r, 1);
    CHECK((ch & COMP_PLACE_MOVED) && w->visible && w->x == 301 && w->y == 150, "placed at %d,%d (%d)", w->x,
          w->y, ch);
    int ww = 0, wh = 0;
    CHECK(comp_route_wanted_size(&r, 1, &ww, &wh) && ww == 600 && wh == 300, "asked %dx%d", ww, wh);
    h = comp_route_at(&r, 301, 150);
    CHECK(h.target == 1 && h.x == 0 && h.y == 0 && h.fx == 100 && h.fy == 50, "content corner -> 1 at %d,%d",
          h.x, h.y);
    h = comp_route_at(&r, 900, 449);
    CHECK(h.target == 1 && h.x == 599 && h.y == 299, "far corner -> 1 at %d,%d", h.x, h.y);
    h = comp_route_at(&r, 901, 449);
    CHECK(h.target == COMP_FMRB && h.in_picture, "just right of it -> the picture");
    // Output pixels inside one frame pixel stay distinct for the window.
    comp_hit_t h0 = comp_route_at(&r, 301 + 30, 150 + 30), h1 = comp_route_at(&r, 302 + 30, 150 + 30);
    CHECK(h0.target == 1 && h1.target == 1 && h1.x == h0.x + 1 && h0.fx == h1.fx,
          "fine pointer: %d then %d in frame pixel %d", h0.x, h1.x, h0.fx);

    // Something of Family mruby's drawn over the content (a menu): no hole.
    frame_fill(150, 60, 30, 20, 0xFFFF);
    h = comp_route_at(&r, 1 + 3 * 160, 3 * 70);
    CHECK(h.target == COMP_FMRB, "covered -> Family mruby (%d)", h.target);
    // The hole colour outside every window's rectangle is just a colour.
    frame_fill(10, 200, 5, 5, COMP_HOLE_565);
    h = comp_route_at(&r, 1 + 3 * 12, 3 * 202);
    CHECK(h.target == COMP_FMRB, "hole colour elsewhere -> Family mruby (%d)", h.target);

    // The picture: opaque but for the holes inside the rectangle.
    static uint32_t argb[TW * TH];
    comp_route_compose(&r, g_frame, argb);
    CHECK(argb[50 * TW + 100] == 0 && argb[149 * TW + 299] == 0, "holes are transparent");
    CHECK((argb[70 * TW + 160] >> 24) == 0xFF && argb[70 * TW + 160] == 0xFFFFFFFFu, "a menu over it is opaque");
    CHECK((argb[202 * TW + 12] >> 24) == 0xFF, "the hole colour elsewhere is opaque");
    CHECK((argb[10 * TW + 10] >> 24) == 0xFF, "the rest is opaque");

    // Not shown (its stand-in suspended): hidden, no holes.
    p1.shown = false;
    comp_route_place(&r, &p1, 1, COMP_FMRB);
    CHECK(!comp_route_window(&r, 1)->visible && comp_route_at(&r, 301, 150).target == COMP_FMRB,
          "not shown: hidden");
    comp_route_compose(&r, g_frame, argb);
    CHECK((argb[50 * TW + 100] >> 24) == 0xFF, "not shown: no hole");
    p1.shown = true;
    comp_route_place(&r, &p1, 1, COMP_FMRB);
    // The high-resolution frame: hidden while it lasts.
    comp_route_layout(&r, 1280, 720, 640, 360);
    CHECK(!comp_route_window(&r, 1)->visible, "high-resolution frame: hidden");
    comp_route_layout(&r, 1280, 720, 426, 240);
    CHECK(comp_route_window(&r, 1)->visible, "base frame again: shown");
    // A bigger output moves it with the picture (4x, at 108,60).
    comp_route_layout(&r, 1920, 1080, 426, 240);
    w = comp_route_window(&r, 1);
    CHECK(w->x == 108 + 400 && w->y == 60 + 200, "after the output grew at %d,%d", w->x, w->y);
    comp_route_destroy(&r);
}

static void test_stacking(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    comp_route_set_base(&r, 426, 240);
    frame_clear();
    comp_route_set_frame(&r, g_frame);
    comp_route_window_add(&r, 1, 600, 400, 0, "a");
    comp_route_window_add(&r, 2, 200, 200, 0, "b");
    // 1 at (100,50) 200x100, 2 at (150,70) 100x100 over it: both stand-ins
    // draw the hole colour, the upper one last.
    comp_place_t p[2] = {
        { .id = 1, .x = 100, .y = 50, .w = 200, .h = 100, .shown = true },
        { .id = 2, .x = 150, .y = 70, .w = 100, .h = 100, .shown = true },
    };
    frame_fill(100, 50, 200, 100, COMP_HOLE_565);
    frame_fill(150, 70, 100, 100, COMP_HOLE_565);
    int ch = comp_route_place(&r, p, 2, 2);
    CHECK(ch & COMP_PLACE_FOCUS, "focus from Family mruby (%d)", ch);
    CHECK(comp_route_focus(&r) == 2 && r.win[1].id == 2, "2 on top and focused");
    comp_hit_t h = comp_route_at(&r, 1 + 3 * 160, 3 * 80);
    CHECK(h.target == 2, "where both are -> the upper (%d)", h.target);
    h = comp_route_at(&r, 1 + 3 * 110, 3 * 60);
    CHECK(h.target == 1, "where 1 only -> 1 (%d)", h.target);
    // Family mruby restacks: 1 over 2.
    comp_place_t q[2] = { p[1], p[0] };
    ch = comp_route_place(&r, q, 2, 1);
    CHECK((ch & COMP_PLACE_ORDER) && (ch & COMP_PLACE_FOCUS) && r.win[1].id == 1, "restacked (%d)", ch);
    h = comp_route_at(&r, 1 + 3 * 160, 3 * 80);
    CHECK(h.target == 1, "where both are -> now 1 (%d)", h.target);
    ch = comp_route_place(&r, q, 2, 1);
    CHECK(ch == 0, "the same places again: nothing (%d)", ch);
    // Family mruby's focus elsewhere (an app of its own).
    ch = comp_route_place(&r, q, 2, COMP_FMRB);
    CHECK(ch == COMP_PLACE_FOCUS && comp_route_focus(&r) == COMP_FMRB, "focus back to Family mruby");
    // A window Family mruby stops listing loses its place.
    ch = comp_route_place(&r, &p[0], 1, COMP_FMRB);
    CHECK((ch & COMP_PLACE_MOVED) && !comp_route_window(&r, 2)->placed && r.win[0].id == 2,
          "unlisted: no place, at the bottom");
    comp_route_destroy(&r);
}

static void test_focus(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    frame_clear();
    comp_route_set_frame(&r, g_frame);
    comp_route_window_add(&r, 1, 600, 400, 0, NULL);
    comp_route_window_add(&r, 2, 300, 300, 0, NULL);
    comp_place_t p[2] = {
        { .id = 1, .x = 100, .y = 50, .w = 200, .h = 100, .shown = true },
        { .id = 2, .x = 150, .y = 70, .w = 100, .h = 100, .shown = true },
    };
    frame_fill(100, 50, 200, 100, COMP_HOLE_565);
    frame_fill(150, 70, 100, 100, COMP_HOLE_565);
    comp_route_place(&r, p, 2, COMP_FMRB);
    CHECK(comp_route_focus(&r) == COMP_FMRB, "Family mruby has the keyboard at first, windows or not");

    bool changed = false;
    comp_hit_t h = comp_route_press(&r, 1 + 3 * 110, 3 * 60, &changed);
    CHECK(h.target == 1 && changed && comp_route_focus(&r) == 1, "press on 1 -> 1 at once");
    CHECK(r.win[r.n - 1].id == 1, "1 raised among the windows");
    h = comp_route_press(&r, 1 + 3 * 110, 3 * 60, &changed);
    CHECK(h.target == 1 && !changed, "press on 1 again: no change");
    h = comp_route_press(&r, 30, 30, &changed);
    CHECK(h.target == COMP_FMRB && changed && comp_route_focus(&r) == COMP_FMRB, "press on the picture -> back");
    h = comp_route_press(&r, 0, 700, &changed);
    CHECK(h.target == COMP_FMRB && !changed, "press on the border: still Family mruby");

    comp_route_press(&r, 1 + 3 * 110, 3 * 60, &changed);
    CHECK(comp_route_window_remove(&r, 1) && comp_route_focus(&r) == COMP_FMRB,
          "the focused window goes -> back to Family mruby");
    CHECK(!comp_route_window_remove(&r, 1), "removing it twice: false");
    h = comp_route_at(&r, 1 + 3 * 160, 3 * 80);
    CHECK(h.target == 2, "2 is still there (%d)", h.target);
    // Family mruby names a window that has gone: the keyboard stays its own.
    comp_route_place(&r, &p[1], 1, 1);
    CHECK(comp_route_focus(&r) == COMP_FMRB, "focus on a gone window -> Family mruby");
    comp_route_destroy(&r);
}

typedef struct {
    comp_route_t *r;
    int got[16];
    char title[COMP_TEXT];
    int parent[16];             /* a MAPPED notice's parent (x) */
    int n;
} reader_t;

static void *notice_reader(void *arg)
{
    reader_t *rd = arg;
    for (int spins = 0; spins < 2000 && rd->n < 8; spins++) {
        comp_notice_t nt;
        while (rd->n < 16 && comp_link_take_notice(&nt)) {
            rd->parent[rd->n] = nt.kind == COMP_NOTICE_MAPPED ? nt.x : -1;
            rd->got[rd->n++] = (int)nt.kind * 10 + nt.id;
            if (nt.kind == COMP_NOTICE_TITLE) snprintf(rd->title, sizeof(rd->title), "%s", nt.text);
        }
        usleep(500);
    }
    return NULL;
}

static void test_notices(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    frame_clear();
    comp_route_set_frame(&r, g_frame);
    comp_link_attach(&r, -1);
    reader_t rd = { .r = &r };
    pthread_t t;
    pthread_create(&t, NULL, notice_reader, &rd);
    bool changed;
    comp_route_window_add(&r, 1, 600, 400, 0, "foot");                     // MAPPED 1
    comp_route_notice(&r, COMP_NOTICE_TITLE, 1, 0, 0, 0, 0, "~/src");   // TITLE 1
    comp_route_window_resize(&r, 1, 700, 400);                          // RESIZED 1
    comp_route_window_resize(&r, 1, 700, 400);                          // same size: nothing
    comp_place_t p = { .id = 1, .x = 100, .y = 50, .w = 200, .h = 100, .shown = true };
    frame_fill(100, 50, 200, 100, COMP_HOLE_565);
    comp_route_place(&r, &p, 1, COMP_FMRB);
    comp_route_press(&r, 1 + 3 * 110, 3 * 60, &changed);                // FOCUS 1
    comp_route_notice(&r, COMP_NOTICE_MOVE, 1, 90, 40, 0, 0, NULL);     // MOVE 1
    comp_route_window_remove(&r, 1);                                    // UNMAPPED 1, FOCUS 0
    comp_route_window_add(&r, 2, 10, 10, 1, NULL);                      // MAPPED 2 (parent 1)
    pthread_join(t, NULL);
    int want[] = { COMP_NOTICE_MAPPED * 10 + 1, COMP_NOTICE_TITLE * 10 + 1, COMP_NOTICE_RESIZED * 10 + 1,
                   COMP_NOTICE_FOCUS * 10 + 1, COMP_NOTICE_MOVE * 10 + 1, COMP_NOTICE_UNMAPPED * 10 + 1,
                   COMP_NOTICE_FOCUS * 10 + 0, COMP_NOTICE_MAPPED * 10 + 2 };
    CHECK(rd.n == 8, "eight notices from the other thread (%d)", rd.n);
    for (int i = 0; i < 8 && i < rd.n; i++) CHECK(rd.got[i] == want[i], "notice %d: %d, want %d", i, rd.got[i], want[i]);
    CHECK(strcmp(rd.title, "~/src") == 0, "the title came with its text (%s)", rd.title);
    CHECK(rd.parent[0] == 0 && rd.parent[7] == 1, "MAPPED carries the parent (%d, %d)", rd.parent[0], rd.parent[7]);

    // A reader that does not come: the oldest are dropped, the newest kept.
    for (int i = 0; i < COMP_NOTICES + 5; i++) comp_route_window_resize(&r, 2, 20 + i, 10);
    comp_notice_t nt;
    int n = 0, last_w = 0;
    while (comp_route_take_notice(&r, &nt)) {
        n++;
        last_w = nt.w;
    }
    CHECK(n == COMP_NOTICES && r.notices_dropped == 5 && last_w == 20 + COMP_NOTICES + 4,
          "full queue: %d kept, %u dropped, last w %d", n, r.notices_dropped, last_w);
    comp_link_detach();
    CHECK(!comp_link_take_notice(&nt), "detached: nothing to take");
    comp_route_destroy(&r);
}

// Dialogs and menus (L9c): a dialog's stand-in in the middle of its
// parent's, kept in the frame; menus kept in the picture.
static void test_dialogs(void)
{
    int x = 0, y = 0;
    // The parent's content 300x150 at (63,56); the dialog's window 162x102
    // (content 160x90, frame 1/11, 2x12): its content's middle on the
    // parent's (213,131).
    comp_dialog_place(63, 56, 300, 150, 162, 102, 1, 11, 2, 12, 426, 240, 13, &x, &y);
    CHECK(x + 1 + 80 == 213 && y + 11 + 45 == 131, "centred: window at (%d,%d)", x, y);
    // Odd sizes: the middles differ by under a pixel.
    comp_dialog_place(10, 20, 101, 51, 62, 42, 1, 11, 2, 12, 426, 240, 13, &x, &y);
    CHECK(abs((2 * (x + 1) + 60) - (2 * 10 + 101)) <= 1 && abs((2 * (y + 11) + 30) - (2 * 20 + 51)) <= 1,
          "odd sizes: window at (%d,%d)", x, y);
    // Near the right and bottom edges: moved back into the frame.
    comp_dialog_place(300, 180, 120, 50, 162, 102, 1, 11, 2, 12, 426, 240, 13, &x, &y);
    CHECK(x == 426 - 162 && y == 240 - 102, "kept in at the bottom right: (%d,%d)", x, y);
    // Near the top left: below the menu bar.
    comp_dialog_place(0, 13, 40, 20, 162, 102, 1, 11, 2, 12, 426, 240, 13, &x, &y);
    CHECK(x == 0 && y == 13, "kept in at the top left: (%d,%d)", x, y);
    // Larger than the frame: its top-left in.
    comp_dialog_place(100, 100, 50, 50, 500, 300, 1, 11, 2, 12, 426, 240, 13, &x, &y);
    CHECK(x == 0 && y == 13, "larger than the frame: (%d,%d)", x, y);

    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 720, 1280, 426, 240);         // 1x at (147,520)
    int bx, by, bw, bh;
    comp_route_picture_box(&r, 210, 576, &bx, &by, &bw, &bh);
    CHECK(bx == 147 - 210 && by == 520 - 576 && bw == 426 && bh == 240, "the popup box: %d,%d %dx%d", bx, by, bw, bh);
    int mx = 147 + 426 - 60, my = 560;
    CHECK(comp_route_inside(&r, &mx, &my, 150, 100) && mx == 147 + 426 - 150 && my == 560,
          "a menu across the right edge moved in: (%d,%d)", mx, my);
    mx = 100;
    my = 1000;
    CHECK(comp_route_inside(&r, &mx, &my, 50, 50) && mx == 147 && my == 520 + 240 - 50,
          "a menu below and left of the picture moved in: (%d,%d)", mx, my);
    mx = 200;
    my = 600;
    CHECK(!comp_route_inside(&r, &mx, &my, 50, 50) && mx == 200 && my == 600, "a menu inside stays");
    mx = 140;
    my = 500;
    CHECK(comp_route_inside(&r, &mx, &my, 600, 300) && mx == 147 && my == 520, "a menu larger: its top-left in");
    comp_route_destroy(&r);
}

// The other direction: places and commands from Family mruby's side wake
// the compositor's loop, and only changes count.
static void test_link(void)
{
    comp_route_t r;
    comp_route_init(&r);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    comp_link_attach(&r, fd);
    CHECK(comp_link_attached(), "attached");
    comp_link_set_geometry(3, 426, 240);
    int sc = 0, bw = 0, bh = 0;
    CHECK(comp_link_geometry(&sc, &bw, &bh) && sc == 3 && bw == 426 && bh == 240, "geometry %d %dx%d", sc, bw, bh);

    comp_places_t pl;
    memset(&pl, 0, sizeof(pl));
    pl.n = 1;
    pl.p[0] = (comp_place_t){ .id = 1, .x = 10, .y = 20, .w = 30, .h = 40, .shown = true };
    pl.focus = 1;
    comp_places_t got;
    CHECK(!comp_link_take_places(&got), "nothing yet");
    comp_link_place(&pl);
    struct pollfd p = { .fd = fd, .events = POLLIN };
    CHECK(poll(&p, 1, 0) == 1, "places wake the loop");
    uint64_t v;
    CHECK(read(fd, &v, sizeof(v)) == sizeof(v), "one wake");
    CHECK(comp_link_take_places(&got) && got.n == 1 && got.p[0].x == 10 && got.focus == 1, "taken");
    CHECK(!comp_link_take_places(&got), "taken once");
    comp_link_place(&pl);
    CHECK(poll(&p, 1, 0) == 0 && !comp_link_take_places(&got), "the same places: no wake");
    pl.p[0].x = 11;
    comp_link_place(&pl);
    CHECK(poll(&p, 1, 0) == 1 && comp_link_take_places(&got) && got.p[0].x == 11, "a change: woken, taken");
    CHECK(read(fd, &v, sizeof(v)) == sizeof(v), "drained");

    comp_link_command(COMP_CMD_CLOSE, 7);
    comp_cmd_kind_t k;
    int id = 0;
    CHECK(poll(&p, 1, 0) == 1 && comp_link_take_command(&k, &id) && k == COMP_CMD_CLOSE && id == 7, "close 7");
    CHECK(!comp_link_take_command(&k, &id), "one command");
    comp_link_detach();
    CHECK(!comp_link_attached(), "detached");
    close(fd);
    comp_route_destroy(&r);
}

// ---------------------------------------------------------------- frame

#define FW 426
#define FH 240
#define FRAMES 300

static void *display_task(void *arg)
{
    (void)arg;
    static uint16_t fb[FW * FH];
    for (int i = 1; i <= FRAMES; i++) {
        // Every pixel of frame i holds i: a torn copy shows two values.
        for (int p = 0; p < FW * FH; p++) fb[p] = (uint16_t)i;
        native_frame_put(fb, FW, FH);
        if (i % 50 == 0) usleep(2000);
    }
    return NULL;
}

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void test_frame(void)
{
    CHECK(native_frame_init(FW, FH), "init");
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CHECK(fd >= 0, "eventfd");
    native_frame_set_wake_fd(fd);

    // One put wakes a poll at once.
    static uint16_t one[FW * FH];
    struct pollfd p = { .fd = fd, .events = POLLIN };
    CHECK(poll(&p, 1, 0) == 0, "nothing to wake for yet");
    native_frame_put(one, FW, FH);
    CHECK(poll(&p, 1, 0) == 1, "a put wakes the reader");
    uint64_t v;
    CHECK(read(fd, &v, sizeof(v)) == sizeof(v) && v == 1, "one wake");

    // A writer thread against a reader that waits on the eventfd and copies.
    static uint16_t copy[640 * 360];
    pthread_t t;
    uint32_t seq0 = native_frame_seq();
    pthread_create(&t, NULL, display_task, NULL);
    int torn = 0, copies = 0, last = 0, backwards = 0;
    uint64_t deadline = ms_now() + 5000;
    while (last < FRAMES && ms_now() < deadline) {
        if (poll(&p, 1, 100) <= 0) continue;
        if (read(fd, &v, sizeof(v)) != sizeof(v)) continue;
        int w = 0, h = 0;
        uint32_t seq = 0;
        if (!native_frame_copy(copy, 640 * 360, &w, &h, &seq)) continue;
        copies++;
        for (int i = 1; i < w * h; i++) {
            if (copy[i] != copy[0]) {
                torn++;
                break;
            }
        }
        if ((int)copy[0] < last) backwards++;
        last = copy[0];
    }
    pthread_join(t, NULL);
    CHECK(last == FRAMES, "the reader saw the last frame (%d of %d)", last, FRAMES);
    CHECK(torn == 0 && backwards == 0, "%d copies: %d torn, %d out of order", copies, torn, backwards);
    CHECK(native_frame_seq() - seq0 == FRAMES, "seq moved by %u", native_frame_seq() - seq0);
    printf("  frame: %d frames put, the reader woke for and copied %d (newest wins)\n", FRAMES, copies);

    native_frame_set_wake_fd(-1);
    close(fd);
}

// The wide screen (L9d): a 640x400 frame filling a 1280x800 output at 2x,
// and the view -- a fullscreen app's 426x240 alone at 3x, centred -- with
// the pointer through it and the windows hidden meanwhile.
static void test_view(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 800, 640, 400);
    comp_route_set_base(&r, 640, 400);
    CHECK(r.place.scale == 2 && r.place.x == 0 && r.place.y == 0 && r.place.w == 1280 && r.place.h == 800,
          "640x400 fills 1280x800 at 2x (%d at %d,%d)", r.place.scale, r.place.x, r.place.y);
    CHECK(!r.viewing && r.show.scale == 2, "no view: the whole frame is shown");
    CHECK(comp_route_window_add(&r, 1, 400, 300, 0, "foot"), "add");
    comp_place_t p = { .id = 1, .x = 300, .y = 100, .w = 200, .h = 150, .shown = true };
    comp_route_place(&r, &p, 1, 1);
    const comp_window_t *w = comp_route_window(&r, 1);
    int ww = 0, wh = 0;
    CHECK(w->visible && w->x == 600 && w->y == 200 && comp_route_wanted_size(&r, 1, &ww, &wh) && ww == 400 &&
          wh == 300, "a window at 2x: (%d,%d) %dx%d", w->x, w->y, ww, wh);

    CHECK(comp_route_set_view(&r, 0, 0, 426, 240), "a view changes what is shown");
    CHECK(!comp_route_set_view(&r, 0, 0, 426, 240), "the same view again: no change");
    CHECK(r.viewing && r.show.scale == 3 && r.show.x == 1 && r.show.y == 40 && r.show.w == 1278 && r.show.h == 720,
          "426x240 alone at 3x, (%d,%d) %dx%d", r.show.x, r.show.y, r.show.w, r.show.h);
    CHECK(r.place.scale == 2, "the windows keep the frame's scale (%d)", r.place.scale);
    w = comp_route_window(&r, 1);
    CHECK(!w->visible, "windows hidden under the view");
    int fx = 0, fy = 0;
    CHECK(comp_route_to_frame(&r, 1 + 3 * 100 + 1, 40 + 3 * 50 + 2, &fx, &fy) && fx == 100 && fy == 50,
          "a point through the view: %d,%d", fx, fy);
    CHECK(!comp_route_to_frame(&r, 1279, 10, &fx, &fy) && fx == 425 && fy == 0, "the band: clamped to %d,%d", fx,
          fy);
    comp_hit_t h = comp_route_at(&r, 1 + 3 * 425 + 1, 40 + 3 * 239 + 1);
    CHECK(h.target == COMP_FMRB && h.in_picture && h.x == 425 && h.y == 239, "hit through the view %d,%d", h.x,
          h.y);
    // Not smaller than the frame, or not inside it: no view.
    CHECK(comp_route_set_view(&r, 0, 0, 640, 400) && !r.viewing, "a view of the whole frame is none");
    comp_route_set_view(&r, 300, 0, 426, 240);
    CHECK(!r.viewing, "a view reaching out of the frame is none");
    comp_route_set_view(&r, 0, 0, 426, 240);
    // The high-resolution frame (not the base) ignores the view.
    comp_route_layout(&r, 1280, 800, 640, 360);
    CHECK(!r.viewing && r.show.scale == 2, "another frame than the base: no view");
    comp_route_layout(&r, 1280, 800, 640, 400);
    CHECK(r.viewing, "the base frame again: the view again");
    comp_route_set_view(&r, 0, 0, 0, 0);
    w = comp_route_window(&r, 1);
    CHECK(!r.viewing && w->visible && w->x == 600, "the view gone: the window shows again");
    comp_route_destroy(&r);
}

int main(void)
{
    test_place();
    test_view();
    test_hits();
    test_stacking();
    test_focus();
    test_notices();
    test_dialogs();
    test_link();
    test_frame();
    if (g_fail) {
        printf("native comp: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native comp: %d checks passed (route, holes, stacking, focus, notices, places, frame hand-over)\n", g_count);
    return 0;
}
