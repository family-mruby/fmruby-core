// crt.frag - the CRT look's fragment shader (doc/web_crt/), shared by the
// browser build (wasm/web/crt.js) and the native compositor
// (native/wlr/wl_crt.c). GLSL ES 3.00 without its #version line.
//
// The curve is the formula of crt_warp.c and crt.js's warpToSource (display
// -> picture); change all three together (rake webcrt:test compares them).
//
// The browser draws the picture itself (u_src pixels) into the default
// framebuffer. The native compositor defines FMRB_CRT_OUTPUT: it draws the
// whole output, already composited by wlroots into a buffer at the output's
// resolution, into the output's buffer; a picture pixel is display.scale
// output pixels (u_unit) and both buffers may be turned (u_frag, u_texel).
//
// Uniforms that are 0 skip their effect (a uniform branch: every fragment
// takes the same way, so it costs nothing to ask).
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_src;      // picture size, pixels
uniform vec2 u_dst;      // display size, device pixels
uniform float u_curve;   // curve coefficient (warpToSource's c)
uniform float u_scan;    // scanline strength, already weighted by magnification
uniform float u_mask;    // grille strength, likewise
uniform float u_glow;    // glow strength
uniform float u_vig;     // dark corners
uniform float u_ntsc;    // NTSC colour bleed (the colour's resolution across)
uniform float u_flicker; // brightness that wavers from frame to frame
uniform float u_noise;   // grain, new every frame
uniform float u_frame;   // the time effects' frame number (30 a second)
#ifdef FMRB_CRT_OUTPUT
uniform mat3 u_frag;     // gl_FragCoord -> display position (0..1, y down)
uniform mat3 u_texel;    // output pixel (logical, its centre) -> texel of u_tex
uniform vec2 u_out;      // the output's logical size, pixels
uniform float u_unit;    // output pixels per picture pixel
#endif
out vec4 o_color;

#ifdef FMRB_CRT_OUTPUT
// The output pixel under the fragment as wlroots composited it, in texels
// of u_tex, and a picture pixel's step right and down (set in main).
vec2 t_base, t_dx, t_dy;
ivec2 t_max;
// A neighbour o picture pixels away (clamped at the output's edge).
vec3 px(ivec2 o) {
  ivec2 t = ivec2(floor(t_base + float(o.x) * t_dx + float(o.y) * t_dy));
  return texelFetch(u_tex, clamp(t, ivec2(0), t_max), 0).rgb;
}
#define AT(o) px(o)
#else
vec3 px(ivec2 p) {
  p = clamp(p, ivec2(0), ivec2(u_src) - 1);
  return texelFetch(u_tex, p, 0).rgb;
}
#define AT(o) px(ip + (o))
#endif

// RGB <-> YIQ (NTSC's luma and two colour-difference signals).
const mat3 TO_YIQ = mat3(0.299, 0.596, 0.211,
                         0.587, -0.274, -0.523,
                         0.114, -0.322, 0.312);
const mat3 TO_RGB = mat3(1.0, 1.0, 1.0,
                         0.956, -0.272, -1.106,
                         0.621, -0.647, 1.703);

// A hash of a picture pixel and a frame, 0..1 (pcg3d).
float hash(uvec3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
  return float(v.x >> 8u) / 16777215.0;
}

