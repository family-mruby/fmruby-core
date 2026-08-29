// Family mruby OS wasm page (doc/wasm/ P4b-P4c).
//
// The firmware runs entirely inside the wasm module (PROXY_TO_PTHREAD); this
// page only moves bytes: the RGBA frame to the canvas each animation frame,
// key/mouse events into the input ring, and the audio ring into an
// AudioWorklet. All shared state lives in the module's SharedArrayBuffer
// memory, so the page needs cross-origin isolation (rake wasm:serve).
'use strict';

const canvas = document.getElementById('screen');
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

async function startAudio() {
  if (!M) return;
  const ac = new AudioContext();
  await ac.audioWorklet.addModule('audio-worklet.js');
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

// The web default palette is baked into the bundled system_conf (cyberpunk);
// "classic" rewrites the [theme] block in MEMFS before the firmware boots.
// Same mechanism the resolution selection will use: the packed conf is just a
// file, and preRun runs after it is unpacked and before main().
const CLASSIC_THEME = {
  desktop_bg: '0xF6', menu_bg: '0xC5', window_bg: '0xFF',
  text: '0x00', text_light: '0xFF', highlight: '0xEE',
  border: '0x60', button: '0x60', dir_color: '0x03',
};

function readSetting(key, fallback) {
  try { return localStorage.getItem(key) || fallback; } catch (e) { return fallback; }
}

const themeSelect = document.getElementById('theme-select');
themeSelect.value = readSetting('fmrb_web_theme', 'cyberpunk');
themeSelect.addEventListener('change', () => {
  try { localStorage.setItem('fmrb_web_theme', themeSelect.value); } catch (e) {}
  location.reload();   // the theme is read once at boot
});

function applyThemeSetting(mod) {
  // The packed defaults ARE the cyberpunk theme (palette in system_conf,
  // neon wallpaper staged at /data), so the default needs no work here.
  // Only "classic" edits MEMFS: palette back to the device values, and the
  // device (western) wallpaper back over /data. Failures are surfaced --
  // a silent catch here once hid a broken swap.
  if (readSetting('fmrb_web_theme', 'cyberpunk') !== 'classic') return;
  try {
    const path = '/flash/etc/system_conf.toml';
    let conf = new TextDecoder().decode(mod.FS.readFile(path));
    for (const [key, val] of Object.entries(CLASSIC_THEME)) {
      conf = conf.replace(new RegExp('^' + key + ' = 0x[0-9A-Fa-f]+', 'm'),
                          key + ' = ' + val);
    }
    mod.FS.writeFile(path, conf);
    mod.FS.writeFile('/flash/data/bg_426x240.png',
      mod.FS.readFile('/flash/usr/share/backgrounds/bg_426x240.png'));
  } catch (e) {
    console.error('classic theme could not be applied:', e);
  }
}

// ---- bootstrap ------------------------------------------------------------

statusLine.textContent = 'booting the firmware...';

const moduleConfig = {
  // The .data (and .wasm) live next to core_web.js, not next to this page;
  // emscripten resolves the data file against the page URL unless told.
  locateFile: (p) => '../build/' + p,
  print: (t) => console.log(t),
  printErr: (t) => console.warn(t),
};
// preRun runs inside run(), after the preloaded .data is unpacked into MEMFS
// and before main() -- the one moment the packed files can be edited.
moduleConfig.preRun = [() => applyThemeSetting(moduleConfig)];

createFmrbCore(moduleConfig).then((mod) => {
  M = mod;
  statusLine.textContent = 'running';
  hookInput();
  requestAnimationFrame(paint);
});

startOverlay.addEventListener('click', async () => {
  startOverlay.style.display = 'none';
  canvas.focus();
  try {
    await startAudio();
  } catch (e) {
    console.warn('audio unavailable:', e);
  }
});
