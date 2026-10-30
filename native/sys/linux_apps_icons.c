/*
 * linux_apps_icons.c - the icons of the launcher's Linux applications: PNG and
 * SVG made into Family mruby's BMP, the icon themes (L9e) and Qt's look
 * (L9e).
 * Split from linux_apps.c (L16), which has the overview; what the files
 * share is in linux_apps_internal.h.
 */

#include "linux_apps_internal.h"

/* -------------------------------------------------------------- icons */

#if NATIVE_HAVE_PNG
/* RGB332, black kept off 0 (the sprites' transparent colour) and 0x01 (the
 * compositor's hole colour; never a visible colour). */
static uint8_t rgb332(unsigned r, unsigned g, unsigned b)
{
    uint8_t v = (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
    if (v == 0x00) v = 0x25;
    if (v == 0x01) v = 0x02;
    return v;
}

/* An 8-bit BMP whose pixel bytes are RGB332 (the sprite loader reads them as
 * colours; the palette is there for image viewers), as tool/gen_icon_bmp.rb
 * writes them. Written to a temporary name and renamed into place. */
static bool write_bmp(const char *path, const uint8_t *px, int w, int h)
{
    int pad = (4 - (w % 4)) % 4;
    uint32_t pal_bytes = 256 * 4;
    uint32_t body = (uint32_t)((w + pad) * h);
    uint32_t off = 14 + 40 + pal_bytes;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    uint32_t total = off + body;
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(&hdr[2], &total, 4);
    memcpy(&hdr[10], &off, 4);
    uint32_t dib = 40;
    int32_t iw = w, ih = h;
    uint16_t planes = 1, bpp = 8;
    uint32_t ncol = 256, res = 2835;
    memcpy(&hdr[14], &dib, 4);
    memcpy(&hdr[18], &iw, 4);
    memcpy(&hdr[22], &ih, 4);
    memcpy(&hdr[26], &planes, 2);
    memcpy(&hdr[28], &bpp, 2);
    memcpy(&hdr[34], &body, 4);
    memcpy(&hdr[38], &res, 4);
    memcpy(&hdr[42], &res, 4);
    memcpy(&hdr[46], &ncol, 4);
    char tmp[768];
    snprintf(tmp, sizeof(tmp), "%s.tmp%ld", path, (long)getpid());
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) return false;
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    for (int v = 0; v < 256 && ok; v++) {
        uint8_t e[4] = { (uint8_t)((v & 0x03) * 255 / 3), (uint8_t)(((v >> 2) & 0x07) * 255 / 7),
                         (uint8_t)(((v >> 5) & 0x07) * 255 / 7), 0 };
        ok = fwrite(e, 1, 4, f) == 4;
    }
    static const uint8_t zeros[4] = { 0 };
    for (int y = h - 1; y >= 0 && ok; y--) {
        ok = fwrite(px + (size_t)y * (size_t)w, 1, (size_t)w, f) == (size_t)w;
        if (ok && pad) ok = fwrite(zeros, 1, (size_t)pad, f) == (size_t)pad;
    }
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

/* RGBA (straight alpha) fitted into the icon square (aspect kept, centred):
 * each icon pixel is the alpha-weighted mean of the source pixels under it,
 * and opaque when at least half covered. */
static void rgba_to_icon(const uint8_t *rgba, int w, int h, uint8_t *px)
{
    const int n = LINUX_APPS_ICON_PX;
    int big = w > h ? w : h;
    int dw = w * n / big, dh = h * n / big;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = (n - dw) / 2, oy = (n - dh) / 2;
    memset(px, 0, (size_t)n * n);
    for (int y = 0; y < dh; y++) {
        int sy0 = y * h / dh, sy1 = (y + 1) * h / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int sx0 = x * w / dw, sx1 = (x + 1) * w / dw;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint64_t r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int sy = sy0; sy < sy1 && sy < h; sy++) {
                const uint8_t *row = rgba + (size_t)sy * (size_t)w * 4;
                for (int sx = sx0; sx < sx1 && sx < w; sx++) {
                    const uint8_t *p = row + (size_t)sx * 4;
                    r += (uint64_t)p[0] * p[3];
                    g += (uint64_t)p[1] * p[3];
                    b += (uint64_t)p[2] * p[3];
                    a += p[3];
                    cnt++;
                }
            }
            if (cnt == 0 || a == 0 || a / cnt < 128) continue;
            px[(oy + y) * n + ox + x] = rgb332((unsigned)(r / a), (unsigned)(g / a), (unsigned)(b / a));
        }
    }
}

