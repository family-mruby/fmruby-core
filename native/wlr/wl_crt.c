/*
 * wl_crt.c - the CRT look over the whole output (doc/web_crt/, C4): the
 * browser build's shader (hosted/crt/crt.frag) on everything the compositor
 * shows, Family mruby's picture and the Linux windows alike.
 *
 * Only wlroots' public interfaces (doc/web_crt/report/c4.md has why each):
 *
 *   1. the scene draws into a swapchain of ours, not the output's
 *      (wlr_scene_output_build_state with options.swapchain): only what was
 *      damaged is drawn again, the scene's damage ring keeping each of our
 *      buffers' age;
 *   2. a render pass on the output's own buffer (wlr_renderer_begin_buffer_pass
 *      makes wlroots' GLES context current and binds the buffer's FBO); in
 *      it, our program draws the scene's buffer (a texture of wlroots',
 *      wlr_gles2_texture_get_attribs) through the shader; submit;
 *   3. that buffer committed to the output, whole.
 *
 * Only when the scene needs a frame (or a setting changed), and with the
 * time effects (flicker, noise; C6) also CRT_TIME_FPS times a second: a
 * timer asks for a frame and, the scene having nothing new, the last scene
 * buffer (held for it) goes through the shader again. Direct scan-out
 * is held off while the look is on (it would hand the output a client's
 * buffer). The hardware cursor stays flat on its own plane.
 *
 * Coordinates: the output's buffer may be turned (display.rotate). Its
 * pixels and the turned ("transformed") ones are related by
 * wlr_box_transform, as the scene does, so the shader gets two matrices
 * made from it: buffer pixel -> display position, and turned pixel ->
 * texel of the scene's buffer. In wlroots' FBOs and dmabuf textures, row 0
 * is the buffer's top: both run y down.
 *
 * The pointer: crt_pointer gives wlr_cursor's place sent back through the
 * curve (crt_warp.c, the shader's formula), so a press lands on what is
 * drawn under the arrow; crt_point does the same for a touch (L17).
 *
 * Without GLES (pixman, Vulkan), with an old GL or a shader the driver
 * refuses, one line says why and the output stays as it is.
 */

#include "output_wlr_internal.h"

#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/box.h>
#include <wlr/util/transform.h>

#define EGL_NO_X11
#include <EGL/egl.h>

#include "crt_shaders.h"
#include "crt_warp.h"

/* How often the timings go to the log while the look draws. */
#define CRT_STATS_MS 10000

static struct {
    bool want;                              /* display.look = "crt" */
    int scan, mask, glow, curve, vig;       /* 0..100 */
    int ntsc, flicker, noise;               /* 0..100 (C6) */
    int unit_conf;                          /* display.scale (0 auto) */
    bool unusable;                          /* cannot draw it here; stays sharp */
    char why[200];                          /* ...because */
    bool on;                                /* drawing (direct scan-out held off) */
    unsigned dropped;                       /* presses outside the curved picture */
    struct wlr_swapchain *chain;            /* the scene draws here */
    struct wlr_buffer *last;                /* the scene's last buffer (locked), for the time effects */
    struct wl_event_source *tick;           /* the time effects' timer */
    bool tick_due;                          /* it fired: draw even without scene damage */
    unsigned time_frames;                   /* frames drawn only for the time effects */
    GLuint prog;
    GLint a_pos;
    GLint u_tex, u_src, u_curve, u_scan, u_mask, u_glow, u_vig, u_frag, u_texel, u_out, u_unit;
    GLint u_ntsc, u_flicker, u_noise, u_frame;
    /* Timings. done: wlroots' timer, from the pass's start to the GPU
     * having finished it (the scene's drawing queued before it included);
     * pass: the CRT draw alone on the GPU (GL_EXT_disjoint_timer_query,
     * GLES 3's query functions); cpu: the whole frame on the CPU. */
    struct wlr_render_timer *timer;
    bool timer_pending;
    GLuint query;
    bool query_pending;
    void (*gen_queries)(GLsizei, GLuint *);
    void (*begin_query)(GLenum, GLuint);
    void (*end_query)(GLenum);
    void (*query_uiv)(GLuint, GLenum, GLuint *);
    struct {
        uint64_t since_ms;
        unsigned frames;
        double done_sum_ms, done_max_ms;
        unsigned done_n;
        double pass_sum_ms, pass_max_ms;
        unsigned pass_n;
        double cpu_sum_ms, cpu_max_ms;
        unsigned total_frames;
        double last_done_ms, last_pass_ms, last_cpu_ms;
    } st;
} crt;

