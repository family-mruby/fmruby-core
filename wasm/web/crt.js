// crt.js - the browser build's CRT look (doc/web_crt/).
//
// The machine's picture stays where it is: main.js keeps painting the 2D
// canvas (#screen), and that canvas is what screenshots and the regression
// read. This file draws a second canvas over it with WebGL2 -- scanlines, a
// phosphor grille, glow, the tube's curve and its dark corners -- and only
// when the picture, the size or a setting has changed.
//
// The curve is one formula, written twice: warpToSource below (JavaScript,
// which main.js uses to send a click to the pixel under the pointer) and the
// same lines in the fragment shader. Both go from the display to the
// picture, which is the direction a fragment shader works in, so a click
// lands on exactly the pixel drawn under it. warpToDisplay is the other
// way, solved by a few Newton steps (the tests and the driver use it).
//
// Loaded as a plain script in the page (window.FmrbCrt) and as a CommonJS
// module under node (test/web_crt/).
(function (root) {
  'use strict';

  // Slider value (0..100) -> the curve's coefficient. 100 is a strongly
  // bent tube; the default is a gentle one.
  const CURVE_MAX = 0.25;

  // The strengths a fresh page gets, 0..100 each. Provisional (C2 settles
  // them with the user).
  const DEFAULTS = Object.freeze({ scan: 55, mask: 30, glow: 30, curve: 15, vignette: 40 });
  const KEYS = Object.freeze(['scan', 'mask', 'glow', 'curve', 'vignette']);

  function curveCoef(curve) {
    return Math.max(0, Math.min(100, curve || 0)) / 100 * CURVE_MAX;
  }

  // Display position (0..1 across the shown canvas, y down) -> picture
  // position (0..1, y down). Radial: d' = d * (1 + c * |d|^2) with d in
  // -1..1, so the whole picture stays on the display and only the space
  // around its bowed edges is outside (> 1 or < 0: nothing is drawn there).
  function warpToSource(u, v, curve) {
    const c = curveCoef(curve);
    if (c === 0) return [u, v];
    const dx = u * 2 - 1, dy = v * 2 - 1;
    const k = 1 + c * (dx * dx + dy * dy);
    return [(dx * k + 1) / 2, (dy * k + 1) / 2];
  }

  // Picture position -> display position: the inverse of warpToSource.
  // Along a ray from the centre, |s| = r + c r^3 is solved for r (monotonic,
  // so Newton from r = |s| settles in a handful of steps).
  function warpToDisplay(x, y, curve) {
    const c = curveCoef(curve);
    if (c === 0) return [x, y];
    const sx = x * 2 - 1, sy = y * 2 - 1;
    const s = Math.hypot(sx, sy);
    if (s === 0) return [0.5, 0.5];
    let r = s;
    for (let i = 0; i < 12; i++) {
      const f = r + c * r * r * r - s;
      r -= f / (1 + 3 * c * r * r);
    }
    const t = r / s;
    return [(sx * t + 1) / 2, (sy * t + 1) / 2];
  }

  // Inside the picture? (0 <= p < 1 on both axes)
  function inside(p) {
    return p[0] >= 0 && p[0] < 1 && p[1] >= 0 && p[1] < 1;
  }

  // How strongly the fine-grained effects may show at a given magnification
  // (display pixels per picture pixel, device pixels included). Below ~1.5
  // a scanline is narrower than a display pixel and only makes moire; by 3
  // it has room. The same formula for every resolution.
  function fineWeight(scale, lo, hi) {
    const t = Math.max(0, Math.min(1, (scale - lo) / (hi - lo)));
    return t * t * (3 - 2 * t);
  }

  const VERT = `#version 300 es
in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
`;

  // Uniforms that are 0 skip their effect (a uniform branch: every fragment
  // takes the same way, so it costs nothing to ask).
  const FRAG = `#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_src;      // picture size, pixels
uniform vec2 u_dst;      // display size, device pixels
uniform float u_curve;   // curve coefficient (warpToSource's c)
uniform float u_scan;    // scanline strength, already weighted by magnification
uniform float u_mask;    // grille strength, likewise
uniform float u_glow;    // glow strength
uniform float u_vig;     // dark corners
out vec4 o_color;

vec3 px(ivec2 p) {
  p = clamp(p, ivec2(0), ivec2(u_src) - 1);
  return texelFetch(u_tex, p, 0).rgb;
}

void main() {
  vec2 uv = vec2(gl_FragCoord.x / u_dst.x, 1.0 - gl_FragCoord.y / u_dst.y);
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
  ivec2 ip = ivec2(floor(p));
  vec3 c = px(ip);
  vec3 halo = vec3(0.0);
  if (u_glow > 0.0) {
    // A beam spreads sideways: mix in the neighbours on the row ...
    vec3 l = px(ip + ivec2(-1, 0));
    vec3 r = px(ip + ivec2(1, 0));
    c = mix(c, 0.5 * c + 0.25 * (l + r), 0.6 * u_glow);
    // ... and bright areas light up their surroundings, filling the gaps
    // the scanlines and the grille leave (added after them).
    halo = 0.16 * (l + r)
         + 0.12 * (px(ip + ivec2(0, -1)) + px(ip + ivec2(0, 1)))
         + 0.06 * (px(ip + ivec2(-2, 0)) + px(ip + ivec2(2, 0))
                 + px(ip + ivec2(-1, -1)) + px(ip + ivec2(1, -1))
                 + px(ip + ivec2(-1, 1)) + px(ip + ivec2(1, 1)));
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
`;

  // The view: a WebGL2 canvas laid over the 2D one. create() gives null (and
  // says why on one console line) where WebGL2 or the shader is refused, and
  // the page keeps its plain look.
  function create(glCanvas) {
    let gl = null;
    try {
      gl = glCanvas.getContext('webgl2', { alpha: false, antialias: false, depth: false,
                                           stencil: false, premultipliedAlpha: false,
                                           preserveDrawingBuffer: false });
    } catch (e) { gl = null; }
    if (!gl) {
      console.warn('fmrb: CRT look unavailable: no WebGL2');
      return null;
    }
    function shader(type, src) {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        throw new Error(gl.getShaderInfoLog(s) || 'compile failed');
      }
      return s;
    }
    let prog;
    try {
      prog = gl.createProgram();
      gl.attachShader(prog, shader(gl.VERTEX_SHADER, VERT));
      gl.attachShader(prog, shader(gl.FRAGMENT_SHADER, FRAG));
      gl.linkProgram(prog);
      if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        throw new Error(gl.getProgramInfoLog(prog) || 'link failed');
      }
    } catch (e) {
      console.warn('fmrb: CRT look unavailable: shader: ' + (e && e.message ? e.message : e));
      return null;
    }
    gl.useProgram(prog);
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    const aPos = gl.getAttribLocation(prog, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);
    const U = {};
    for (const n of ['u_tex', 'u_src', 'u_dst', 'u_curve', 'u_scan', 'u_mask', 'u_glow', 'u_vig']) {
      U[n] = gl.getUniformLocation(prog, n);
    }
    const tex = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.uniform1i(U.u_tex, 0);

    let texW = 0, texH = 0;
    let pending = null;      // an ImageData to upload before the next draw
    let dirty = false;       // draw on the next flush
    let lost = false;
    let settings = Object.assign({}, DEFAULTS);
    const stats = { uploads: 0, draws: 0 };

    glCanvas.addEventListener('webglcontextlost', (ev) => {
      ev.preventDefault();
      lost = true;
      console.warn('fmrb: CRT look: WebGL context lost');
    });

    function draw() {
      if (lost || !texW) return false;
      const W = glCanvas.width, H = glCanvas.height;
      const scale = Math.min(W / texW, H / texH);
      gl.viewport(0, 0, W, H);
      gl.uniform2f(U.u_src, texW, texH);
      gl.uniform2f(U.u_dst, W, H);
      gl.uniform1f(U.u_curve, curveCoef(settings.curve));
      gl.uniform1f(U.u_scan, settings.scan / 100 * fineWeight(scale, 1.5, 3.0));
      gl.uniform1f(U.u_mask, settings.mask / 100 * fineWeight(scale, 2.0, 3.0));
      gl.uniform1f(U.u_glow, settings.glow / 100);
      gl.uniform1f(U.u_vig, settings.vignette / 100);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      stats.draws++;
      return true;
    }

    return {
      get lost() { return lost; },
      stats,
      // A new picture (main.js's imageData, already copied out of the
      // SharedArrayBuffer): uploaded on the next flush.
      setImage(imageData) { pending = imageData; dirty = true; },
      setSettings(s) { settings = Object.assign({}, settings, s); dirty = true; },
      // The display canvas's pixel size changed.
      resize(w, h) {
        w = Math.max(1, Math.round(w)); h = Math.max(1, Math.round(h));
        if (glCanvas.width !== w || glCanvas.height !== h) {
          glCanvas.width = w;
          glCanvas.height = h;
          dirty = true;
        }
      },
      // Called once per animation frame; does nothing unless something changed.
      flush() {
        if (lost) return false;
        if (pending) {
          const im = pending;
          pending = null;
          const data = new Uint8Array(im.data.buffer, im.data.byteOffset, im.data.byteLength);
          if (im.width !== texW || im.height !== texH) {
            texW = im.width; texH = im.height;
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, texW, texH, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
          } else {
            gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, texW, texH, gl.RGBA, gl.UNSIGNED_BYTE, data);
          }
          stats.uploads++;
        }
        if (!dirty) return false;
        dirty = false;
        return draw();
      },
      // n draws, each waited for (a one-pixel read back makes the GPU
      // finish), to time one: average milliseconds per draw. Measurement
      // only (the driver's `crt bench=N`); not counted in stats.draws.
      bench(n) {
        if (lost || !texW) return null;
        const px = new Uint8Array(4);
        const t0 = performance.now();
        for (let i = 0; i < n; i++) {
          draw();
          gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        }
        stats.draws -= n;
        dirty = true;
        return (performance.now() - t0) / n;
      },
      // Draw now and read the canvas back in the same task (the drawing
      // buffer is not preserved past it). For the driver's `view`.
      snapshot() {
        if (pending) this.flush();
        draw();
        return glCanvas.toDataURL('image/png');
      },
    };
  }

  const api = { DEFAULTS, KEYS, CURVE_MAX, warpToSource, warpToDisplay, inside, fineWeight, create };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.FmrbCrt = api;
})(typeof window !== 'undefined' ? window : globalThis);
