// Host tests for the processing halves of the native program's backends
// (doc/linux_native/, L2). WSL2 has no framebuffer, no KMS output, no evdev
// device and no sound card, so what opens those is only built; what turns
// their data into the firmware's (and back) is plain code and runs here:
//
//   present   native/present/fmrb_present.c: scale, black border, rotation
//             on surfaces in memory, RGB565 and XRGB8888, the 4 angles
//   keys      native/input/linux_keys.c: Linux KEY_* to HID usages
//   evdev     native/input/evdev_decode.c: recorded input_event streams
//             (test/native/evdev/*.ev) through a pipe, against what the
//             firmware is sent
//   cardputer native/input/keymap_cardputer_zero.c: the Sym layer against
//             M5Stack's console key table
//   fbdev     native/output/fbdev_info.c: /proc/fb and the screeninfo
//             answers
//   machine   native/machine/native_machine.c over config/machines/*.toml
//   etc       native/etc/etc_update.c: an old user /etc (test/native/etc/old)
//             brought up to a newer distribution's (seed) must come out
//             byte for byte as test/native/etc/expected, and a second run
//             must change nothing
//   sys       native/sys/sys_linux_read.c (L6): the battery and the network
//             link from a made-up sysfs and procfs written under /tmp
//             (battery none / there / charging / full, wired and wireless,
//             connected or not), the mixer steps and logind's methods
//
// Usage: native_test <fmruby-core dir>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "etc_update.h"
#include "evdev_decode.h"
#include "fbdev_info.h"
#include "fmrb_keymap.h"
#include "fmrb_present.h"
#include "keymap_extra.h"
#include "linux_keys.h"
#include "native_machine.h"
#include "sys_linux_read.h"
#include "fmrb_platform_sys.h"

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

static const char *g_core = ".";

// ---- present ---------------------------------------------------------------

// A frame whose every pixel differs from its neighbours and from black.
static void make_frame(uint16_t *fb, int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            fb[y * w + x] = (uint16_t)(0x8000 | ((y * 37 + x * 11) & 0x7fff) | 1);
        }
    }
}

// The pixel the surface should hold at physical (px, py), worked out from
// the definitions alone: physical -> view by the turn, view -> frame by the
// placement.
static uint32_t expected_at(const uint16_t *fb, int fb_w, int fb_h,
                            const fmrb_present_surface_t *s,
                            const fmrb_present_place_t *p, int px, int py)
{
    int vx = px, vy = py;
    switch (s->rotate) {
    case 90:  vx = py;            vy = s->w - 1 - px; break;
    case 180: vx = s->w - 1 - px; vy = s->h - 1 - py; break;
    case 270: vx = s->h - 1 - py; vy = px;            break;
    default: break;
    }
    int dx = vx - p->x, dy = vy - p->y;
    if (dx < 0 || dy < 0 || dx >= p->w || dy >= p->h) return 0;
    int fx = dx / p->scale, fy = dy / p->scale;
    if (fx >= fb_w || fy >= fb_h) return 0;
    uint16_t v = fb[fy * fb_w + fx];
    return s->format == FMRB_PRESENT_XRGB8888 ? fmrb_present_565_to_8888(v) : v;
}

static uint32_t read_px(const fmrb_present_surface_t *s, int x, int y)
{
    const uint8_t *row = (const uint8_t *)s->pixels + (size_t)y * s->stride_bytes;
    if (s->format == FMRB_PRESENT_XRGB8888) return ((const uint32_t *)row)[x];
    return ((const uint16_t *)row)[x];
}

// One surface: draw, then compare every pixel, and check that the padding
// past each row's width is left alone.
static void present_case(fmrb_present_format_t fmt, int rotate, int sw, int sh, int pad_px,
                         int fb_w, int fb_h, int fixed, int want_scale)
{
    int bpp = fmrb_present_bytes_per_pixel(fmt);
    int stride = (sw + pad_px) * bpp;
    uint8_t *mem = malloc((size_t)stride * sh);
    uint16_t *fb = malloc((size_t)fb_w * fb_h * sizeof(uint16_t));
    memset(mem, 0x5a, (size_t)stride * sh);
    make_frame(fb, fb_w, fb_h);
    fmrb_present_surface_t s = { .pixels = mem, .w = sw, .h = sh, .stride_bytes = stride,
                                 .format = fmt, .rotate = rotate };
    fmrb_present_place_t p;
    fmrb_present_place(&s, fb_w, fb_h, fixed, &p);
    fmrb_present_draw(fb, fb_w, fb_h, &s, &p);
    const char *fname = fmt == FMRB_PRESENT_XRGB8888 ? "XRGB8888" : "RGB565";
    CHECK(p.scale == want_scale, "%s rot %d %dx%d frame %dx%d fixed %d: scale %d, want %d",
          fname, rotate, sw, sh, fb_w, fb_h, fixed, p.scale, want_scale);
    int bad = 0, fx0 = -1, fy0 = -1;
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            if (read_px(&s, x, y) != expected_at(fb, fb_w, fb_h, &s, &p, x, y)) {
                if (bad++ == 0) { fx0 = x; fy0 = y; }
            }
        }
        for (int k = sw * bpp; k < stride; k++) {
            if (mem[(size_t)y * stride + k] != 0x5a) bad++;
        }
    }
    CHECK(bad == 0, "%s rot %d %dx%d frame %dx%d: %d pixels wrong, first at (%d,%d)",
          fname, rotate, sw, sh, fb_w, fb_h, bad, fx0, fy0);

    // A drawn pixel and to_frame agree: the point maps back to the frame
    // pixel that was drawn there.
    int mism = 0;
    for (int y = 0; y < sh; y += 7) {
        for (int x = 0; x < sw; x += 5) {
            int fx = 0, fy = 0;
            if (!fmrb_present_to_frame(&p, fb_w, fb_h, x, y, &fx, &fy)) continue;
            uint16_t v = fb[fy * fb_w + fx];
            uint32_t want = fmt == FMRB_PRESENT_XRGB8888 ? fmrb_present_565_to_8888(v) : v;
            if (read_px(&s, x, y) != want) mism++;
        }
    }
    CHECK(mism == 0, "%s rot %d: to_frame disagrees with the drawing at %d points", fname, rotate, mism);
    free(mem);
    free(fb);
}

