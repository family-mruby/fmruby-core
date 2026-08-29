// Family mruby OS wasm page (doc/wasm/ P4b-P4c).
//
// The firmware runs entirely inside the wasm module (PROXY_TO_PTHREAD); this
// page only moves bytes: the RGBA frame to the canvas each animation frame,
// key/mouse events into the input ring, and the audio ring into an
// AudioWorklet. All shared state lives in the module's SharedArrayBuffer
// memory, so the page needs cross-origin isolation (rake wasm:serve).
'use strict';

const canvas = document.getElementById('screen');
const screenBox = document.getElementById('screen-box');
const ctx = canvas.getContext('2d');
const startOverlay = document.getElementById('start');
const statusLine = document.getElementById('status');

let M = null;          // the emscripten module instance
let lastSeq = -1;
let imageData = null;

function pushInput(type, a, b, c, d) {
  if (!M) return;
  const ring = M._fmrb_wasm_input_ring();
  const events = M._fmrb_wasm_input_ring_events();
  const wrPtr = M._fmrb_wasm_input_wr_ptr();
  const heap = M.HEAPU32;
  const wr = Atomics.load(heap, wrPtr >> 2);
  const rec = (ring >> 2) + (wr % events) * 5;
  heap[rec] = type;
  heap[rec + 1] = a >>> 0;
  heap[rec + 2] = b >>> 0;
  heap[rec + 3] = c >>> 0;
  heap[rec + 4] = d >>> 0;
  Atomics.add(heap, wrPtr >> 2, 1);
}

// ---- display --------------------------------------------------------------

function paint() {
  requestAnimationFrame(paint);
  if (!M) return;
  const seq = M._fmrb_wasm_frame_seq();
  if (seq === lastSeq) return;
  lastSeq = seq;
  const w = M._fmrb_wasm_frame_width();
  const h = M._fmrb_wasm_frame_height();
  const ptr = M._fmrb_wasm_frame_rgba();
  if (!w || !h || !ptr) return;
  if (!imageData || imageData.width !== w || imageData.height !== h) {
    canvas.width = w;
    canvas.height = h;
    imageData = ctx.createImageData(w, h);
    applyZoom();   // the CSS size follows the framebuffer the firmware chose
  }
  // Copy out of the SharedArrayBuffer (ImageData refuses a SAB view).
  imageData.data.set(new Uint8Array(M.HEAPU8.buffer, ptr, w * h * 4));
  ctx.putImageData(imageData, 0, 0);
}

// ---- input ----------------------------------------------------------------

function canvasPos(ev) {
  const r = canvas.getBoundingClientRect();
  const x = Math.round((ev.clientX - r.left) * canvas.width / r.width);
  const y = Math.round((ev.clientY - r.top) * canvas.height / r.height);
  return [Math.min(Math.max(x, 0), canvas.width - 1),
          Math.min(Math.max(y, 0), canvas.height - 1)];
}

function hookInput() {
  canvas.addEventListener('mousemove', (ev) => {
    const [x, y] = canvasPos(ev);
    pushInput(4, x, y, 0, 0);
  });
  canvas.addEventListener('mousedown', (ev) => {
    ev.preventDefault();
    canvas.focus();
    const [x, y] = canvasPos(ev);
    const btn = ev.button === 2 ? 3 : ev.button === 1 ? 2 : 1;
    pushInput(3, x, y, btn, 1);
  });
  canvas.addEventListener('mouseup', (ev) => {
    ev.preventDefault();
    const [x, y] = canvasPos(ev);
    const btn = ev.button === 2 ? 3 : ev.button === 1 ? 2 : 1;
    pushInput(3, x, y, btn, 0);
  });
  canvas.addEventListener('contextmenu', (ev) => ev.preventDefault());

  canvas.addEventListener('keydown', (ev) => {
    const sc = RD_HID_KEYMAP[ev.code];
    if (!sc) return;
    ev.preventDefault();
    pushInput(1, sc, rdModMask(ev), 0, 0);
  });
  canvas.addEventListener('keyup', (ev) => {
    const sc = RD_HID_KEYMAP[ev.code];
    if (!sc) return;
    ev.preventDefault();
    pushInput(2, sc, rdModMask(ev), 0, 0);
  });
}

// ---- audio ----------------------------------------------------------------

// The AudioContext must be created inside the click (browser autoplay
// rules); the worklet is wired once the module is up.
async function startAudio(ac) {
  if (!M) return;
  await ac.audioWorklet.addModule('audio-worklet.js' + (window.FMRB_WASM_VER || ''));
  const node = new AudioWorkletNode(ac, 'fmrb-apu', {
    numberOfInputs: 0,
    numberOfOutputs: 1,
    outputChannelCount: [1],
  });
  node.port.postMessage({
    buffer: M.HEAPU8.buffer,                 // the module's SAB memory
    ringPtr: M._fmrb_wasm_audio_ring(),
    ringSamples: M._fmrb_wasm_audio_ring_size(),
    wrPtr: 0,                                 // no pointer export: poll helper below
    rate: M._fmrb_wasm_audio_rate(),
  });
  // The write counter has no address export; forward it at 60 Hz. The ring
  // is a second of audio, so this granularity is far below the slack.
  setInterval(() => {
    node.port.postMessage({ wr: M._fmrb_wasm_audio_wr(),
                            volume: M._fmrb_wasm_audio_volume() });
  }, 16);
  node.connect(ac.destination);
  await ac.resume();
}