static bool png_to_icon(const char *src, uint8_t *px)
{
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, src)) return false;
    if (img.width == 0 || img.height == 0 || img.width > 2048 || img.height > 2048) {
        png_image_free(&img);
        return false;
    }
    img.format = PNG_FORMAT_RGBA;
    size_t size = PNG_IMAGE_SIZE(img);
    uint8_t *rgba = malloc(size);
    if (rgba == NULL) {
        png_image_free(&img);
        return false;
    }
    if (!png_image_finish_read(&img, NULL, rgba, 0, NULL)) {
        free(rgba);
        return false;
    }
    rgba_to_icon(rgba, (int)img.width, (int)img.height, px);
    free(rgba);
    return true;
}

/* SVG through librsvg and cairo, opened when first needed (dlopen: the
 * program runs without them, and then has no SVG icons). Rendered at
 * SVG_PX square and fitted down like a PNG. */
#define SVG_PX 96

typedef struct {
    double x, y, width, height;
} svg_rect_t;    /* RsvgRectangle */

static struct {
    pthread_once_t once;
    bool ok;
    void *(*handle_new)(const char *file, void **error);
    int (*render)(void *handle, void *cr, const svg_rect_t *viewport, void **error);
    void (*unref)(void *obj);
    void (*error_free)(void *error);
    void *(*surface_create)(int format, int w, int h);
    void *(*create)(void *surface);
    void (*destroy)(void *cr);
    void (*surface_flush)(void *surface);
    unsigned char *(*surface_data)(void *surface);
    int (*surface_stride)(void *surface);
    void (*surface_destroy)(void *surface);
} s_svg = { .once = PTHREAD_ONCE_INIT };

static void svg_open(void)
{
    void *lib = dlopen("librsvg-2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) {
        fprintf(stderr, "native: linux apps: no librsvg (%s): SVG icons are left out\n", dlerror());
        return;
    }
    /* cairo, GObject and GLib come with it (dlsym searches its dependencies). */
    *(void **)&s_svg.handle_new = dlsym(lib, "rsvg_handle_new_from_file");
    *(void **)&s_svg.render = dlsym(lib, "rsvg_handle_render_document");
    *(void **)&s_svg.unref = dlsym(lib, "g_object_unref");
    *(void **)&s_svg.error_free = dlsym(lib, "g_error_free");
    *(void **)&s_svg.surface_create = dlsym(lib, "cairo_image_surface_create");
    *(void **)&s_svg.create = dlsym(lib, "cairo_create");
    *(void **)&s_svg.destroy = dlsym(lib, "cairo_destroy");
    *(void **)&s_svg.surface_flush = dlsym(lib, "cairo_surface_flush");
    *(void **)&s_svg.surface_data = dlsym(lib, "cairo_image_surface_get_data");
    *(void **)&s_svg.surface_stride = dlsym(lib, "cairo_image_surface_get_stride");
    *(void **)&s_svg.surface_destroy = dlsym(lib, "cairo_surface_destroy");
    s_svg.ok = s_svg.handle_new && s_svg.render && s_svg.unref && s_svg.error_free && s_svg.surface_create &&
               s_svg.create && s_svg.destroy && s_svg.surface_flush && s_svg.surface_data && s_svg.surface_stride &&
               s_svg.surface_destroy;
    if (!s_svg.ok) {
        fprintf(stderr, "native: linux apps: librsvg without rsvg_handle_render_document (older than 2.46): "
                        "SVG icons are left out\n");
    }
}

bool linux_apps_svg_available(void)
{
    pthread_once(&s_svg.once, svg_open);
    return s_svg.ok;
}