static void test_present(void)
{
    const fmrb_present_format_t fmts[] = { FMRB_PRESENT_RGB565, FMRB_PRESENT_XRGB8888 };
    for (int f = 0; f < 2; f++) {
        for (int r = 0; r < 360; r += 90) {
            int turned = (r == 90 || r == 270);
            // The HDMI case: 426x240 on 1280x720 at 3x (1 px border left and right).
            present_case(fmts[f], r, turned ? 720 : 1280, turned ? 1280 : 720, 3, 426, 240, 0, 3);
            // The high-resolution frame on the same surface: 2x.
            present_case(fmts[f], r, turned ? 720 : 1280, turned ? 1280 : 720, 0, 640, 360, 0, 2);
            // A fixed scale that fits, and one that does not (falls back).
            present_case(fmts[f], r, turned ? 360 : 640, turned ? 640 : 360, 1, 160, 90, 2, 2);
            present_case(fmts[f], r, turned ? 360 : 640, turned ? 640 : 360, 1, 160, 90, 9, 4);
            // The LCD: 320x170 at 1x, and a frame larger than the surface (cropped).
            present_case(fmts[f], r, turned ? 170 : 320, turned ? 320 : 170, 0, 320, 170, 0, 1);
            present_case(fmts[f], r, turned ? 170 : 320, turned ? 320 : 170, 2, 426, 240, 0, 1);
        }
    }

    // Placement numbers the outputs rely on.
    fmrb_present_surface_t s = { .w = 1280, .h = 720, .rotate = 0 };
    fmrb_present_place_t p;
    fmrb_present_place(&s, 426, 240, 0, &p);
    CHECK(p.scale == 3 && p.x == 1 && p.y == 0 && p.w == 1278 && p.h == 720,
          "426x240 on 1280x720: scale %d at (%d,%d) %dx%d", p.scale, p.x, p.y, p.w, p.h);
    s.w = 720; s.h = 1280; s.rotate = 90;
    fmrb_present_place(&s, 426, 240, 0, &p);
    CHECK(p.view_w == 1280 && p.view_h == 720 && p.scale == 3,
          "portrait panel turned 90: view %dx%d scale %d", p.view_w, p.view_h, p.scale);
    s.w = 1920; s.h = 1200; s.rotate = 0;
    fmrb_present_place(&s, 426, 240, 0, &p);
    CHECK(p.scale == 4 && p.x == 108 && p.y == 120, "426x240 on 1920x1200: scale %d at (%d,%d)",
          p.scale, p.x, p.y);
    int w = 0, h = 0;
    fmrb_present_surface_size(426, 240, 3, &w, &h);
    CHECK(w == 1280 && h == 720, "window for 426x240 at 3: %dx%d", w, h);
    CHECK(fmrb_present_565_to_8888(0xffff) == 0xffffff && fmrb_present_565_to_8888(0xf800) == 0xff0000 &&
          fmrb_present_565_to_8888(0x07e0) == 0x00ff00 && fmrb_present_565_to_8888(0x001f) == 0x0000ff &&
          fmrb_present_565_to_8888(0) == 0, "565 -> 8888 ends");
}

// ---- keys --------------------------------------------------------------------

static void test_keys(void)
{
    // Letters, digits and the punctuation row: the HID usage, and what the
    // firmware's US table makes of it, against the Linux key's legend.
    static const struct { unsigned code; char ch; } legend[] = {
        { KEY_A, 'a' }, { KEY_B, 'b' }, { KEY_C, 'c' }, { KEY_D, 'd' }, { KEY_E, 'e' }, { KEY_F, 'f' },
        { KEY_G, 'g' }, { KEY_H, 'h' }, { KEY_I, 'i' }, { KEY_J, 'j' }, { KEY_K, 'k' }, { KEY_L, 'l' },
        { KEY_M, 'm' }, { KEY_N, 'n' }, { KEY_O, 'o' }, { KEY_P, 'p' }, { KEY_Q, 'q' }, { KEY_R, 'r' },
        { KEY_S, 's' }, { KEY_T, 't' }, { KEY_U, 'u' }, { KEY_V, 'v' }, { KEY_W, 'w' }, { KEY_X, 'x' },
        { KEY_Y, 'y' }, { KEY_Z, 'z' },
        { KEY_1, '1' }, { KEY_2, '2' }, { KEY_3, '3' }, { KEY_4, '4' }, { KEY_5, '5' },
        { KEY_6, '6' }, { KEY_7, '7' }, { KEY_8, '8' }, { KEY_9, '9' }, { KEY_0, '0' },
        { KEY_MINUS, '-' }, { KEY_EQUAL, '=' }, { KEY_LEFTBRACE, '[' }, { KEY_RIGHTBRACE, ']' },
        { KEY_SEMICOLON, ';' }, { KEY_APOSTROPHE, '\'' }, { KEY_GRAVE, '`' }, { KEY_BACKSLASH, '\\' },
        { KEY_COMMA, ',' }, { KEY_DOT, '.' }, { KEY_SLASH, '/' }, { KEY_SPACE, ' ' },
    };
    for (size_t i = 0; i < sizeof(legend) / sizeof(legend[0]); i++) {
        uint8_t u = fmrb_linux_key_to_hid(legend[i].code);
        char c = fmrb_keymap_scancode_to_char(u, 0, FMRB_KEYMAP_LAYOUT_US);
        CHECK(c == legend[i].ch, "KEY %u -> usage 0x%02x -> '%c', want '%c'", legend[i].code, u, c, legend[i].ch);
    }
    static const struct { unsigned code; uint8_t usage; } fixed[] = {
        { KEY_ESC, 0x29 }, { KEY_ENTER, 0x28 }, { KEY_BACKSPACE, 0x2A }, { KEY_TAB, 0x2B },
        { KEY_CAPSLOCK, 0x39 }, { KEY_F1, 0x3A }, { KEY_F10, 0x43 }, { KEY_F11, 0x44 }, { KEY_F12, 0x45 },
        { KEY_SYSRQ, 0x46 }, { KEY_PAUSE, 0x48 }, { KEY_INSERT, 0x49 }, { KEY_HOME, 0x4A },
        { KEY_PAGEUP, 0x4B }, { KEY_DELETE, 0x4C }, { KEY_END, 0x4D }, { KEY_PAGEDOWN, 0x4E },
        { KEY_RIGHT, 0x4F }, { KEY_LEFT, 0x50 }, { KEY_DOWN, 0x51 }, { KEY_UP, 0x52 },
        { KEY_KPENTER, 0x58 }, { KEY_KP0, 0x62 }, { KEY_102ND, 0x64 }, { KEY_COMPOSE, 0x65 },
        { KEY_LEFTCTRL, 0xE0 }, { KEY_LEFTSHIFT, 0xE1 }, { KEY_LEFTALT, 0xE2 }, { KEY_LEFTMETA, 0xE3 },
        { KEY_RIGHTCTRL, 0xE4 }, { KEY_RIGHTSHIFT, 0xE5 }, { KEY_RIGHTALT, 0xE6 }, { KEY_RIGHTMETA, 0xE7 },
        // Japanese keyboards (the kernel's own HID table, inverted).
        { KEY_RO, 0x87 }, { KEY_KATAKANAHIRAGANA, 0x88 }, { KEY_YEN, 0x89 }, { KEY_HENKAN, 0x8A },
        { KEY_MUHENKAN, 0x8B }, { KEY_ZENKAKUHANKAKU, 0x35 },
        { KEY_F13, 0x68 }, { KEY_F24, 0x73 },
        // Outside the keyboard page: nothing.
        { KEY_PLAYPAUSE, 0 }, { KEY_BRIGHTNESSUP, 0 }, { KEY_CD, 0 }, { KEY_RESERVED, 0 }, { 0x2ff, 0 },
    };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        uint8_t u = fmrb_linux_key_to_hid(fixed[i].code);
        CHECK(u == fixed[i].usage, "KEY %u -> 0x%02x, want 0x%02x", fixed[i].code, u, fixed[i].usage);
    }
    // No two keys of the table share a usage, except the half/full-width key.
    int owner[256];
    for (int i = 0; i < 256; i++) owner[i] = -1;
    for (unsigned c = 0; c < 0x300; c++) {
        uint8_t u = fmrb_linux_key_to_hid(c);
        if (u == 0) continue;
        if (owner[u] >= 0 && !(u == 0x35 && (c == KEY_ZENKAKUHANKAKU || owner[u] == KEY_ZENKAKUHANKAKU))) {
            CHECK(0, "usage 0x%02x from both KEY %d and KEY %u", u, owner[u], c);
        }
        owner[u] = (int)c;
    }
    CHECK(fmrb_linux_key_mod_bit(KEY_LEFTSHIFT) == FMRB_KEYMAP_MOD_LSHIFT &&
          fmrb_linux_key_mod_bit(KEY_RIGHTALT) == FMRB_KEYMAP_MOD_RALT &&
          fmrb_linux_key_mod_bit(KEY_RIGHTCTRL) == FMRB_KEYMAP_MOD_RCTRL &&
          fmrb_linux_key_mod_bit(KEY_LEFTMETA) == 0 && fmrb_linux_key_mod_bit(KEY_A) == 0,
          "modifier bits");
}