// ---- page settings (localStorage; the key never leaves this browser) ------

// The web default look (cyberpunk palette + neon wallpaper) is baked into
// the bundle; "classic" is requested via argv and applied by the machine
// itself (wasm/backend/page_settings_wasm.c holds the palette).

// The framebuffer sizes offered. 426x240 is what the hardware runs; the other
// two are the same desktop with more room, which only the web build can afford
// (doc/wasm/report/p5.md records what each one looks like).
const RESOLUTIONS = ['426x240', '640x360', '852x480'];
const DEFAULT_RES = '426x240';
// The shipped window default (386x180 of 426x240) kept in proportion, so an
// app window covers the same share of a larger screen instead of shrinking
// into a corner of it.
const USER_APP_W_RATIO = 386 / 426;
const USER_APP_H_RATIO = 180 / 240;

function readSetting(key, fallback) {
  try { return localStorage.getItem(key) || fallback; } catch (e) { return fallback; }
}

function writeSetting(key, value) {
  try { localStorage.setItem(key, value); } catch (e) {}
}

// ?w=&h= names the same thing the selector does; it is written through to the
// stored setting so a plain reload (and a bookmark without the query) keeps it.
function resolutionFromQuery() {
  const q = new URLSearchParams(location.search);
  const w = parseInt(q.get('w'), 10);
  const h = parseInt(q.get('h'), 10);
  if (!w || !h) return null;
  const key = w + 'x' + h;
  return RESOLUTIONS.includes(key) ? key : null;
}

function currentResolution() {
  const fromQuery = resolutionFromQuery();
  if (fromQuery) {
    writeSetting('fmrb_web_res', fromQuery);
    return fromQuery;
  }
  const stored = readSetting('fmrb_web_res', DEFAULT_RES);
  return RESOLUTIONS.includes(stored) ? stored : DEFAULT_RES;
}

// ?theme= mirrors the selector the way ?w=&h= does (shareable, and the only
// way a headless test can pick a theme).
const themeFromQuery = new URLSearchParams(location.search).get('theme');
if (themeFromQuery === 'classic' || themeFromQuery === 'cyberpunk') {
  writeSetting('fmrb_web_theme', themeFromQuery);
}

const themeSelect = document.getElementById('theme-select');
themeSelect.value = readSetting('fmrb_web_theme', 'cyberpunk');
themeSelect.addEventListener('change', () => {
  writeSetting('fmrb_web_theme', themeSelect.value);
  location.reload();   // the theme is read once at boot
});

const resSelect = document.getElementById('res-select');
resSelect.value = currentResolution();
resSelect.addEventListener('change', () => {
  writeSetting('fmrb_web_res', resSelect.value);
  // Drop a stale ?w=&h= on the way out, or it would win over the new choice.
  location.replace(location.pathname + location.hash);
});

// The resolution is the framebuffer; the zoom is only how large that
// framebuffer is drawn, so it takes effect without a reload.
const zoomSelect = document.getElementById('zoom-select');
zoomSelect.value = readSetting('fmrb_web_zoom', 'fit');
zoomSelect.addEventListener('change', () => {
  writeSetting('fmrb_web_zoom', zoomSelect.value);
  applyZoom();
});

function applyZoom() {
  const full = document.fullscreenElement === screenBox;
  const zoom = full ? 'fit' : zoomSelect.value;   // full screen means fill it
  let scale;
  if (zoom === 'fit') {
    // Contain: the whole screen visible, aspect kept, page chrome left room
    // for. Fractional scales stay crisp because the canvas is pixelated.
    let availW, availH;
    if (full) {
      availW = window.innerWidth;
      availH = window.innerHeight;
    } else {
      const box = screenBox.getBoundingClientRect();
      availW = Math.max(document.documentElement.clientWidth - 24, 160);
      availH = Math.max(document.documentElement.clientHeight - box.top - 24, 120);
    }
    scale = Math.max(Math.min(availW / canvas.width, availH / canvas.height), 0.5);
  } else {
    scale = parseFloat(zoom) || 1;
  }
  canvas.style.width = Math.round(canvas.width * scale) + 'px';
  canvas.style.height = Math.round(canvas.height * scale) + 'px';
}

window.addEventListener('resize', () => applyZoom());
applyZoom();

// ---- full screen ----------------------------------------------------------