static bool svg_to_icon(const char *src, uint8_t *px)
{
    if (!linux_apps_svg_available()) return false;
    void *err = NULL;
    void *h = s_svg.handle_new(src, &err);
    if (h == NULL) {
        if (err != NULL) s_svg.error_free(err);
        return false;
    }
    void *surf = s_svg.surface_create(0 /* CAIRO_FORMAT_ARGB32 */, SVG_PX, SVG_PX);
    void *cr = surf != NULL ? s_svg.create(surf) : NULL;
    const svg_rect_t vp = { 0, 0, SVG_PX, SVG_PX };
    bool ok = cr != NULL && s_svg.render(h, cr, &vp, &err);
    if (err != NULL) s_svg.error_free(err);
    if (cr != NULL) s_svg.destroy(cr);
    s_svg.unref(h);
    uint8_t *rgba = ok ? malloc((size_t)SVG_PX * SVG_PX * 4) : NULL;
    if (rgba != NULL) {
        s_svg.surface_flush(surf);
        const unsigned char *data = s_svg.surface_data(surf);
        int stride = s_svg.surface_stride(surf);
        /* Premultiplied ARGB in native-endian words, made straight RGBA. */
        for (int y = 0; y < SVG_PX; y++) {
            const uint32_t *row = (const uint32_t *)(const void *)(data + (size_t)y * (size_t)stride);
            for (int x = 0; x < SVG_PX; x++) {
                uint32_t v = row[x];
                unsigned a = v >> 24, r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                uint8_t *o = rgba + ((size_t)y * SVG_PX + (size_t)x) * 4;
                o[0] = (uint8_t)(a ? r * 255 / a : 0);
                o[1] = (uint8_t)(a ? g * 255 / a : 0);
                o[2] = (uint8_t)(a ? b * 255 / a : 0);
                o[3] = (uint8_t)a;
            }
        }
        rgba_to_icon(rgba, SVG_PX, SVG_PX, px);
        free(rgba);
    }
    if (surf != NULL) s_svg.surface_destroy(surf);
    return rgba != NULL;
}
#else
bool linux_apps_svg_available(void)
{
    return false;
}
#endif

bool has_suffix(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

/* --------------------------------------------------- the icon themes (L9e)
 *
 * The freedesktop icon theme lookup, for the launcher's size: the user's
 * theme, the themes it inherits (in order, each once), then hicolor; in
 * each, the theme's directories nearest that size first; a PNG, else an
 * SVG. The user's theme: Plasma's (kdeglobals [Icons] Theme), else GTK's
 * (settings.ini gtk-icon-theme-name), else breeze (Plasma's own default)
 * when it is installed. The themes' directories are read once per reading
 * of the list. */

/* The value of key in [section] of an ini-like file, or false. */
static bool ini_value(const char *path, const char *section, const char *key, char *out, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    char line[1024];
    bool in = false, found = false;
    size_t kl = strlen(key);
    while (!found && fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            in = end != NULL && (size_t)(end - line - 1) == strlen(section) &&
                 strncmp(line + 1, section, strlen(section)) == 0;
            continue;
        }
        if (!in || strncmp(line, key, kl) != 0) continue;
        const char *v = line + kl;
        while (*v == ' ' || *v == '\t') v++;
        if (*v != '=') continue;
        v++;
        while (*v == ' ' || *v == '\t') v++;
        snprintf(out, cap, "%s", v);
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = '\0';
        found = out[0] != '\0';
    }
    fclose(f);
    return found;
}

static int icon_bases(char dirs[][512], int nd, char bases[][512], int max)
{
    int n = 0;
    if (getenv("HOME") != NULL && pathf(bases[n], 512, "%s/.icons", getenv("HOME"))) n++;
    for (int d = 0; d < nd && n < max; d++) {
        if (pathf(bases[n], 512, "%s/icons", dirs[d])) n++;
    }
    return n;
}

static bool theme_exists(const char *name, char bases[][512], int nb)
{
    char p[700];
    for (int b = 0; b < nb; b++) {
        if (pathf(p, sizeof(p), "%s/%s/index.theme", bases[b], name) && access(p, R_OK) == 0) return true;
    }
    return false;
}

