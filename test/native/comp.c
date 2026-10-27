// Host tests of the compositor's hand-overs (doc/linux_native/, L8), the
// parts that need no wlroots:
//
//   route   native/wlr/comp_route.c: the picture's place on outputs of
//           several sizes (whole scale, centred, black around; the
//           high-resolution frame), windows centred at their own size,
//           what a point is over (top window first, then the picture or
//           the border, clamped into the frame), the keyboard moving with
//           presses, a window going away, and the notices for the kernel
//           taken from another thread
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

static void test_hits(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    // Before any window: the picture, frame coordinates.
    comp_hit_t h = comp_route_at(&r, 1 + 3 * 100 + 1, 3 * 50 + 2);
    CHECK(h.target == COMP_FMRB && h.in_picture && h.x == 100 && h.y == 50, "picture hit %d,%d", h.x, h.y);
    // The border (x 0 is left of the picture at x 1): Family mruby, clamped.
    h = comp_route_at(&r, 0, 700);
    CHECK(h.target == COMP_FMRB && !h.in_picture && h.x == 0 && h.y == 233, "border hit %d,%d", h.x, h.y);

    // A 600x400 window, centred: (340,160)-(940,560).
    CHECK(comp_route_window_add(&r, 1, 600, 400), "add 1");
    const comp_window_t *w = comp_route_window(&r, 1);
    CHECK(w != NULL && w->x == 340 && w->y == 160, "window 1 at %d,%d", w ? w->x : -1, w ? w->y : -1);
    CHECK(!comp_route_window_add(&r, 1, 10, 10), "the same id twice refused");
    h = comp_route_at(&r, 340, 160);
    CHECK(h.target == 1 && h.x == 0 && h.y == 0, "window corner -> 1 at %d,%d", h.x, h.y);
    h = comp_route_at(&r, 939, 559);
    CHECK(h.target == 1 && h.x == 599 && h.y == 399, "window far corner -> 1");
    h = comp_route_at(&r, 940, 559);
    CHECK(h.target == COMP_FMRB && h.in_picture, "just right of the window -> the picture");

    // A smaller second window on top: (540,260)-(740,460).
    comp_route_window_add(&r, 2, 200, 200);
    h = comp_route_at(&r, 600, 300);
    CHECK(h.target == 2 && h.x == 60 && h.y == 40, "over both -> the top one (%d)", h.target);
    h = comp_route_at(&r, 400, 300);
    CHECK(h.target == 1, "over 1 only -> 1");

    // A resize re-centres; an output resize re-centres every window.
    comp_route_window_resize(&r, 1, 800, 600);
    w = comp_route_window(&r, 1);
    CHECK(w->x == 240 && w->y == 60, "resized window at %d,%d", w->x, w->y);
    comp_route_layout(&r, 1920, 1080, 426, 240);
    w = comp_route_window(&r, 1);
    CHECK(w->x == 560 && w->y == 240, "after the output grew at %d,%d", w->x, w->y);
    comp_route_destroy(&r);
}

static void test_focus(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    comp_route_window_add(&r, 1, 600, 400);   // (340,160)-(940,560)
    comp_route_window_add(&r, 2, 200, 200);   // (540,260)-(740,460)
    CHECK(comp_route_focus(&r) == COMP_FMRB, "Family mruby has the keyboard at first, windows or not");

    bool changed = false;
    comp_hit_t h = comp_route_press(&r, 400, 200, &changed);
    CHECK(h.target == 1 && changed && comp_route_focus(&r) == 1, "press on 1 -> 1");
    CHECK(r.win[r.n - 1].id == 1, "1 raised to the top");
    // 1 is on top now, and covers 2: a press where both are goes to 1.
    h = comp_route_press(&r, 600, 300, &changed);
    CHECK(h.target == 1 && !changed, "press on 1 again: no change");
    h = comp_route_press(&r, 100, 100, &changed);
    CHECK(h.target == COMP_FMRB && changed && comp_route_focus(&r) == COMP_FMRB, "press on the picture -> back");
    h = comp_route_press(&r, 0, 0, &changed);
    CHECK(h.target == COMP_FMRB && !changed, "press on the border: still Family mruby");

    comp_route_press(&r, 400, 200, &changed);
    CHECK(comp_route_window_remove(&r, 1) && comp_route_focus(&r) == COMP_FMRB,
          "the focused window goes -> back to Family mruby");
    CHECK(!comp_route_window_remove(&r, 1), "removing it twice: false");
    h = comp_route_at(&r, 600, 300);
    CHECK(h.target == 2, "2 is still there (%d)", h.target);
    comp_route_destroy(&r);
}

typedef struct {
    comp_route_t *r;
    int got[8];
    int n;
} reader_t;

static void *notice_reader(void *arg)
{
    reader_t *rd = arg;
    for (int spins = 0; spins < 2000 && rd->n < 6; spins++) {
        comp_notice_t nt;
        while (rd->n < 8 && comp_route_take_notice(rd->r, &nt)) rd->got[rd->n++] = (int)nt.kind * 10 + nt.id;
        usleep(500);
    }
    return NULL;
}

static void test_notices(void)
{
    comp_route_t r;
    comp_route_init(&r);
    comp_route_layout(&r, 1280, 720, 426, 240);
    reader_t rd = { .r = &r };
    pthread_t t;
    pthread_create(&t, NULL, notice_reader, &rd);
    bool changed;
    comp_route_window_add(&r, 1, 600, 400);       // MAPPED 1
    comp_route_window_resize(&r, 1, 700, 400);    // RESIZED 1
    comp_route_window_resize(&r, 1, 700, 400);    // same size: nothing
    comp_route_press(&r, 640, 360, &changed);     // FOCUS 1
    comp_route_window_remove(&r, 1);              // UNMAPPED 1, FOCUS 0
    comp_route_window_add(&r, 2, 10, 10);         // MAPPED 2
    pthread_join(t, NULL);
    int want[] = { COMP_NOTICE_MAPPED * 10 + 1, COMP_NOTICE_RESIZED * 10 + 1, COMP_NOTICE_FOCUS * 10 + 1,
                   COMP_NOTICE_UNMAPPED * 10 + 1, COMP_NOTICE_FOCUS * 10 + 0, COMP_NOTICE_MAPPED * 10 + 2 };
    CHECK(rd.n == 6, "six notices from the other thread (%d)", rd.n);
    for (int i = 0; i < 6 && i < rd.n; i++) CHECK(rd.got[i] == want[i], "notice %d: %d, want %d", i, rd.got[i], want[i]);

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

int main(void)
{
    test_place();
    test_hits();
    test_focus();
    test_notices();
    test_frame();
    if (g_fail) {
        printf("native comp: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native comp: %d checks passed (route, focus, notices, frame hand-over)\n", g_count);
    return 0;
}