/* GL_EXT_disjoint_timer_query's names (GLES 3 has the functions). */
#define CRT_TIME_ELAPSED 0x88BF
#define CRT_QUERY_RESULT 0x8866
#define CRT_QUERY_RESULT_AVAILABLE 0x8867
#define CRT_GPU_DISJOINT 0x8FBB

static double mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* What the settings see (doc/web_crt, C5): whether the look can be drawn
 * here and what is set. */
static void crt_publish(void)
{
    comp_crt_t c = { .crt = crt.want,
                     .s = { crt.scan, crt.mask, crt.glow, crt.curve, crt.vig, crt.ntsc, crt.flicker, crt.noise } };
    comp_link_set_crt(!crt.unusable, &c);
}

static void say_unavailable(void)
{
    fprintf(stderr, "native: compositor: CRT look unavailable: %s; the output stays sharp\n", crt.why);
}

/* The look cannot be drawn here (said when it is wanted). */
static void give_up(const char *fmt, const char *detail)
{
    snprintf(crt.why, sizeof(crt.why), fmt, detail ? detail : "");
    crt.unusable = true;
    if (crt.want) say_unavailable();
    crt_publish();
}

void crt_init(const native_machine_t *m)
{
    memset(&crt, 0, sizeof(crt));
    crt.want = strcmp(m->display.look, "crt") == 0;
    crt.scan = m->display.crt_scanline;
    crt.mask = m->display.crt_mask;
    crt.glow = m->display.crt_glow;
    crt.curve = m->display.crt_curve;
    crt.vig = m->display.crt_vignette;
    crt.ntsc = m->display.crt_ntsc;
    crt.flicker = m->display.crt_flicker;
    crt.noise = m->display.crt_noise;
    crt.unit_conf = m->display.scale;
    if (!wlr_renderer_is_gles2(s.renderer)) give_up("the renderer is %s (it needs GLES)", renderer_name());
    crt_publish();
}

/* Drawing, or about to: the pointer goes through the curve. */
static bool crt_active(void)
{
    return crt.want && !crt.unusable && s.output != NULL;
}

/* A picture pixel in turned output pixels: Family mruby's scale on the
 * output (display.scale). */
static int crt_unit(void)
{
    int u = s.route.place.scale > 0 ? s.route.place.scale : (crt.unit_conf > 0 ? crt.unit_conf : 2);
    float os = s.output != NULL && s.output->scale > 0 ? s.output->scale : 1.0f;
    int v = (int)lroundf((float)u * os);
    return v > 0 ? v : 1;
}

void crt_pointer(double *x, double *y, bool *inside)
{
    *x = s.cursor->x;
    *y = s.cursor->y;
    crt_point(x, y, inside);
}

void crt_point(double *x, double *y, bool *inside)
{
    if (inside != NULL) *inside = true;
    if (!crt_active() || crt_curve_coef(crt.curve) == 0) return;
    struct wlr_box b;
    wlr_output_layout_get_box(s.layout, s.output, &b);
    if (b.width <= 0 || b.height <= 0) return;
    double sx = 0, sy = 0;
    crt_warp_to_source((*x - b.x) / b.width, (*y - b.y) / b.height, crt.curve, &sx, &sy);
    if (inside != NULL) *inside = crt_inside(sx, sy);
    /* Outside the picture: held at its edge (a move there still moves). */
    double lim = 1.0 - 1e-9;
    sx = sx < 0 ? 0 : (sx > lim ? lim : sx);
    sy = sy < 0 ? 0 : (sy > lim ? lim : sy);
    *x = b.x + sx * b.width;
    *y = b.y + sy * b.height;
}

/* Ask for a frame that draws everything again. */
static void crt_redraw_all(void)
{
    if (s.output == NULL) return;
    if (s.scene_output != NULL) wlr_damage_ring_add_whole(&s.scene_output->damage_ring);
    wlr_output_update_needs_frame(s.output);
}

/* The time effects are on: the picture changes with time. */
static bool crt_timed(void)
{
    return crt.flicker > 0 || crt.noise > 0;
}