void main() {
#ifdef FMRB_CRT_OUTPUT
  vec2 uv = (u_frag * vec3(gl_FragCoord.xy, 1.0)).xy;
#else
  vec2 uv = vec2(gl_FragCoord.x / u_dst.x, 1.0 - gl_FragCoord.y / u_dst.y);
#endif
  vec2 st = uv;
  if (u_curve > 0.0) {
    vec2 d = uv * 2.0 - 1.0;
    d *= 1.0 + u_curve * dot(d, d);
    st = d * 0.5 + 0.5;
  }
  if (st.x < 0.0 || st.y < 0.0 || st.x >= 1.0 || st.y >= 1.0) {
    o_color = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  vec2 p = st * u_src;
#ifdef FMRB_CRT_OUTPUT
  // The output pixel under this one; the neighbours are a picture pixel away.
  vec2 lp = floor(st * u_out) + 0.5;
  t_base = (u_texel * vec3(lp, 1.0)).xy;
  t_dx = mat2(u_texel)[0] * u_unit;
  t_dy = mat2(u_texel)[1] * u_unit;
  t_max = textureSize(u_tex, 0) - 1;
#else
  ivec2 ip = ivec2(floor(p));
#endif
  vec3 c = AT(ivec2(0, 0));
  if (u_ntsc > 0.0) {
    // A composite signal carries the colour in about a third of the
    // brightness's band: keep this pixel's luma, take the colour (I and Q)
    // from a gaussian along the row (sigma 3 picture pixels, out to 7 each
    // way; the far taps stand for their neighbours too), so colour spills
    // 2-4 pixels past its edges. Where the brightness steps, the colour
    // decoder also sees the step as colour: a fringe whose hue turns with
    // the subcarrier's phase (about 0.6 of a cycle a pixel, half a cycle a
    // line).
    // The decoder's filter also lags, so the colour trails to the right:
    // a pixel takes more from its left and less from its right.
    //
    // Strength: e = 2 * u_ntsc. The blur is all in by e = 1 (the strength
    // 50); above it the lag and the fringe keep growing (the strength 100
    // all from the left, the fringe twice as strong).
    vec3 l1 = AT(ivec2(-1, 0)), r1 = AT(ivec2(1, 0));
    vec3 lw = l1 * 0.1259 + AT(ivec2(-2, 0)) * 0.1067 + AT(ivec2(-3, 0)) * 0.0810
            + AT(ivec2(-5, 0)) * 0.0994 + AT(ivec2(-7, 0)) * 0.0205;
    vec3 rw = r1 * 0.1259 + AT(ivec2(2, 0)) * 0.1067 + AT(ivec2(3, 0)) * 0.0810
            + AT(ivec2(5, 0)) * 0.0994 + AT(ivec2(7, 0)) * 0.0205;
    float e = 2.0 * u_ntsc;
    float lag = min(0.6 * e, 1.0);
    vec2 iq = (TO_YIQ * (c * 0.1330 + lw * (1.0 + lag) + rw * (1.0 - lag))).yz;
    vec3 yiq = TO_YIQ * c;
    vec2 pp = floor(p);
    float phase = 6.2832 * (0.6 * pp.x + 0.5 * pp.y);
    float ystep = dot(r1 - l1, vec3(0.299, 0.587, 0.114));
    float blend = sqrt(min(e, 1.0));
    iq += 0.8 * e / blend * ystep * vec2(cos(phase), sin(phase));
    c = clamp(TO_RGB * vec3(yiq.x, mix(yiq.yz, iq, blend)), 0.0, 1.0);
  }
  if (u_noise > 0.0) {
    // Grain in the signal: each picture pixel brighter or darker (+-14% at
    // 100), new every frame. Before the scanlines and the grille, so a
    // grain is shaped like the picture's pixel it falls on.
    float n = hash(uvec3(uvec2(ivec2(floor(p)) + 4096), uint(u_frame))) - 0.5;
    c = clamp(c + vec3(n * u_noise * 0.28), 0.0, 1.0);
  }
  vec3 halo = vec3(0.0);
  if (u_glow > 0.0) {
    // A beam spreads sideways: mix in the neighbours on the row ...
    vec3 l = AT(ivec2(-1, 0));
    vec3 r = AT(ivec2(1, 0));
    c = mix(c, 0.5 * c + 0.25 * (l + r), 0.6 * u_glow);
    // ... and bright areas light up their surroundings, filling the gaps
    // the scanlines and the grille leave (added after them).
    halo = 0.16 * (l + r)
         + 0.12 * (AT(ivec2(0, -1)) + AT(ivec2(0, 1)))
         + 0.06 * (AT(ivec2(-2, 0)) + AT(ivec2(2, 0))
                 + AT(ivec2(-1, -1)) + AT(ivec2(1, -1))
                 + AT(ivec2(-1, 1)) + AT(ivec2(1, 1)));
    halo = halo * halo;
  }
  if (u_scan > 0.0) {
    // One beam per picture row, a gaussian across it; a bright pixel
    // widens its beam, so light text stays solid while dark rows open up.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    float sigma = mix(0.20, 0.38, lum);
    float fy = fract(p.y) - 0.5;
    float beam = exp(-fy * fy / (2.0 * sigma * sigma));
    float gain = 1.0 / min(1.0, sigma * 2.5066);
    c = mix(c, c * beam * mix(1.0, gain, 0.7), u_scan);
  }
  if (u_mask > 0.0) {
    // Aperture grille: R, G and B stripes, three to a picture pixel.
    int stripe = int(fract(p.x) * 3.0);
    vec3 m = vec3(0.45);
    m[stripe] = 1.0;
    c *= mix(vec3(1.0), m, u_mask) / mix(1.0, 0.7, u_mask);
  }
  c += halo * u_glow * 0.9;
  if (u_flicker > 0.0) {
    // The whole picture brighter or darker each frame (+-12% at 100), and
    // a dark bar rolling down the screen once in 4 seconds (mains hum):
    // about a sixth of the height, 60% darker at its middle at 100.
    uint f = uint(u_frame);
    float w = hash(uvec3(f, 7u, 11u)) * 2.0 - 1.0;
    float dy = fract(st.y - u_frame / 120.0 + 0.5) - 0.5;
    float bar = exp(-dy * dy / (2.0 * 0.06 * 0.06));
    c *= 1.0 + u_flicker * (0.12 * w - 0.6 * bar);
  }
  if (u_vig > 0.0) {
    float v = 16.0 * st.x * st.y * (1.0 - st.x) * (1.0 - st.y);
    c *= pow(clamp(v, 0.0, 1.0), 0.45 * u_vig);
  }
  o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