// The whole display for the machine. Esc is the browser's own way out of full
// screen, so the app never sees it; the Keyboard Lock API hands it back where
// the browser has one (Chrome), and where it does not the page says so rather
// than leaving people wondering why Esc quits.
const fullscreenBtn = document.getElementById('fullscreen-btn');

fullscreenBtn.addEventListener('click', async () => {
  if (document.fullscreenElement) {
    document.exitFullscreen();
    return;
  }
  try {
    await screenBox.requestFullscreen({ navigationUI: 'hide' });
  } catch (e) {
    console.warn('full screen refused:', e);
    return;
  }
  try {
    if (navigator.keyboard && navigator.keyboard.lock) {
      await navigator.keyboard.lock(['Escape', 'KeyW']);
    }
  } catch (e) {
    console.warn('keyboard lock unavailable; Escape leaves full screen:', e);
  }
});

document.addEventListener('fullscreenchange', () => {
  const full = document.fullscreenElement === screenBox;
  fullscreenBtn.textContent = full ? 'Leave full screen' : 'Full screen';
  if (!full && navigator.keyboard && navigator.keyboard.unlock) {
    navigator.keyboard.unlock();
  }
  if (full) canvas.focus();   // keys go to the machine, not the page
  applyZoom();
});

// ---- bootstrap ------------------------------------------------------------

// The machine does not power on until the click -- that is the "start"
// the overlay promises, and it puts the AudioContext (which needs the
// gesture) in place BEFORE boot, so the boot jingle is actually heard.
//
// Say so out loud when the page is not cross-origin isolated: the firmware's
// threads live in a SharedArrayBuffer, so without it the module fails to
// instantiate with an error that names none of this. On a static host
// coi-serviceworker installs itself and reloads once, and the second load is
// isolated; if it still is not, the page cannot run and this is why.
if (typeof crossOriginIsolated !== 'undefined' && !crossOriginIsolated) {
  statusLine.textContent =
    'waiting for cross-origin isolation (the page reloads itself once)';
} else {
  statusLine.textContent = 'ready';
}

const moduleConfig = {
  // The .data (and .wasm) live next to core_web.js, not next to this page;
  // emscripten resolves the data file against the page URL unless told.
  // The version suffix is what keeps a fresh .data from being paired with a
  // cached .wasm; `rake wasm:dist` fills both of these in (dev serves
  // no-cache and leaves them at "../build/" and "").
  locateFile: (p) => (window.FMRB_WASM_BASE || '') + p + (window.FMRB_WASM_VER || ''),
  print: (t) => console.log(t),
  printErr: (t) => console.warn(t),
};
// Settings travel as argv, NOT as page-side FS edits: under PROXY_TO_PTHREAD
// the page's MEMFS and the machine's MEMFS are different filesystems, and a
// preRun writeFile lands in the wrong one (report/p5.md addendum). The C side
// (wasm/backend/page_settings_wasm.c) applies these on the machine's own
// thread before the kernel reads its settings.
moduleConfig.arguments = [];
{
  const res = currentResolution();
  if (res !== DEFAULT_RES) moduleConfig.arguments.push('--fmrb-res=' + res);
  if (readSetting('fmrb_web_theme', 'cyberpunk') === 'classic') {
    moduleConfig.arguments.push('--fmrb-theme=classic');
  }
}

startOverlay.addEventListener('click', async () => {
  startOverlay.style.display = 'none';
  canvas.focus();
  statusLine.textContent = 'booting the firmware...';

  let ac = null;
  try {
    ac = new AudioContext();
    // resume() from a real click settles at once; from anything the browser
    // does not count as a gesture (autostart, some embeddings) it stays
    // pending forever -- never let it hold the boot hostage.
    await Promise.race([ac.resume(),
                        new Promise((r) => setTimeout(r, 500))]);
  } catch (e) {
    console.warn('audio unavailable:', e);
  }

  const mod = await createFmrbCore(moduleConfig);
  M = mod;
  statusLine.textContent = 'running';
  hookInput();
  requestAnimationFrame(paint);
  if (ac) {
    try {
      await startAudio(ac);
    } catch (e) {
      console.warn('audio unavailable:', e);
    }
  }
}, { once: true });

// ?autostart=1 presses the power switch itself -- the headless regression's
// hand. No gesture means the AudioContext may be refused; that path already
// degrades to silent.
if (new URLSearchParams(location.search).get('autostart') === '1' &&
    !(typeof crossOriginIsolated !== 'undefined' && !crossOriginIsolated)) {
  // ?holdload=N keeps the window load event open for N ms (a slow image the
  // dev server serves), so headless Chrome's --screenshot, which fires at
  // load, captures the booted machine instead of the boot message.
  const hold = new URLSearchParams(location.search).get('holdload');
  if (hold) {
    const img = new Image();
    img.style.display = 'none';
    img.src = 'probe-hold?ms=' + encodeURIComponent(hold);
    document.body.appendChild(img);
  }
  startOverlay.click();
}