// ---- the Cardputer Zero's Sym layer -----------------------------------------

static void test_cardputer(void)
{
    // M5Stack's console key table for the keyboard
    // (tca8418_keypad_m5stack_keymap.map, "keycode N = keysym"): the code the
    // Sym layer of the CardputerZero overlay sends, and the character.
    static const struct { unsigned code; char ch; } console[] = {
        { 26, '!' }, { 27, '@' }, { 39, '#' }, { 40, '$' }, { 41, '%' }, { 43, '^' },
        { 51, '&' }, { 52, '*' }, { 53, '(' }, { 94, ')' },
        { 55, '~' }, { 69, '`' }, { 70, '_' }, { 71, '-' }, { 72, '+' }, { 73, '=' },
        { 74, '[' }, { 75, ']' }, { 76, '{' }, { 77, '}' },
        { 79, ';' }, { 80, ':' }, { 81, '\'' }, { 82, '"' }, { 83, '<' }, { 85, '>' },
        { 86, '\\' }, { 89, '|' },
        { 90, ',' }, { 91, '.' }, { 92, '/' }, { 93, '?' },
    };
    for (size_t i = 0; i < sizeof(console) / sizeof(console[0]); i++) {
        uint8_t u = 0;
        bool shift = false;
        bool hit = fmrb_keymap_cardputer_zero(console[i].code, &u, &shift);
        char c = hit ? fmrb_keymap_scancode_to_char(u, shift ? FMRB_KEYMAP_MOD_LSHIFT : 0, FMRB_KEYMAP_LAYOUT_US) : 0;
        CHECK(hit && c == console[i].ch, "Sym code %u -> usage 0x%02x shift %d -> '%c', want '%c'",
              console[i].code, u, shift, c ? c : '?', console[i].ch);
    }
    uint8_t u = 0xff;
    bool shift = true;
    CHECK(fmrb_keymap_cardputer_zero(KEY_CD, &u, &shift) && u == 0, "Sym key swallowed");
    CHECK(fmrb_keymap_cardputer_zero(KEY_DVD, &u, &shift) && u == 0, "Fn key swallowed");
    CHECK(!fmrb_keymap_cardputer_zero(KEY_A, &u, &shift) && !fmrb_keymap_cardputer_zero(KEY_UP, &u, &shift) &&
          !fmrb_keymap_cardputer_zero(KEY_ENTER, &u, &shift), "plain and Fn keys fall through");
    CHECK(fmrb_keymap_extra_find("cardputer_zero") == fmrb_keymap_cardputer_zero &&
          fmrb_keymap_extra_find("") == NULL && fmrb_keymap_extra_find("other") == NULL, "lookup by name");
}

// ---- evdev recordings --------------------------------------------------------

static const struct { const char *name; int value; } k_names[] = {
    { "EV_SYN", EV_SYN }, { "EV_KEY", EV_KEY }, { "EV_REL", EV_REL }, { "EV_MSC", EV_MSC },
    { "SYN_REPORT", SYN_REPORT }, { "SYN_DROPPED", SYN_DROPPED },
    { "REL_X", REL_X }, { "REL_Y", REL_Y }, { "REL_WHEEL", REL_WHEEL }, { "MSC_SCAN", MSC_SCAN },
    { "BTN_LEFT", BTN_LEFT }, { "BTN_RIGHT", BTN_RIGHT }, { "BTN_MIDDLE", BTN_MIDDLE },
    { "KEY_A", KEY_A }, { "KEY_B", KEY_B }, { "KEY_C", KEY_C }, { "KEY_UP", KEY_UP },
    { "KEY_LEFTSHIFT", KEY_LEFTSHIFT }, { "KEY_RIGHTCTRL", KEY_RIGHTCTRL },
    { "KEY_PLAYPAUSE", KEY_PLAYPAUSE }, { "KEY_CD", KEY_CD }, { "KEY_DVD", KEY_DVD },
    { "KEY_LEFTBRACE", KEY_LEFTBRACE }, { "KEY_KP7", KEY_KP7 }, { "KEY_COMMA", KEY_COMMA },
};