static void crt_drop_last(void)
{
    if (crt.last != NULL) {
        wlr_buffer_unlock(crt.last);
        crt.last = NULL;
    }
}

static int crt_tick(void *data)
{
    (void)data;
    crt.tick_due = true;
    if (s.output != NULL) wlr_output_schedule_frame(s.output);
    return 0;
}

/* After a frame: with the time effects on, the next one in a 30th of a
 * second (the timer is one-shot, so it never piles up behind slow frames);
 * off, the timer stays idle and the last buffer is let go. */
static void crt_arm_tick(void)
{
    if (!crt_timed() || !crt.want || crt.unusable) {
        if (crt.tick != NULL) wl_event_source_timer_update(crt.tick, 0);
        crt.tick_due = false;
        crt_drop_last();
        return;
    }
    if (crt.tick == NULL) crt.tick = wl_event_loop_add_timer(s.loop, crt_tick, NULL);
    if (crt.tick != NULL) wl_event_source_timer_update(crt.tick, 1000 / CRT_TIME_FPS);
}

/* Back to the plain output: the output's own buffers hold the CRT picture,
 * which the scene's damage ring knows nothing of. */
static void crt_stop(void)
{
    crt_drop_last();
    if (crt.tick != NULL) wl_event_source_timer_update(crt.tick, 0);
    crt.tick_due = false;
    if (crt.on) {
        if (s.output != NULL) wlr_output_lock_attach_render(s.output, false);
        crt.on = false;
        crt_redraw_all();
    }
    if (crt.chain != NULL) {
        wlr_swapchain_destroy(crt.chain);
        crt.chain = NULL;
    }
}

bool crt_set(const char *look, const int strengths[COMP_CRT_N], char *why, size_t why_len)
{
    if (look != NULL) {
        if (strcmp(look, "crt") != 0 && strcmp(look, "sharp") != 0) {
            snprintf(why, why_len, "look \"%s\" is not sharp or crt", look);
            return false;
        }
        crt.want = strcmp(look, "crt") == 0;
    }
    if (strengths != NULL) {
        for (int i = 0; i < COMP_CRT_N; i++) {
            if (strengths[i] < -1 || strengths[i] > 100) {
                snprintf(why, why_len, "a strength is not 0-100");
                return false;
            }
        }
        int *f[COMP_CRT_N] = { &crt.scan, &crt.mask, &crt.glow, &crt.curve, &crt.vig,
                               &crt.ntsc, &crt.flicker, &crt.noise };
        for (int i = 0; i < COMP_CRT_N; i++) {
            if (strengths[i] >= 0) *f[i] = strengths[i];
        }
    }
    if (crt.want && crt.unusable) say_unavailable();
    crt_publish();
    if (!crt.want) crt_stop();
    else crt_redraw_all();
    /* What the pointer is over may have moved with the curve. */
    recheck_pointer();
    fprintf(stderr,
            "native: compositor: look %s (scanline %d, mask %d, glow %d, curve %d, vignette %d, ntsc %d, flicker %d, "
            "noise %d)%s\n",
            crt.want ? "crt" : "sharp", crt.scan, crt.mask, crt.glow, crt.curve, crt.vig, crt.ntsc, crt.flicker,
            crt.noise,
            crt.want && crt.unusable ? ", unavailable here" : "");
    return true;
}

void crt_dropped_press(void)
{
    crt.dropped++;
}

void crt_describe(char *out, size_t cap)
{
    double span = crt.st.since_ms ? (double)(now_ms() - crt.st.since_ms) / 1000.0 : 0.0;
    snprintf(out, cap,
             "OK look=%s drawing=%d unusable=%d scanline=%d mask=%d glow=%d curve=%d vignette=%d ntsc=%d "
             "flicker=%d noise=%d unit=%d frames=%u time_frames=%u span_s=%.1f last_pass_ms=%.3f "
             "last_done_ms=%.3f last_cpu_ms=%.3f dropped=%u\n",
             crt.want ? "crt" : "sharp", crt.on ? 1 : 0, crt.unusable ? 1 : 0, crt.scan, crt.mask, crt.glow,
             crt.curve, crt.vig, crt.ntsc, crt.flicker, crt.noise, crt_unit(), crt.st.total_frames,
             crt.time_frames, span, crt.st.last_pass_ms, crt.st.last_done_ms, crt.st.last_cpu_ms, crt.dropped);
}

