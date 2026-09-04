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
 *   --fmrb-conf=KEY=VALUE a setting the machine's own Config dialog saved,
 *                         handed back so a Save survives the reload
 *
 * There was a --fmrb-theme=light too, behind a selector on the page. Colours
 * belong to Config, which has three presets to the page's two and is the only
 * place the machine itself can be asked; one setting with two doors needed a
 * rule about which won, and the rule did not hold.
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

static int s_res_w, s_res_h;

/* Settings the machine's own Config dialog wrote, handed back by the page.
 * /etc is rebuilt from the bundle on every visit, so without this a Save is
 * forgotten the moment the tab is reloaded. Key and value arrive exactly as
 * they appear in the file, quotes and all. */
/* Room for every key the page carries (CONF_KEYS in wasm/web/main.js, 19 as
 * of this writing) plus the startup_app that ?app= adds. */
#define CONF_OVERRIDE_MAX 24
#define CONF_TOKEN_MAX 32
/* Values are longer than keys: `wallpaper` carries a path, and
 * "/usr/share/backgrounds/bg_cyber_426x240.png" with its quotes is 45
 * characters. Anything longer is dropped rather than truncated (a half path
 * would name no file at all). */
#define CONF_VALUE_MAX 96
static char s_conf_key[CONF_OVERRIDE_MAX][CONF_TOKEN_MAX];
static char s_conf_val[CONF_OVERRIDE_MAX][CONF_VALUE_MAX];
static int s_conf_n;

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

void fmrb_wasm_page_settings_parse(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int w, h;
        if (sscanf(argv[i], "--fmrb-res=%dx%d", &w, &h) == 2 &&
            w >= BASE_W && w <= 1920 && h >= BASE_H && h <= 1080) {
            s_res_w = w;
            s_res_h = h;
        } else if (strncmp(argv[i], "--fmrb-conf=", 12) == 0 &&
                   s_conf_n < CONF_OVERRIDE_MAX) {
            const char *kv = argv[i] + 12;
            const char *eq = strchr(kv, '=');
            if (eq && (size_t)(eq - kv) < CONF_TOKEN_MAX &&
                strlen(eq + 1) < CONF_VALUE_MAX) {
                memcpy(s_conf_key[s_conf_n], kv, (size_t)(eq - kv));
                s_conf_key[s_conf_n][eq - kv] = '\0';
                snprintf(s_conf_val[s_conf_n], CONF_VALUE_MAX, "%s", eq + 1);
                s_conf_n++;
            }
        }
    }
}

void fmrb_wasm_page_settings_apply(void)
{
    if (!s_res_w && !s_conf_n) return;

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
    /* What Config saved, put back over the bundle's defaults. */
    for (int i = 0; i < s_conf_n; i++) {
        if (conf_set(conf, CONF_MAX, s_conf_key[i], s_conf_val[i])) {
            printf("page settings: %s = %s\n", s_conf_key[i], s_conf_val[i]);
        }
    }

    /* No wallpaper is staged here any more. The desktop picks one the
     * moment it starts (system_desktop.app.rb, theme_wallpaper) from the
     * theme actually in force and the window's actual size, and syncs it to
     * the same /flash/data name -- the same choice this made, from better
     * information, a moment later. Doing it twice only meant this copy could
     * disagree with Config. */

    f = fopen(CONF_PATH, "wb");
    if (!f) { fprintf(stderr, "page settings: cannot write %s\n", CONF_PATH); free(conf); return; }
    fwrite(conf, 1, strlen(conf), f);
    fclose(f);
    free(conf);
}