static int name_value(const char *s, int *out)
{
    for (size_t i = 0; i < sizeof(k_names) / sizeof(k_names[0]); i++) {
        if (strcmp(k_names[i].name, s) == 0) {
            *out = k_names[i].value;
            return 0;
        }
    }
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

typedef struct {
    char lines[64][64];
    int n;
} capture_t;

static void cap_add(capture_t *c, const char *line)
{
    if (c->n < 64) snprintf(c->lines[c->n++], sizeof(c->lines[0]), "%s", line);
}

static void cap_key(void *ud, bool down, uint8_t usage, uint8_t mods, bool us)
{
    char b[64];
    snprintf(b, sizeof(b), "key %s %02x %02x%s", down ? "down" : "up", usage, mods, us ? " us" : "");
    cap_add(ud, b);
}

static void cap_move(void *ud, int x, int y)
{
    char b[64];
    snprintf(b, sizeof(b), "move %d %d", x, y);
    cap_add(ud, b);
}

static void cap_button(void *ud, int x, int y, int button, bool down)
{
    char b[64];
    snprintf(b, sizeof(b), "button %d %s %d %d", button, down ? "down" : "up", x, y);
    cap_add(ud, b);
}

static void cap_wheel(void *ud, int x, int y, int n)
{
    char b[64];
    snprintf(b, sizeof(b), "wheel %d %d %d", n, x, y);
    cap_add(ud, b);
}

static void test_evdev_file(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/test/native/evdev/%s", g_core, name);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "%s: cannot open", path);
    if (f == NULL) return;

    evdev_geometry_t geo = { 426, 240, 426, 240, 1.0, 1.0 };
    fmrb_keymap_extra_fn extra = NULL;
    struct input_event evs[256];
    int n_ev = 0;
    capture_t want = { .n = 0 };
    bool expecting = false;
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        if (strcmp(line, "--- expect") == 0) { expecting = true; continue; }
        if (expecting) { cap_add(&want, line); continue; }
        char a[64], b[64], c[64];
        if (sscanf(line, "keymap %63s", a) == 1) {
            extra = fmrb_keymap_extra_find(a);
            CHECK(extra != NULL, "%s: no keymap %s", name, a);
            continue;
        }
        if (sscanf(line, "screen %d %d base %d %d scale %lf %lf", &geo.screen_w, &geo.screen_h,
                   &geo.base_w, &geo.base_h, &geo.scale_x, &geo.scale_y) == 6) {
            continue;
        }
        int t = 0, k = 0, v = 0;
        if (sscanf(line, "%63s %63s %63s", a, b, c) != 3 || name_value(a, &t) || name_value(b, &k) ||
            name_value(c, &v) || n_ev >= 256) {
            CHECK(0, "%s: cannot read \"%s\"", name, line);
            continue;
        }
        memset(&evs[n_ev], 0, sizeof(evs[0]));
        evs[n_ev].type = (uint16_t)t;
        evs[n_ev].code = (uint16_t)k;
        evs[n_ev].value = v;
        n_ev++;
    }
    fclose(f);

    // Through a pipe, read the way the device is read (non-blocking, in
    // whole events), in two writes so a batch boundary falls mid-report.
    int fds[2];
    CHECK(pipe(fds) == 0, "pipe");
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    evdev_pointer_t pointer = { 0 };
    evdev_decoder_t dec;
    evdev_decoder_init(&dec, &pointer, extra);
    evdev_sink_t sink = { cap_key, cap_move, cap_button, cap_wheel };
    capture_t got = { .n = 0 };
    int half = n_ev / 2;
    int total = 0;
    CHECK(write(fds[1], evs, sizeof(evs[0]) * (size_t)half) == (ssize_t)(sizeof(evs[0]) * (size_t)half), "write");
    total += evdev_drain_fd(fds[0], &dec, &geo, &sink, &got);
    CHECK(evdev_drain_fd(fds[0], &dec, &geo, &sink, &got) == 0, "%s: an empty pipe reads nothing", name);
    CHECK(write(fds[1], evs + half, sizeof(evs[0]) * (size_t)(n_ev - half)) ==
          (ssize_t)(sizeof(evs[0]) * (size_t)(n_ev - half)), "write");
    total += evdev_drain_fd(fds[0], &dec, &geo, &sink, &got);
    close(fds[1]);
    CHECK(evdev_drain_fd(fds[0], &dec, &geo, &sink, &got) == -1, "%s: a closed device reads as gone", name);
    close(fds[0]);

    CHECK(total == n_ev, "%s: read %d of %d events", name, total, n_ev);
    CHECK(got.n == want.n, "%s: %d calls, want %d", name, got.n, want.n);
    for (int i = 0; i < got.n || i < want.n; i++) {
        const char *g = i < got.n ? got.lines[i] : "(none)";
        const char *w = i < want.n ? want.lines[i] : "(none)";
        CHECK(strcmp(g, w) == 0, "%s: call %d is \"%s\", want \"%s\"", name, i + 1, g, w);
    }
}

static void test_evdev(void)
{
    test_evdev_file("typing.ev");
    test_evdev_file("mouse.ev");
    test_evdev_file("hires.ev");
    test_evdev_file("cardputer.ev");
}

// ---- fbdev ---------------------------------------------------------------------