bool linux_apps_user_icon_theme(char *out, size_t cap, char *why, size_t why_cap)
{
    char cfg[512];
    const char *xch = getenv("XDG_CONFIG_HOME");
    if (xch != NULL && xch[0] == '/') {
        snprintf(cfg, sizeof(cfg), "%s", xch);
    } else if (getenv("HOME") != NULL) {
        snprintf(cfg, sizeof(cfg), "%s/.config", getenv("HOME"));
    } else {
        cfg[0] = '\0';
    }
    char p[700];
    if (cfg[0] != '\0') {
        if (pathf(p, sizeof(p), "%s/kdeglobals", cfg) && ini_value(p, "Icons", "Theme", out, cap)) {
            snprintf(why, why_cap, "Theme in %s", p);
            return true;
        }
        static const char *const gtk[] = { "gtk-4.0", "gtk-3.0" };
        for (size_t i = 0; i < sizeof(gtk) / sizeof(gtk[0]); i++) {
            if (pathf(p, sizeof(p), "%s/%s/settings.ini", cfg, gtk[i]) &&
                ini_value(p, "Settings", "gtk-icon-theme-name", out, cap)) {
                snprintf(why, why_cap, "gtk-icon-theme-name in %s", p);
                return true;
            }
        }
    }
    snprintf(out, cap, "breeze");
    snprintf(why, why_cap, "none set: Plasma's default");
    return false;
}

static int dir_cmp(const void *a, const void *b)
{
    const theme_dir_t *x = a, *y = b;
    return x->dist - y->dist;
}

/* One directory section's distance from the launcher's size (the spec's
 * DirectorySizeDistance at scale 1); -1 for a scaled one. */
static int section_dist(const char *type, int size, int min, int max, int threshold, int scale)
{
    const int want = LINUX_APPS_ICON_PX;
    if (scale > 1) return -1;
    if (strcmp(type, "Fixed") == 0) return abs(size - want);
    if (strcmp(type, "Scalable") == 0) return want < min ? min - want : (want > max ? want - max : 0);
    int lo = size - threshold, hi = size + threshold;
    return want < lo ? lo - want : (want > hi ? want - hi : 0);
}

/* Add a theme's directories (from its index.theme) to th; its Inherits into
 * inherits. False when it has no index.theme. */
static bool theme_load(linux_icon_themes_t *th, const char *name, char bases[][512], int nb, char *inherits,
                       size_t icap)
{
    char idx[700];
    FILE *f = NULL;
    for (int b = 0; b < nb && f == NULL; b++) {
        if (pathf(idx, sizeof(idx), "%s/%s/index.theme", bases[b], name)) f = fopen(idx, "r");
    }
    if (f == NULL) return false;
    inherits[0] = '\0';
    char line[4096];
    char section[256] = "";
    char type[16] = "Threshold";
    int size = 0, min = -1, max = -1, threshold = 2, scale = 1;
    bool in_dir = false;
    /* A section is complete at the next header (and at the end). */
    for (bool more = true; more;) {
        more = fgets(line, sizeof(line), f) != NULL;
        if (more) line[strcspn(line, "\r\n")] = '\0';
        if (!more || line[0] == '[') {
            if (in_dir && size > 0) {
                int dist = section_dist(type, size, min < 0 ? size : min, max < 0 ? size : max, threshold, scale);
                for (int b = 0; b < nb && dist >= 0 && th->ndir < THEME_DIRS_MAX; b++) {
                    char p[900];
                    struct stat st;
                    if (!pathf(p, sizeof(p), "%s/%s/%s", bases[b], name, section) || stat(p, &st) != 0 ||
                        !S_ISDIR(st.st_mode)) {
                        continue;
                    }
                    th->dir[th->ndir].path = strdup(p);
                    th->dir[th->ndir].dist = dist;
                    if (th->dir[th->ndir].path != NULL) th->ndir++;
                }
            }
            if (!more) break;
            char *end = strchr(line, ']');
            if (end == NULL) continue;
            *end = '\0';
            snprintf(section, sizeof(section), "%.255s", line + 1);
            in_dir = strcmp(section, "Icon Theme") != 0;
            snprintf(type, sizeof(type), "Threshold");
            size = 0;
            min = max = -1;
            threshold = 2;
            scale = 1;
            continue;
        }
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        if (!in_dir) {
            if (strcmp(k, "Inherits") == 0) snprintf(inherits, icap, "%s", v);
        } else if (strcmp(k, "Size") == 0) {
            size = atoi(v);
        } else if (strcmp(k, "MinSize") == 0) {
            min = atoi(v);
        } else if (strcmp(k, "MaxSize") == 0) {
            max = atoi(v);
        } else if (strcmp(k, "Threshold") == 0) {
            threshold = atoi(v);
        } else if (strcmp(k, "Scale") == 0) {
            scale = atoi(v);
        } else if (strcmp(k, "Type") == 0) {
            snprintf(type, sizeof(type), "%s", v);
        }
    }
    fclose(f);
    return true;
}