static GLuint compile(GLenum type, const char *const *parts, int n)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, n, parts, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        for (char *p = log; *p; p++) {
            if (*p == '\n') *p = ' ';
        }
        give_up("the shader: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* The program, once, with wlroots' context current (inside a pass). */
static bool crt_program(void)
{
    if (crt.prog != 0) return true;
    const char *ver = (const char *)glGetString(GL_VERSION);
    int major = 0;
    if (ver == NULL || sscanf(ver, "OpenGL ES %d", &major) != 1 || major < 3) {
        give_up("%s (GLSL ES 3.00 needs OpenGL ES 3)", ver ? ver : "no GL_VERSION");
        return false;
    }
    static const char *const vs[] = { "#version 300 es\n", crt_vert_src };
    static const char *const fs[] = { "#version 300 es\n#define FMRB_CRT_OUTPUT 1\n", crt_frag_src };
    GLuint v = compile(GL_VERTEX_SHADER, vs, 2);
    if (v == 0) return false;
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, 2);
    if (f == 0) {
        glDeleteShader(v);
        return false;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        glDeleteProgram(p);
        give_up("the shader did not link: %s", log);
        return false;
    }
    crt.prog = p;
    crt.a_pos = glGetAttribLocation(p, "a_pos");
    crt.u_tex = glGetUniformLocation(p, "u_tex");
    crt.u_src = glGetUniformLocation(p, "u_src");
    crt.u_curve = glGetUniformLocation(p, "u_curve");
    crt.u_scan = glGetUniformLocation(p, "u_scan");
    crt.u_mask = glGetUniformLocation(p, "u_mask");
    crt.u_glow = glGetUniformLocation(p, "u_glow");
    crt.u_vig = glGetUniformLocation(p, "u_vig");
    crt.u_frag = glGetUniformLocation(p, "u_frag");
    crt.u_texel = glGetUniformLocation(p, "u_texel");
    crt.u_out = glGetUniformLocation(p, "u_out");
    crt.u_unit = glGetUniformLocation(p, "u_unit");
    crt.u_ntsc = glGetUniformLocation(p, "u_ntsc");
    crt.u_flicker = glGetUniformLocation(p, "u_flicker");
    crt.u_noise = glGetUniformLocation(p, "u_noise");
    crt.u_frame = glGetUniformLocation(p, "u_frame");
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    if (ext != NULL && strstr(ext, "GL_EXT_disjoint_timer_query") != NULL) {
        crt.gen_queries = (void (*)(GLsizei, GLuint *))eglGetProcAddress("glGenQueries");
        crt.begin_query = (void (*)(GLenum, GLuint))eglGetProcAddress("glBeginQuery");
        crt.end_query = (void (*)(GLenum))eglGetProcAddress("glEndQuery");
        crt.query_uiv = (void (*)(GLuint, GLenum, GLuint *))eglGetProcAddress("glGetQueryObjectuiv");
        if (crt.gen_queries && crt.begin_query && crt.end_query && crt.query_uiv) crt.gen_queries(1, &crt.query);
    }
    fprintf(stderr, "native: compositor: CRT look on %s (%s)%s\n", ver, (const char *)glGetString(GL_RENDERER),
            crt.query != 0 ? "" : ", its pass not timed (no GL_EXT_disjoint_timer_query)");
    return true;
}

/* The turned pixel -> buffer pixel map, as wlroots' scene lays the output
 * out (transform_output_box: the inverse transform over the turned size):
 * buffer = A * turned + b, from three one-pixel boxes. */
static void turned_to_buffer(int tw, int th, double A[2][2], double b[2])
{
    enum wl_output_transform t = wlr_output_transform_invert(s.output->transform);
    double c[3][2];
    static const int at[3][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 } };
    for (int i = 0; i < 3; i++) {
        struct wlr_box in = { .x = at[i][0], .y = at[i][1], .width = 1, .height = 1 }, out;
        wlr_box_transform(&out, &in, t, tw, th);
        c[i][0] = out.x + 0.5;
        c[i][1] = out.y + 0.5;
    }
    A[0][0] = c[1][0] - c[0][0];
    A[1][0] = c[1][1] - c[0][1];
    A[0][1] = c[2][0] - c[0][0];
    A[1][1] = c[2][1] - c[0][1];
    b[0] = c[0][0] - (A[0][0] * 0.5 + A[0][1] * 0.5);
    b[1] = c[0][1] - (A[1][0] * 0.5 + A[1][1] * 0.5);
}

