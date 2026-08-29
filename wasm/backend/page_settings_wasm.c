/*
 * page_settings_wasm.c - apply the page's settings from inside the machine.
 *
 * The page CANNOT edit the packed files itself: under PROXY_TO_PTHREAD the
 * browser main thread and the thread running main() each have their own
 * MEMFS, and a preRun FS.writeFile lands in the wrong one (found the hard
 * way -- doc/wasm/report/p5.md addendum). So the page passes its settings as
 * argv (Module.arguments) and this code, running on the machine's own thread
 * against the machine's own FS, edits /flash/etc/system_conf.toml before the
 * kernel reads it.
 *
 *   --fmrb-res=WxH        internal framebuffer size (display_width/height,
 *                         with default_user_app_* kept in proportion)
 *   --fmrb-theme=classic  the device palette + the device (western) wallpaper
 *
 * The same flags work on the node build (rake wasm:run -- --fmrb-res=852x480),
 * which is also how this file is regression-tested headlessly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONF_PATH "/flash/etc/system_conf.toml"
#define CONF_MAX  (32 * 1024)

/* The shipped window default (386x180 of 426x240) kept in proportion. */
#define USER_APP_W_NUM 386
#define USER_APP_H_NUM 180
#define BASE_W 426
#define BASE_H 240

/* The device palette, mirroring config/system_conf_p4.toml. The web default
 * (cyberpunk) is what the bundle ships; this is the way back. */
static const char *CLASSIC_THEME[][2] = {
    { "desktop_bg", "0xF6" }, { "menu_bg", "0xC5" }, { "window_bg", "0xFF" },
    { "text", "0x00" },       { "text_light", "0xFF" }, { "highlight", "0xEE" },
    { "border", "0x60" },     { "button", "0x60" },     { "dir_color", "0x03" },
};

static int s_res_w, s_res_h;
static int s_classic;

/* Replace the value of "key = <token>" on its own line. Returns 1 if found. */
static int conf_set(char *conf, size_t cap, const char *key, const char *value)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\n%s = ", key);
    char *at = strstr(conf, pat);
    if (!at && strncmp(conf, pat + 1, strlen(pat) - 1) == 0) at = conf - 1;
    if (!at) return 0;
    char *val = at + strlen(pat);
    char *eol = val;
    while (*eol && *eol != '\n' && *eol != '#' && *eol != ' ') eol++;
    size_t tail = strlen(eol);
    if ((size_t)(val - conf) + strlen(value) + tail + 1 > cap) return 0;
    memmove(val + strlen(value), eol, tail + 1);
    memcpy(val, value, strlen(value));
    return 1;
}

static void copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) { fprintf(stderr, "page settings: cannot read %s\n", from); return; }
    FILE *out = fopen(to, "wb");
    if (!out) { fclose(in); fprintf(stderr, "page settings: cannot write %s\n", to); return; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
}

void fmrb_wasm_page_settings_parse(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int w, h;
        if (sscanf(argv[i], "--fmrb-res=%dx%d", &w, &h) == 2 &&
            w >= BASE_W && w <= 1920 && h >= BASE_H && h <= 1080) {
            s_res_w = w;
            s_res_h = h;
        } else if (strcmp(argv[i], "--fmrb-theme=classic") == 0) {
            s_classic = 1;
        }
    }
}

void fmrb_wasm_page_settings_apply(void)
{
    if (!s_res_w && !s_classic) return;

    char *conf = malloc(CONF_MAX);
    if (!conf) return;
    FILE *f = fopen(CONF_PATH, "rb");
    if (!f) { fprintf(stderr, "page settings: cannot read %s\n", CONF_PATH); free(conf); return; }
    size_t len = fread(conf, 1, CONF_MAX - 1, f);
    fclose(f);
    conf[len] = '\0';

    char num[16];
    if (s_res_w) {
        snprintf(num, sizeof(num), "%d", s_res_w);
        conf_set(conf, CONF_MAX, "display_width", num);
        snprintf(num, sizeof(num), "%d", s_res_h);
        conf_set(conf, CONF_MAX, "display_height", num);
        snprintf(num, sizeof(num), "%d", s_res_w * USER_APP_W_NUM / BASE_W);
        conf_set(conf, CONF_MAX, "default_user_app_width", num);
        snprintf(num, sizeof(num), "%d", s_res_h * USER_APP_H_NUM / BASE_H);
        conf_set(conf, CONF_MAX, "default_user_app_height", num);
        printf("page settings: resolution %dx%d\n", s_res_w, s_res_h);
    }
    if (s_classic) {
        for (size_t i = 0; i < sizeof(CLASSIC_THEME) / sizeof(CLASSIC_THEME[0]); i++) {
            conf_set(conf, CONF_MAX, CLASSIC_THEME[i][0], CLASSIC_THEME[i][1]);
        }
        copy_file("/flash/usr/share/backgrounds/bg_426x240.png",
                  "/flash/data/bg_426x240.png");
        printf("page settings: classic theme\n");
    }

    f = fopen(CONF_PATH, "wb");
    if (!f) { fprintf(stderr, "page settings: cannot write %s\n", CONF_PATH); free(conf); return; }
    fwrite(conf, 1, strlen(conf), f);
    fclose(f);
    free(conf);
}