static bool theme_listed(const linux_icon_themes_t *th, const char *name)
{
    for (int i = 0; i < th->n; i++) {
        if (strcmp(th->name[i], name) == 0) return true;
    }
    return false;
}

linux_icon_themes_t *linux_apps_icon_themes(const char *user_theme)
{
    char dirs[16][512];
    int nd = linux_xdg_data_dirs(dirs, 16);
    char bases[ICON_BASES_MAX][512];
    int nb = icon_bases(dirs, nd, bases, ICON_BASES_MAX);
    linux_icon_themes_t *th = calloc(1, sizeof(*th));
    if (th == NULL) return NULL;
    th->dir = calloc(THEME_DIRS_MAX, sizeof(theme_dir_t));
    if (th->dir == NULL) {
        free(th);
        return NULL;
    }
    /* The queue of themes to read: the user's, what each inherits (breadth
     * first), and hicolor last whatever happens. */
    char queue[THEMES_MAX][64];
    int qn = 0;
    if (user_theme != NULL && user_theme[0] != '\0' && strcmp(user_theme, "hicolor") != 0 &&
        theme_exists(user_theme, bases, nb)) {
        snprintf(queue[qn++], 64, "%.63s", user_theme);
    }
    for (int qi = 0; qi < qn && th->n < THEMES_MAX - 1; qi++) {
        if (theme_listed(th, queue[qi])) continue;
        char inherits[512];
        int before = th->ndir;
        if (!theme_load(th, queue[qi], bases, nb, inherits, sizeof(inherits))) continue;
        qsort(th->dir + before, (size_t)(th->ndir - before), sizeof(theme_dir_t), dir_cmp);
        th->start[th->n] = before;
        snprintf(th->name[th->n++], 64, "%.63s", queue[qi]);
        th->start[th->n] = th->ndir;
        char *save = NULL;
        for (char *t = strtok_r(inherits, ",", &save); t != NULL && qn < THEMES_MAX; t = strtok_r(NULL, ",", &save)) {
            while (*t == ' ') t++;
            if (t[0] != '\0' && strcmp(t, "hicolor") != 0) snprintf(queue[qn++], 64, "%.63s", t);
        }
    }
    char inherits[512];
    int before = th->ndir;
    if (!theme_load(th, "hicolor", bases, nb, inherits, sizeof(inherits))) {
        /* No index.theme for hicolor: its usual application directories. */
        static const char *const sizes[] = { "24x24", "32x32", "22x22", "48x48", "64x64", "16x16",
                                             "96x96", "128x128", "256x256", "512x512", "scalable" };
        for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); z++) {
            for (int b = 0; b < nb && th->ndir < THEME_DIRS_MAX; b++) {
                char p[900];
                if (!pathf(p, sizeof(p), "%s/hicolor/%s/apps", bases[b], sizes[z])) continue;
                th->dir[th->ndir].path = strdup(p);
                th->dir[th->ndir].dist = (int)z;
                if (th->dir[th->ndir].path != NULL) th->ndir++;
            }
        }
    }
    qsort(th->dir + before, (size_t)(th->ndir - before), sizeof(theme_dir_t), dir_cmp);
    th->start[th->n] = before;
    snprintf(th->name[th->n++], 64, "hicolor");
    th->start[th->n] = th->ndir;
    return th;
}

void linux_apps_icon_themes_free(linux_icon_themes_t *th)
{
    if (th == NULL) return;
    for (int i = 0; i < th->ndir; i++) free(th->dir[i].path);
    free(th->dir);
    free(th);
}

const char *linux_apps_icon_theme_names(const linux_icon_themes_t *th, char *out, size_t cap)
{
    size_t len = 0;
    out[0] = '\0';
    for (int i = 0; th != NULL && i < th->n && len < cap; i++) {
        int n = snprintf(out + len, cap - len, "%s%s", i ? " > " : "", th->name[i]);
        if (n < 0) break;
        len += (size_t)n;
    }
    return out;
}

