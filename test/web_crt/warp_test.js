// The CRT look's curve (wasm/web/crt.js, doc/web_crt/): the display ->
// picture map the shader and the click both use, and its inverse.
//
//   node test/web_crt/warp_test.js     (rake webcrt:test, part of rake test)
'use strict';
const path = require('path');
const crt = require(path.join(__dirname, '..', '..', 'wasm', 'web', 'crt.js'));

let failures = 0, checks = 0;
function check(ok, what) {
  checks++;
  if (!ok) { failures++; console.log('FAIL ' + what); }
}
const near = (a, b, eps) => Math.abs(a - b) <= eps;

// Curve 0 is the identity, both ways.
for (const [u, v] of [[0, 0], [0.25, 0.75], [0.999, 0.5]]) {
  const s = crt.warpToSource(u, v, 0);
  const d = crt.warpToDisplay(u, v, 0);
  check(s[0] === u && s[1] === v && d[0] === u && d[1] === v, `identity at ${u},${v}`);
}

for (const curve of [10, 40, 100]) {
  // Round trip picture -> display -> picture over a grid, edges included.
  let worst = 0;
  for (let i = 0; i <= 40; i++) {
    for (let j = 0; j <= 40; j++) {
      const x = i / 40, y = j / 40;
      const d = crt.warpToDisplay(x, y, curve);
      const s = crt.warpToSource(d[0], d[1], curve);
      worst = Math.max(worst, Math.abs(s[0] - x), Math.abs(s[1] - y));
    }
  }
  check(worst < 1e-9, `curve ${curve}: round trip error ${worst}`);

  // The whole picture stays on the display: its corners and edge midpoints
  // land inside 0..1, and strictly inward of the display's edges.
  for (const [x, y] of [[0, 0], [1, 0], [0, 1], [1, 1], [0.5, 0], [0, 0.5], [1, 0.5], [0.5, 1]]) {
    const d = crt.warpToDisplay(x, y, curve);
    check(d[0] >= 0 && d[0] <= 1 && d[1] >= 0 && d[1] <= 1, `curve ${curve}: picture point ${x},${y} on the display`);
  }
  // The display's corners are outside the picture (the black edge).
  for (const [u, v] of [[0, 0], [1, 0], [0, 1], [1, 1]]) {
    check(!crt.inside(crt.warpToSource(u, v, curve)), `curve ${curve}: display corner ${u},${v} outside`);
  }
  // The centre stays put.
  const c = crt.warpToSource(0.5, 0.5, curve);
  check(near(c[0], 0.5, 1e-12) && near(c[1], 0.5, 1e-12), `curve ${curve}: centre`);

  // Pixel by pixel, the way a click is sent: the display point that shows
  // the centre of a picture pixel goes back to that pixel (426x240 and the
  // high-resolution 640x360).
  for (const [w, h] of [[426, 240], [640, 360]]) {
    let wrong = 0;
    for (let py = 0; py < h; py++) {
      for (let px = 0; px < w; px++) {
        const d = crt.warpToDisplay((px + 0.5) / w, (py + 0.5) / h, curve);
        const s = crt.warpToSource(d[0], d[1], curve);
        if (Math.floor(s[0] * w) !== px || Math.floor(s[1] * h) !== py) wrong++;
      }
    }
    check(wrong === 0, `curve ${curve}: ${w}x${h} pixels that do not come back: ${wrong}`);
  }
}

// The curve bends outwards from the centre: a point halfway to the right
// edge on the display shows a picture point further out.
check(crt.warpToSource(0.75, 0.5, 40)[0] > 0.75, 'barrel direction');

// Fine effects fade with the magnification and are full by 3x.
check(crt.fineWeight(1.0, 1.5, 3.0) === 0, 'scanlines off at 1x');
check(crt.fineWeight(3.0, 1.5, 3.0) === 1, 'scanlines full at 3x');
check(crt.fineWeight(2.0, 1.5, 3.0) > 0 && crt.fineWeight(2.0, 1.5, 3.0) < 1, 'scanlines partial at 2x');

console.log(`web_crt warp: ${checks - failures}/${checks} passed`);
process.exit(failures ? 1 : 0);