static void test_fbdev(void)
{
    const char *proc = "0 vc4drmfb\n1 fb_st7789v\n";
    CHECK(native_fbdev_find(proc, "fb_st7789v") == 1, "find by name");
    CHECK(native_fbdev_find(proc, "") == 0, "first");
    CHECK(native_fbdev_find(proc, "nope") == -1, "absent");
    CHECK(native_fbdev_find("", "") == -1, "empty /proc/fb");

    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    fmrb_present_surface_t s;
    size_t off = 0;
    char why[256] = "";
    // The st7789v panel as fbtft reports it: RGB565, 320x170.
    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    var.xres = 320; var.yres = 170; var.bits_per_pixel = 16;
    var.red = (struct fb_bitfield){ 11, 5, 0 };
    var.green = (struct fb_bitfield){ 5, 6, 0 };
    var.blue = (struct fb_bitfield){ 0, 5, 0 };
    fix.type = FB_TYPE_PACKED_PIXELS; fix.visual = FB_VISUAL_TRUECOLOR;
    fix.line_length = 640; fix.smem_len = 640 * 170;
    CHECK(native_fbdev_surface(&var, &fix, 0, &s, &off, why, sizeof(why)) &&
          s.format == FMRB_PRESENT_RGB565 && s.w == 320 && s.h == 170 && s.stride_bytes == 640 && off == 0,
          "RGB565 320x170: %s", why);
    // A 32-bit console with a panned visible area and padded rows.
    var.xres = 1280; var.yres = 720; var.bits_per_pixel = 32; var.yoffset = 720;
    var.red = (struct fb_bitfield){ 16, 8, 0 };
    var.green = (struct fb_bitfield){ 8, 8, 0 };
    var.blue = (struct fb_bitfield){ 0, 8, 0 };
    fix.line_length = 5248; fix.smem_len = 5248 * 1440;
    CHECK(native_fbdev_surface(&var, &fix, 270, &s, &off, why, sizeof(why)) &&
          s.format == FMRB_PRESENT_XRGB8888 && s.rotate == 270 && off == (size_t)720 * 5248,
          "XRGB8888 panned: %s", why);
    // Refused: blue first, 24 bit, a palette, a short buffer.
    var.yoffset = 0;
    var.red = (struct fb_bitfield){ 0, 8, 0 };
    var.blue = (struct fb_bitfield){ 16, 8, 0 };
    CHECK(!native_fbdev_surface(&var, &fix, 0, &s, &off, why, sizeof(why)) && strstr(why, "not RGB565") != NULL,
          "BGR refused (%s)", why);
    var.red = (struct fb_bitfield){ 16, 8, 0 };
    var.blue = (struct fb_bitfield){ 0, 8, 0 };
    var.bits_per_pixel = 24;
    CHECK(!native_fbdev_surface(&var, &fix, 0, &s, &off, why, sizeof(why)), "24 bit refused");
    var.bits_per_pixel = 32;
    fix.visual = FB_VISUAL_PSEUDOCOLOR;
    CHECK(!native_fbdev_surface(&var, &fix, 0, &s, &off, why, sizeof(why)), "palette refused");
    fix.visual = FB_VISUAL_TRUECOLOR;
    fix.smem_len = 5248 * 100;
    CHECK(!native_fbdev_surface(&var, &fix, 0, &s, &off, why, sizeof(why)) && strstr(why, "smaller") != NULL,
          "short memory refused (%s)", why);
}

// ---- machine definitions -------------------------------------------------------

static int load(native_machine_t *m, const char *name, char *err, size_t n)
{
    char dir[512], path[600];
    snprintf(dir, sizeof(dir), "%s/config/machines", g_core);
    native_machine_resolve(name, dir, path, sizeof(path));
    native_machine_defaults(m);
    return native_machine_load_file(m, path, err, n);
}

