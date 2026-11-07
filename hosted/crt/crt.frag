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
#ifdef FMRB_CRT_OUTPUT
uniform mat3 u_frag;     // gl_FragCoord -> display position (0..1, y down)
uniform mat3 u_texel;    // output pixel (logical, its centre) -> texel of u_tex
uniform vec2 u_out;      // the output's logical size, pixels
uniform float u_unit;    // output pixels per picture pixel
#endif
out vec4 o_color;

#ifdef FMRB_CRT_OUTPUT
// The output pixel lp (logical) as wlroots composited it.
vec3 px(ivec2 lp) {
  lp = clamp(lp, ivec2(0), ivec2(u_out) - 1);
  vec2 t = (u_texel * vec3(vec2(lp) + 0.5, 1.0)).xy;
  ivec2 ts = textureSize(u_tex, 0);
  return texelFetch(u_tex, clamp(ivec2(floor(t)), ivec2(0), ts - 1), 0).rgb;
}
// A neighbour o picture pixels away.
#define AT(o) px(ip + (o) * int(u_unit))
#else
vec3 px(ivec2 p) {
  p = clamp(p, ivec2(0), ivec2(u_src) - 1);
  return texelFetch(u_tex, p, 0).rgb;
}
#define AT(o) px(ip + (o))
#endif

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
  ivec2 ip = ivec2(floor(st * u_out));
#else
  ivec2 ip = ivec2(floor(p));
#endif
  vec3 c = AT(ivec2(0, 0));
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
  if (u_vig > 0.0) {
    float v = 16.0 * st.x * st.y * (1.0 - st.x) * (1.0 - st.y);
    c *= pow(clamp(v, 0.0, 1.0), 0.45 * u_vig);
  }
  o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