/* The image file for an Icon value (a PNG or an SVG), or false. */
bool linux_apps_find_icon(const linux_icon_themes_t *th, const char *icon, char *out, size_t cap)
{
    if (icon == NULL || icon[0] == '\0') return false;
    if (icon[0] == '/') {
        if (!(has_suffix(icon, ".png") || has_suffix(icon, ".svg") || has_suffix(icon, ".svgz")) ||
            access(icon, R_OK) != 0) {
            return false;
        }
        snprintf(out, cap, "%s", icon);
        return true;
    }
    char name[256];
    snprintf(name, sizeof(name), "%s", icon);
    if (has_suffix(name, ".png") || has_suffix(name, ".svg")) name[strlen(name) - 4] = '\0';
    if (strchr(name, '/') != NULL) return false;
    static const char *const exts[] = { "png", "svg", "svgz" };
    for (int t = 0; th != NULL && t < th->n; t++) {
        for (int i = th->start[t]; i < th->start[t + 1]; i++) {
            for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
                if (pathf(out, cap, "%s/%s.%s", th->dir[i].path, name, exts[e]) && access(out, R_OK) == 0) {
                    return true;
                }
            }
        }
    }
    char dirs[16][512];
    int nd = linux_xdg_data_dirs(dirs, 16);
    for (int d = 0; d < nd; d++) {
        for (size_t e = 0; e < 2; e++) {
            if (pathf(out, cap, "%s/pixmaps/%s.%s", dirs[d], name, exts[e]) && access(out, R_OK) == 0) return true;
        }
    }
    return false;
}

/* The BMP for an image (made once per source: the name follows its path,
 * size and time), as a Family mruby path; NULL when it cannot be made.
 * *svg counts the SVGs converted. */
char *icon_for(const char *icon, const linux_icon_themes_t *th, const char *icon_dir, const char *fm_dir,
               int *svg)
{
#if NATIVE_HAVE_PNG
    char src[1024];
    if (!linux_apps_find_icon(th, icon, src, sizeof(src))) return NULL;
    struct stat st;
    if (stat(src, &st) != 0) return NULL;
    char key[1200];
    snprintf(key, sizeof(key), "%s|%lld|%lld", src, (long long)st.st_size, (long long)st.st_mtime);
    uint64_t hsh = 1469598103934665603ULL;
    for (const char *k = key; *k; k++) {
        hsh ^= (uint8_t)*k;
        hsh *= 1099511628211ULL;
    }
    char file[64];
    snprintf(file, sizeof(file), "lx%016llx.bmp", (unsigned long long)hsh);
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", icon_dir, file);
    if (access(path, R_OK) != 0) {
        uint8_t px[LINUX_APPS_ICON_PX * LINUX_APPS_ICON_PX];
        bool is_svg = !has_suffix(src, ".png");
        bool made = is_svg ? svg_to_icon(src, px) : png_to_icon(src, px);
        if (!made || !write_bmp(path, px, LINUX_APPS_ICON_PX, LINUX_APPS_ICON_PX)) return NULL;
        if (is_svg) (*svg)++;
    }
    char fm[400];
    snprintf(fm, sizeof(fm), "%s/%s", fm_dir, file);
    return strdup(fm);
#else
    (void)icon;
    (void)th;
    (void)icon_dir;
    (void)fm_dir;
    (void)svg;
    return NULL;
#endif
}

/* ---------------------------------------------------- Qt's look (L9e) */

bool linux_apps_plasma_qt(const char *root, char *found, size_t cap)
{
    /* Plasma's platform theme for Qt 6 and Qt 5 (plasma-integration),
     * wherever the distribution keeps Qt's plugins. */
    static const char *const pats[] = {
        "%s/usr/lib/*/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
        "%s/usr/lib/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
        "%s/usr/lib*/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so",
        "%s/usr/lib/*/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme5.so",
        "%s/usr/lib/*/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme.so",
        "%s/usr/lib/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme5.so",
    };
    for (size_t i = 0; i < sizeof(pats) / sizeof(pats[0]); i++) {
        char pat[512];
        if (!pathf(pat, sizeof(pat), pats[i], root ? root : "")) continue;
        glob_t g;
        if (glob(pat, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            snprintf(found, cap, "%s", g.gl_pathv[0]);
            globfree(&g);
            return true;
        }
        globfree(&g);
    }
    return false;
}