static void test_machine(void)
{
    native_machine_t m;
    char err[400] = "";

    CHECK(load(&m, "sdl", err, sizeof(err)) == 0, "sdl: %s", err);
    native_machine_t def;
    native_machine_defaults(&def);
    // The file and the built-in definition agree on everything that runs.
    CHECK(strcmp(m.display.backend, def.display.backend) == 0 && strcmp(m.input.backend, def.input.backend) == 0 &&
          strcmp(m.audio.backend, def.audio.backend) == 0 && m.display.scale == def.display.scale &&
          m.display.rotate == def.display.rotate && m.screen.w == def.screen.w && m.screen.h == def.screen.h &&
          strcmp(m.desktop.variant, def.desktop.variant) == 0 && strcmp(m.name, "sdl") == 0,
          "sdl.toml is the built-in default");
    CHECK(native_machine_check(&m, err, sizeof(err)) == 0, "sdl check: %s", err);

    CHECK(load(&m, "cardputer_zero", err, sizeof(err)) == 0, "cardputer_zero: %s", err);
    CHECK(strcmp(m.display.backend, "fbdev") == 0 && strcmp(m.display.device, "auto:fb_st7789v") == 0 &&
          strcmp(m.input.backend, "evdev") == 0 && strcmp(m.input.keymap, "cardputer_zero") == 0 &&
          strcmp(m.input.keymap_device, "tca8418") == 0 && strcmp(m.audio.backend, "alsa") == 0 &&
          m.screen.w == 320 && m.screen.h == 170 && strcmp(m.desktop.variant, "keys") == 0,
          "cardputer_zero fields");
    CHECK(native_machine_check(&m, err, sizeof(err)) == 0, "cardputer_zero check: %s", err);

    // The same screen and desktop in an SDL window (L4).
    CHECK(load(&m, "sdl_keys", err, sizeof(err)) == 0, "sdl_keys: %s", err);
    CHECK(strcmp(m.display.backend, "sdl") == 0 && m.display.scale == 4 && m.screen.w == 320 &&
          m.screen.h == 170 && strcmp(m.desktop.variant, "keys") == 0 &&
          strcmp(m.system.exit_key, "esc_long") == 0, "sdl_keys fields");
    CHECK(native_machine_check(&m, err, sizeof(err)) == 0, "sdl_keys check: %s", err);

    CHECK(load(&m, "cardputer_zero_hdmi", err, sizeof(err)) == 0, "cardputer_zero_hdmi: %s", err);
    CHECK(strcmp(m.display.backend, "drm") == 0 && strcmp(m.display.mode, "1280x720") == 0 &&
          strcmp(m.input.keymap, "cardputer_zero") == 0 && strcmp(m.audio.device, "default") == 0, "hdmi fields");
    CHECK(native_machine_check(&m, err, sizeof(err)) == 0, "hdmi check: %s", err);

    // The power entries by the form (L6): the Cardputer's two end the
    // program, the PC asks logind, the development windows end the program.
    CHECK(strcmp(m.system.power, "exit") == 0 && strcmp(m.system.services, "linux") == 0 &&
          strcmp(m.audio.mixer, "auto") == 0 && !m.system.power_log_only, "hdmi power exit, mixer auto");
    CHECK(load(&def, "sdl_keys", err, sizeof(err)) == 0 && strcmp(def.system.power, "exit") == 0 &&
          strcmp(def.system.services, "linux") == 0, "sdl_keys power exit, services linux");
    CHECK(load(&def, "cardputer_zero", err, sizeof(err)) == 0 && strcmp(def.system.power, "exit") == 0,
          "cardputer_zero power exit");
    CHECK(load(&m, "linux_pc_drm", err, sizeof(err)) == 0, "linux_pc_drm: %s", err);
    CHECK(strcmp(m.system.power, "logind") == 0, "linux_pc_drm power logind");
    CHECK(strcmp(m.display.mode, "auto") == 0 && m.input.keymap[0] == '\0' &&
          m.input.n_devices == 1 && strcmp(m.input.devices[0], "auto") == 0, "linux_pc_drm fields");

    // --set and its refusals.
    native_machine_defaults(&m);
    CHECK(native_machine_set(&m, "system.power", "logind", err, sizeof(err)) == 0 &&
          native_machine_set(&m, "system.power_log_only", "true", err, sizeof(err)) == 0 &&
          strcmp(m.system.power, "logind") == 0 && m.system.power_log_only, "--set power logind, log only");
    CHECK(native_machine_set(&m, "system.power", "halt", err, sizeof(err)) != 0, "power halt refused");
    CHECK(native_machine_set(&m, "system.power_log_only", "yes", err, sizeof(err)) != 0, "power_log_only yes refused");
    CHECK(native_machine_set(&m, "audio.mixer", "Speaker", err, sizeof(err)) == 0 &&
          strcmp(m.audio.mixer, "Speaker") == 0, "--set mixer");
    CHECK(native_machine_set(&m, "audio.mixer", "", err, sizeof(err)) != 0, "empty mixer refused");
    native_machine_defaults(&m);
    CHECK(native_machine_set(&m, "display.rotate", "270", err, sizeof(err)) == 0 && m.display.rotate == 270, "rotate");
    CHECK(native_machine_set(&m, "display.rotate", "45", err, sizeof(err)) != 0, "rotate 45 refused");
    CHECK(native_machine_set(&m, "display.scale", "auto", err, sizeof(err)) == 0 && m.display.scale == 0, "scale auto");
    CHECK(native_machine_set(&m, "display.scale", "2", err, sizeof(err)) == 0 && m.display.scale == 2, "scale 2");
    CHECK(native_machine_set(&m, "display.mode", "1920x1080@60", err, sizeof(err)) == 0, "mode with rate");
    CHECK(native_machine_set(&m, "display.mode", "big", err, sizeof(err)) != 0, "bad mode refused");
    CHECK(native_machine_set(&m, "input.devices", "/dev/input/event3, /dev/input/by-path/x-event-kbd",
                             err, sizeof(err)) == 0 &&
          m.input.n_devices == 2 && strcmp(m.input.devices[1], "/dev/input/by-path/x-event-kbd") == 0, "devices");
    CHECK(native_machine_set(&m, "display.colour", "red", err, sizeof(err)) != 0 && strstr(err, "unknown") != NULL,
          "unknown key refused (%s)", err);
    CHECK(native_machine_set(&m, "desktop.variant", "keys", err, sizeof(err)) == 0 &&
          native_machine_check(&m, err, sizeof(err)) == 0, "keys desktop accepted (%s)", err);
    CHECK(native_machine_set(&m, "desktop.variant", "touch", err, sizeof(err)) == 0 &&
          native_machine_check(&m, err, sizeof(err)) != 0 && strstr(err, "mouse or keys") != NULL,
          "unknown desktop refused (%s)", err);
    native_machine_defaults(&m);
    CHECK(native_machine_set(&m, "screen.form", "640x480", err, sizeof(err)) == 0 &&
          native_machine_check(&m, err, sizeof(err)) != 0 && strstr(err, "640x480") != NULL,
          "unknown screen refused (%s)", err);

    // The PC's login-screen session (L7): full screen SDL, both power groups.
    CHECK(load(&m, "linux_pc", err, sizeof(err)) == 0, "linux_pc: %s", err);
    CHECK(strcmp(m.display.backend, "sdl") == 0 && m.display.fullscreen && m.display.scale == 0 &&
          m.display.rotate == 0 && strcmp(m.input.backend, "sdl") == 0 && strcmp(m.audio.backend, "sdl") == 0 &&
          strcmp(m.desktop.variant, "mouse") == 0 && m.screen.w == 426, "linux_pc fields");
    CHECK(strcmp(m.system.power, "logind,exit") == 0 && native_machine_power_has(&m, "logind") &&
          native_machine_power_has(&m, "exit"), "linux_pc power logind and exit (%s)", m.system.power);
    CHECK(strcmp(m.files.host, "auto") == 0 && strcmp(m.files.root, "auto") == 0, "linux_pc files");
    CHECK(native_machine_check(&m, err, sizeof(err)) == 0, "linux_pc check: %s", err);
    native_machine_defaults(&m);
    CHECK(!m.display.fullscreen && strcmp(m.files.host, "auto") == 0 && strcmp(m.files.root, "auto") == 0,
          "defaults: a window, both Linux places shown");
    CHECK(native_machine_power_has(&m, "exit") && !native_machine_power_has(&m, "logind") &&
          !native_machine_power_has(&m, "exi"), "default power exit only");
    CHECK(native_machine_set(&m, "system.power", "exit, logind", err, sizeof(err)) == 0 &&
          strcmp(m.system.power, "exit,logind") == 0, "power list (%s)", m.system.power);
    CHECK(native_machine_set(&m, "system.power", "exit,exit", err, sizeof(err)) != 0, "power twice refused");
    CHECK(native_machine_set(&m, "system.power", "", err, sizeof(err)) != 0, "empty power refused");
    CHECK(native_machine_set(&m, "system.power", "logind,halt", err, sizeof(err)) != 0, "power list with halt refused");
    CHECK(native_machine_set(&m, "display.fullscreen", "true", err, sizeof(err)) == 0 && m.display.fullscreen,
          "--set fullscreen");
    CHECK(native_machine_set(&m, "display.fullscreen", "yes", err, sizeof(err)) != 0, "fullscreen yes refused");
    CHECK(native_machine_set(&m, "files.host", "none", err, sizeof(err)) == 0 &&
          native_machine_set(&m, "files.root", "/srv", err, sizeof(err)) == 0 &&
          strcmp(m.files.host, "none") == 0 && strcmp(m.files.root, "/srv") == 0, "--set files");
    CHECK(native_machine_set(&m, "files.host", "home", err, sizeof(err)) != 0, "relative files.host refused");
    // Every shipped machine shows both places (the user's decision, 2026-10-27).
    static const char *const shipped[] = { "sdl", "sdl_keys", "cardputer_zero", "cardputer_zero_hdmi",
                                           "linux_pc_drm", "linux_pc" };
    for (size_t i = 0; i < sizeof(shipped) / sizeof(shipped[0]); i++) {
        CHECK(load(&m, shipped[i], err, sizeof(err)) == 0 && strcmp(m.files.host, "auto") == 0 &&
              strcmp(m.files.root, "auto") == 0, "%s shows /mnt/host and /mnt/root", shipped[i]);
    }

    char path[256];
    native_machine_resolve("sdl", "/d", path, sizeof(path));
    CHECK(strcmp(path, "/d/sdl.toml") == 0, "resolve name: %s", path);
    native_machine_resolve("my.toml", "/d", path, sizeof(path));
    CHECK(strcmp(path, "my.toml") == 0, "resolve file: %s", path);
    native_machine_resolve("./x/y", "/d", path, sizeof(path));
    CHECK(strcmp(path, "./x/y") == 0, "resolve path: %s", path);

    // A file with an unknown section or key is refused, with its name.
    char tmp[] = "/tmp/fmrb_native_machine_XXXXXX";
    int fd = mkstemp(tmp);
    CHECK(fd >= 0, "mkstemp");
    const char *bad = "[display]\nbackend = \"drm\"\nflip = true\n";
    CHECK(write(fd, bad, strlen(bad)) == (ssize_t)strlen(bad), "write");
    close(fd);
    native_machine_defaults(&m);
    CHECK(native_machine_load_file(&m, tmp, err, sizeof(err)) != 0 && strstr(err, "display.flip") != NULL,
          "unknown key in a file (%s)", err);
    unlink(tmp);
}