static void crt_add(double ms, double *sum, double *max, unsigned *n, double *last)
{
    *sum += ms;
    if (ms > *max) *max = ms;
    (*n)++;
    *last = ms;
}

/* The last frame's CRT pass on the GPU (long done by now), with the
 * context current. */
static void crt_read_timings(void)
{
    if (crt.query_pending) {
        GLuint avail = 0, ns = 0;
        crt.query_uiv(crt.query, CRT_QUERY_RESULT_AVAILABLE, &avail);
        if (avail) {
            GLint disjoint = 0;
            glGetIntegerv(CRT_GPU_DISJOINT, &disjoint);
            crt.query_uiv(crt.query, CRT_QUERY_RESULT, &ns);
            if (!disjoint) {
                crt_add(ns / 1e6, &crt.st.pass_sum_ms, &crt.st.pass_max_ms, &crt.st.pass_n, &crt.st.last_pass_ms);
            }
            crt.query_pending = false;
        }
    }
}

static bool crt_draw(struct wlr_buffer *src, struct wlr_buffer *dst)
{
    struct wlr_texture *tex = wlr_texture_from_buffer(s.renderer, src);
    if (tex == NULL) {
        give_up("the scene's buffer cannot be a texture%s", NULL);
        return false;
    }
    struct wlr_gles2_texture_attribs ta;
    wlr_gles2_texture_get_attribs(tex, &ta);
    if (ta.target != GL_TEXTURE_2D) {
        wlr_texture_destroy(tex);
        give_up("the scene's buffer is an external-only texture%s", NULL);
        return false;
    }
    /* wlroots' timer is read before the pass that starts it again. */
    if (crt.timer_pending && crt.timer != NULL) {
        int ns = wlr_render_timer_get_duration_ns(crt.timer);
        if (ns >= 0) crt_add(ns / 1e6, &crt.st.done_sum_ms, &crt.st.done_max_ms, &crt.st.done_n, &crt.st.last_done_ms);
        crt.timer_pending = false;
    }
    if (crt.timer == NULL) crt.timer = wlr_render_timer_create(s.renderer);
    struct wlr_render_pass *pass =
        wlr_renderer_begin_buffer_pass(s.renderer, dst, &(struct wlr_buffer_pass_options){ .timer = crt.timer });
    if (pass == NULL) {
        wlr_texture_destroy(tex);
        give_up("no render pass on the output's buffer%s", NULL);
        return false;
    }
    if (!crt_program()) {
        wlr_render_pass_submit(pass);
        wlr_texture_destroy(tex);
        return false;
    }
    crt_read_timings();

    int tw = 0, th = 0;
    wlr_output_transformed_resolution(s.output, &tw, &th);
    double A[2][2], b[2];
    turned_to_buffer(tw, th, A, b);
    double det = A[0][0] * A[1][1] - A[0][1] * A[1][0];
    double I[2][2] = { { A[1][1] / det, -A[0][1] / det }, { -A[1][0] / det, A[0][0] / det } };
    /* gl_FragCoord (buffer pixel) -> turned pixel -> 0..1. Column-major. */
    float frag[9] = {
        (float)(I[0][0] / tw), (float)(I[1][0] / th), 0,
        (float)(I[0][1] / tw), (float)(I[1][1] / th), 0,
        (float)(-(I[0][0] * b[0] + I[0][1] * b[1]) / tw), (float)(-(I[1][0] * b[0] + I[1][1] * b[1]) / th), 1,
    };
    float texel[9] = {
        (float)A[0][0], (float)A[1][0], 0,
        (float)A[0][1], (float)A[1][1], 0,
        (float)b[0], (float)b[1], 1,
    };
    int unit = crt_unit();
    /* Display pixels per picture pixel, for the fine effects' weight (the
     * browser's formula). */
    double scale = unit;

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(crt.prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ta.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glUniform1i(crt.u_tex, 0);
    glUniform2f(crt.u_src, (float)tw / (float)unit, (float)th / (float)unit);
    glUniform2f(crt.u_out, (float)tw, (float)th);
    glUniform1f(crt.u_unit, (float)unit);
    glUniformMatrix3fv(crt.u_frag, 1, GL_FALSE, frag);
    glUniformMatrix3fv(crt.u_texel, 1, GL_FALSE, texel);
    glUniform1f(crt.u_curve, (float)crt_curve_coef(crt.curve));
    glUniform1f(crt.u_scan, (float)(crt.scan / 100.0 * crt_fine_weight(scale, 1.5, 3.0)));
    glUniform1f(crt.u_mask, (float)(crt.mask / 100.0 * crt_fine_weight(scale, 2.0, 3.0)));
    glUniform1f(crt.u_glow, (float)(crt.glow / 100.0));
    glUniform1f(crt.u_vig, (float)(crt.vig / 100.0));
    glUniform1f(crt.u_ntsc, (float)(crt.ntsc / 100.0));
    glUniform1f(crt.u_flicker, (float)(crt.flicker / 100.0));
    glUniform1f(crt.u_noise, (float)(crt.noise / 100.0));
    /* The time effects' frame: from the clock, CRT_TIME_FPS a second
     * whatever the drawing rate (crt.js's timeFrame). */
    glUniform1f(crt.u_frame, (float)((now_ms() * CRT_TIME_FPS / 1000) % 1048576));
    static const GLfloat tri[] = { -1, -1, 3, -1, -1, 3 };
    glEnableVertexAttribArray((GLuint)crt.a_pos);
    glVertexAttribPointer((GLuint)crt.a_pos, 2, GL_FLOAT, GL_FALSE, 0, tri);
    bool timed = crt.query != 0 && !crt.query_pending;
    if (timed) crt.begin_query(CRT_TIME_ELAPSED, crt.query);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (timed) {
        crt.end_query(CRT_TIME_ELAPSED);
        crt.query_pending = true;
    }
    glDisableVertexAttribArray((GLuint)crt.a_pos);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    bool ok = wlr_render_pass_submit(pass);
    wlr_texture_destroy(tex);
    if (ok) crt.timer_pending = true;
    return ok;
}

/* The scene's swapchain: the output's buffer size, the output's format
 * with the modifiers wlroots can also read back as a texture. */
static bool crt_chain(void)
{
    struct wlr_output *o = s.output;
    if (crt.chain != NULL && crt.chain->width == o->width && crt.chain->height == o->height &&
        o->swapchain != NULL && crt.chain->format.format == o->swapchain->format.format) {
        return true;
    }
    crt_drop_last();
    if (crt.chain != NULL) {
        wlr_swapchain_destroy(crt.chain);
        crt.chain = NULL;
    }
    if (o->swapchain == NULL) return false;
    const struct wlr_drm_format *of = &o->swapchain->format;
    const struct wlr_drm_format *tf =
        wlr_drm_format_set_get(wlr_renderer_get_texture_formats(s.renderer, WLR_BUFFER_CAP_DMABUF), of->format);
    static uint64_t mods[128];
    struct wlr_drm_format f = { .format = of->format, .len = 0, .capacity = 128, .modifiers = mods };
    for (size_t i = 0; tf != NULL && i < of->len; i++) {
        for (size_t j = 0; j < tf->len; j++) {
            if (of->modifiers[i] == tf->modifiers[j] && f.len < f.capacity) {
                mods[f.len++] = of->modifiers[i];
                break;
            }
        }
    }
    const struct wlr_drm_format *use = f.len > 0 ? &f : of;
    crt.chain = wlr_swapchain_create(s.allocator, o->width, o->height, use);
    if (crt.chain == NULL) {
        give_up("no buffers for the scene%s", NULL);
        return false;
    }
    fprintf(stderr, "native: compositor: CRT look: the scene draws into %dx%d buffers (%zu of %zu modifiers)\n",
            o->width, o->height, f.len, of->len);
    return true;
}

static void crt_stats(double cpu_ms)
{
    uint64_t t = now_ms();
    if (crt.st.since_ms == 0) crt.st.since_ms = t;
    crt.st.frames++;
    crt.st.total_frames++;
    unsigned n = 0;
    crt_add(cpu_ms, &crt.st.cpu_sum_ms, &crt.st.cpu_max_ms, &n, &crt.st.last_cpu_ms);
    if (t - crt.st.since_ms < CRT_STATS_MS) return;
    fprintf(stderr,
            "native: compositor: CRT look: %u frames in %.1f s; the CRT pass on the GPU mean %.2f ms max %.2f ms "
            "(%u timed); to the GPU's end of it mean %.2f ms max %.2f ms; the frame on the CPU mean %.2f ms max "
            "%.2f ms\n",
            crt.st.frames, (double)(t - crt.st.since_ms) / 1000.0,
            crt.st.pass_n ? crt.st.pass_sum_ms / crt.st.pass_n : 0.0, crt.st.pass_max_ms, crt.st.pass_n,
            crt.st.done_n ? crt.st.done_sum_ms / crt.st.done_n : 0.0, crt.st.done_max_ms,
            crt.st.cpu_sum_ms / crt.st.frames, crt.st.cpu_max_ms);
    unsigned total = crt.st.total_frames;
    double lp = crt.st.last_pass_ms, ld = crt.st.last_done_ms, lc = crt.st.last_cpu_ms;
    memset(&crt.st, 0, sizeof(crt.st));
    crt.st.since_ms = t;
    crt.st.total_frames = total;
    crt.st.last_pass_ms = lp;
    crt.st.last_done_ms = ld;
    crt.st.last_cpu_ms = lc;
}

bool crt_frame(void)
{
    if (!crt.want || crt.unusable || s.output == NULL || s.scene_output == NULL) {
        if (crt.on || crt.chain != NULL) crt_stop();
        return false;
    }
    struct wlr_output *o = s.output;
    bool tick = crt.tick_due;
    crt.tick_due = false;
    bool scene_new = wlr_scene_output_needs_frame(s.scene_output);
    /* Nothing new in the scene: only the time effects' tick draws, from
     * the buffer kept from the last frame. */
    if (!scene_new && !(tick && crt_timed() && crt.last != NULL)) {
        if (tick) crt_arm_tick();
        return true;
    }
    double t0 = mono_ms();

    if (!crt.on) {
        wlr_output_lock_attach_render(o, true);
        crt.on = true;
    }
    struct wlr_output_state out;
    wlr_output_state_init(&out);
    bool ok = false;
    if (!wlr_output_configure_primary_swapchain(o, &out, &o->swapchain) || !crt_chain()) goto done;

    struct wlr_buffer *src = NULL;
    if (scene_new) {
        struct wlr_output_state scene;
        wlr_output_state_init(&scene);
        const struct wlr_scene_output_state_options opts = { .swapchain = crt.chain };
        if (wlr_scene_output_build_state(s.scene_output, &scene, &opts) &&
            (scene.committed & WLR_OUTPUT_STATE_BUFFER) && scene.buffer != NULL) {
            src = wlr_buffer_lock(scene.buffer);
        }
        /* Only its buffer: the wait point it names is the scene's pass,
         * which ours follows in the same context anyway. */
        wlr_output_state_finish(&scene);
    } else {
        src = wlr_buffer_lock(crt.last);
        crt.time_frames++;
    }
    if (src == NULL) goto done;
    /* Kept for the time effects' next tick (only while they are on). */
    if (crt_timed() && src != crt.last) {
        crt_drop_last();
        crt.last = wlr_buffer_lock(src);
    }

    struct wlr_buffer *dst = wlr_swapchain_acquire(o->swapchain);
    if (dst == NULL) {
        wlr_buffer_unlock(src);
        goto done;
    }
    bool drawn = crt_draw(src, dst);
    wlr_buffer_unlock(src);
    if (!drawn) {
        wlr_buffer_unlock(dst);
        goto done;
    }
    wlr_output_state_set_buffer(&out, dst);
    wlr_buffer_unlock(dst);
    ok = wlr_output_commit_state(o, &out);
    if (ok) s.stats.commits++;

done:
    wlr_output_state_finish(&out);
    if (crt.unusable) {
        /* Gave up in this frame: the plain output from the next one. */
        crt_stop();
        return false;
    }
    if (ok) crt_stats(mono_ms() - t0);
    crt_arm_tick();
    return true;
}

void crt_output_gone(void)
{
    crt_drop_last();
    if (crt.tick != NULL) {
        wl_event_source_remove(crt.tick);
        crt.tick = NULL;
    }
    crt.tick_due = false;
    if (crt.chain != NULL) {
        wlr_swapchain_destroy(crt.chain);
        crt.chain = NULL;
    }
    crt.on = false;
}