// ------------------------------------------------------------------ etc

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf != NULL) {
        buf[n] = '\0';
        *len = (size_t)n;
    }
    return buf;
}

static void etc_log(void *ctx, const char *line)
{
    int *n = ctx;
    (*n)++;
    printf("  etc: %s\n", line);
}

// Every file of a against b (same names, same bytes).
static int same_dirs(const char *a, const char *b)
{
    int diff = 0;
    for (int pass = 0; pass < 2; pass++) {
        const char *x = pass ? b : a, *y = pass ? a : b;
        DIR *d = opendir(x);
        if (d == NULL) return -1;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char px[512], py[512];
            snprintf(px, sizeof(px), "%s/%s", x, e->d_name);
            snprintf(py, sizeof(py), "%s/%s", y, e->d_name);
            size_t lx = 0, ly = 0;
            char *bx = slurp(px, &lx), *by = slurp(py, &ly);
            if (bx == NULL || by == NULL || lx != ly || memcmp(bx, by, lx) != 0) {
                printf("  etc: %s differs from %s\n", px, py);
                diff++;
            }
            free(bx);
            free(by);
        }
        closedir(d);
    }
    return diff;
}

static void test_etc(void)
{
    char seed[256], old[256], expected[256], work[] = "/tmp/fmrb_native_etc_XXXXXX";
    snprintf(seed, sizeof(seed), "%s/test/native/etc/seed", g_core);
    snprintf(old, sizeof(old), "%s/test/native/etc/old", g_core);
    snprintf(expected, sizeof(expected), "%s/test/native/etc/expected", g_core);
    CHECK(mkdtemp(work) != NULL, "mkdtemp");
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp %s/* %s/", old, work);
    CHECK(system(cmd) == 0, "copy old etc");

    int lines = 0;
    int added = native_etc_update(seed, work, etc_log, &lines);
    // system_conf: zenoh_router, greeting, wallpaper, theme.dir_color,
    // [clock_face], [[launcher_exclude]] x2; services: [net], [timesync];
    // hid_devices.toml copied; the factory copy does not parse (left alone).
    CHECK(added == 10, "additions %d, want 10", added);
    CHECK(same_dirs(work, expected) == 0, "the updated etc is not test/native/etc/expected");

    lines = 0;
    added = native_etc_update(seed, work, etc_log, &lines);
    CHECK(added == 0, "second run added %d", added);
    CHECK(same_dirs(work, expected) == 0, "the second run changed the etc");

    // No user etc yet: nothing to do (the first start copies the seed).
    char none[300];
    snprintf(none, sizeof(none), "%s/none", work);
    CHECK(native_etc_update(seed, none, etc_log, &lines) == 0, "missing etc");
    struct stat st;
    CHECK(stat(none, &st) != 0, "a missing etc is not created");

    snprintf(cmd, sizeof(cmd), "rm -rf %s", work);
    CHECK(system(cmd) == 0, "clean up");
}

// ---- sys: the battery and the network link from a made-up sysfs / procfs (L6)

static char g_tree[256];

static void put_file(const char *rel, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tree, rel);
    for (char *p = path + strlen(g_tree) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0755);
            *p = '/';
        }
    }
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "cannot write %s", path);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void put_dir(const char *rel)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/.keep", g_tree, rel);
    put_file(path + strlen(g_tree) + 1, "");
}

static void new_tree(void)
{
    if (g_tree[0]) {
        char cmd[300];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tree);
        if (system(cmd) != 0) printf("cannot remove %s\n", g_tree);
    }
    snprintf(g_tree, sizeof(g_tree), "/tmp/fmrb-native-sys-XXXXXX");
    CHECK(mkdtemp(g_tree) != NULL, "mkdtemp");
    put_dir("sys/class/power_supply");
    put_dir("sys/class/net");
    put_file("proc/net/route", "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n");
    put_file("proc/net/wireless",
             "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
             " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n");
}

static void test_sys(void)
{
    char sys[300], proc[300];
    sys_linux_battery_t b;
    sys_linux_link_t l;

    // Battery: none (a mains supply only), there, charging, full, from charge_*.
    new_tree();
    snprintf(sys, sizeof(sys), "%s/sys", g_tree);
    snprintf(proc, sizeof(proc), "%s/proc", g_tree);
    CHECK(!sys_linux_battery_read(sys, &b), "no power_supply: no battery");
    put_file("sys/class/power_supply/AC/type", "Mains\n");
    put_file("sys/class/power_supply/AC/online", "1\n");
    CHECK(!sys_linux_battery_read(sys, &b), "mains only: no battery");
    put_file("sys/class/power_supply/BAT0/type", "Battery\n");
    put_file("sys/class/power_supply/BAT0/present", "1\n");
    put_file("sys/class/power_supply/BAT0/capacity", "87\n");
    put_file("sys/class/power_supply/BAT0/status", "Discharging\n");
    CHECK(sys_linux_battery_read(sys, &b) && b.percent == 87 && !b.charging && strcmp(b.name, "BAT0") == 0,
          "BAT0 87%% discharging: %d %d %s", b.percent, b.charging, b.name);
    put_file("sys/class/power_supply/BAT0/status", "Charging\n");
    CHECK(sys_linux_battery_read(sys, &b) && b.percent == 87 && b.charging, "BAT0 charging");
    put_file("sys/class/power_supply/BAT0/status", "Full\n");
    put_file("sys/class/power_supply/BAT0/capacity", "104\n");
    CHECK(sys_linux_battery_read(sys, &b) && b.percent == 100 && !b.charging, "full, clamped to 100");
    put_file("sys/class/power_supply/BAT0/present", "0\n");
    CHECK(!sys_linux_battery_read(sys, &b), "a battery not present is none");
    put_file("sys/class/power_supply/BAT1/type", "Battery\n");
    put_file("sys/class/power_supply/BAT1/charge_now", "1500000\n");
    put_file("sys/class/power_supply/BAT1/charge_full", "2000000\n");
    put_file("sys/class/power_supply/BAT1/status", "Charging\n");
    CHECK(sys_linux_battery_read(sys, &b) && b.percent == 75 && b.charging && strcmp(b.name, "BAT1") == 0,
          "BAT1 from charge_now/charge_full: %d", b.percent);

    // Network: no interface, loopback only, wired with the default route,
    // wireless connected with its signal, wireless not connected.
    new_tree();
    snprintf(sys, sizeof(sys), "%s/sys", g_tree);
    snprintf(proc, sizeof(proc), "%s/proc", g_tree);
    CHECK(!sys_linux_link_read(sys, proc, &l), "no interface");
    put_file("sys/class/net/lo/type", "772\n");
    put_file("sys/class/net/lo/operstate", "unknown\n");
    CHECK(!sys_linux_link_read(sys, proc, &l), "loopback only is no interface");
    put_file("sys/class/net/eth0/type", "1\n");
    put_file("sys/class/net/eth0/operstate", "down\n");
    CHECK(sys_linux_link_read(sys, proc, &l) && !l.connected && !l.wireless && strcmp(l.iface, "eth0") == 0 &&
          l.signal == -1, "wired, no route: not connected");
    put_file("sys/class/net/eth0/operstate", "up\n");
    CHECK(sys_linux_link_read(sys, proc, &l) && !l.connected, "wired up but no default route: not connected");
    put_file("proc/net/route",
             "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n"
             "eth0\t0002A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\n"
             "eth0\t00000000\t0102A8C0\t0003\t0\t0\t100\t00000000\n");
    CHECK(sys_linux_link_read(sys, proc, &l) && l.connected && !l.wireless && strcmp(l.iface, "eth0") == 0 &&
          l.signal == -1, "wired with the default route: connected");
    put_file("sys/class/net/wlan0/type", "1\n");
    put_file("sys/class/net/wlan0/operstate", "up\n");
    put_dir("sys/class/net/wlan0/wireless");
    put_file("proc/net/wireless",
             "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
             " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
             " wlan0: 0000   49.  -61.  -256        0      0      0      0     12        0\n");
    put_file("proc/net/route",
             "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n"
             "wlan0\t00000000\t0102A8C0\t0003\t0\t0\t600\t00000000\n");
    CHECK(sys_linux_link_read(sys, proc, &l) && l.connected && l.wireless && strcmp(l.iface, "wlan0") == 0 &&
          l.signal == 70, "wireless with the route: connected, signal 49/70 -> %d", l.signal);
    put_file("sys/class/net/wlan0/operstate", "dormant\n");
    CHECK(sys_linux_link_read(sys, proc, &l) && !l.connected && l.wireless && strcmp(l.iface, "wlan0") == 0 &&
          l.signal == -1, "wireless not associated (dormant): not connected");
    put_file("proc/net/route", "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n");
    put_file("sys/class/net/wlan0/operstate", "down\n");
    CHECK(sys_linux_link_read(sys, proc, &l) && !l.connected && strcmp(l.iface, "wlan0") == 0,
          "no route: the wireless interface is the one shown");
    CHECK(sys_linux_wireless_signal(proc, "wlan1") == -1, "no line for wlan1");

    // The mixer's steps and logind's methods.
    CHECK(sys_linux_mixer_to_step(0, 0, 255, 10) == 0 && sys_linux_mixer_to_step(255, 0, 255, 10) == 10 &&
          sys_linux_mixer_to_step(128, 0, 255, 10) == 5 && sys_linux_mixer_to_step(-5, 0, 255, 10) == 0,
          "mixer -> step");
    CHECK(sys_linux_step_to_mixer(0, -10239, 400, 10) == -10239 && sys_linux_step_to_mixer(10, -10239, 400, 10) == 400 &&
          sys_linux_mixer_to_step(sys_linux_step_to_mixer(7, 0, 127, 10), 0, 127, 10) == 7, "step -> mixer, round trip");
    int round_trip_ok = 1;
    for (int st = 0; st <= 10; st++) {
        if (sys_linux_mixer_to_step(sys_linux_step_to_mixer(st, 0, 63, 10), 0, 63, 10) != st) round_trip_ok = 0;
    }
    CHECK(round_trip_ok, "every step survives a 0..63 mixer");
    CHECK(strcmp(sys_linux_logind_method(FMRB_SYS_POWER_OFF), "PowerOff") == 0 &&
          strcmp(sys_linux_logind_method(FMRB_SYS_POWER_RESTART), "Reboot") == 0 &&
          strcmp(sys_linux_logind_method(FMRB_SYS_POWER_SUSPEND), "Suspend") == 0 &&
          sys_linux_logind_method(FMRB_SYS_POWER_EXIT) == NULL &&
          sys_linux_logind_method(FMRB_SYS_POWER_RESET) == NULL, "logind methods");

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_tree);
    if (system(cmd) != 0) printf("cannot remove %s\n", g_tree);
}

int main(int argc, char **argv)
{
    if (argc > 1) g_core = argv[1];
    test_present();
    test_keys();
    test_cardputer();
    test_evdev();
    test_fbdev();
    test_machine();
    test_etc();
    test_sys();
    if (g_fail) {
        printf("native: %d of %d checks failed\n", g_fail, g_count);
        return 1;
    }
    printf("native: %d checks passed (present, keys, evdev, cardputer, fbdev, machine, etc, sys)\n", g_count);
    return 0;
}
