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
zoomSelect.value = readSetting('fmrb_web_zoom', '2');   // 2x by default; full screen still fits
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
// Settings travel as argv, NOT as page-side FS edits. The C side
// (wasm/backend/page_settings_wasm.c) applies these on the machine's own
// thread before the kernel reads its settings, which is the surer order
// whatever the page can reach (see ?fsprobe=1 below and report/storage_t0.md).
moduleConfig.arguments = [];
{
  const res = currentResolution();
  if (res !== DEFAULT_RES) moduleConfig.arguments.push('--fmrb-res=' + res);
  if (readSetting('fmrb_web_theme', 'cyberpunk') === 'classic') {
    moduleConfig.arguments.push('--fmrb-theme=classic');
  }
}
// Everything the page wants to do before main() goes in here, in order.
moduleConfig.preRun = [];

// ---- /home that outlives the tab ---------------------------------------
//
// There is one filesystem and it lives on this thread: every file call the
// machine makes is proxied here (measured in report/storage_t0.md). So the
// page can put a persistent mount under the machine's feet.
//
// /home is the one directory the bundle ships nothing into, which is what
// makes this safe: the packer's own unpack runs after this and would
// otherwise overwrite what we put down. We create the directory, mount
// IDBFS over it, and read IndexedDB back BEFORE the machine boots -- as a
// run dependency, the same gate the packed .data uses, so main() waits for
// both.
//
// If any of it fails (private window, storage refused), the machine still
// boots; /home is then an ordinary directory that lasts as long as the tab.
const HOME_PATH = '/flash/home';
const HOME_DEP = 'fmrb-home-load';
let homeStore = 'volatile';

// One tab writes. Two machines sharing one IndexedDB would overwrite each
// other wholesale (last one to sync wins), so the second tab runs with a
// throw-away /home rather than eating the first one's work. The lock is held
// for the life of the page, which is exactly how long the machine runs.
let homeWriter = null;
let lockDecided = Promise.resolve();
if (navigator.locks) {
  let decided;
  lockDecided = new Promise((r) => { decided = r; });
  navigator.locks.request('fmrb-home', { ifAvailable: true }, (lock) => {
    homeWriter = !!lock;
    decided();
    // Say so before the machine is even switched on: a second tab that is
    // about to lose its work should hear about it first.
    if (!lock) showStorageState();
    return lock ? new Promise(() => {}) : undefined;
  }).catch(() => { homeWriter = true; decided(); });
} else {
  homeWriter = true;
}

function mountHome(mod) {
  mod.addRunDependency(HOME_DEP);
  lockDecided.then(() => {
    if (homeWriter === false) {
      homeStore = 'second tab';
      console.warn('fmrb: another tab owns /home; this one runs on a copy ' +
                   'that is thrown away');
      mod.removeRunDependency(HOME_DEP);
      showStorageState();
      return;
    }
    try {
      mod.FS.mkdirTree(HOME_PATH);
      mod.FS.mount(mod.FS.filesystems.IDBFS, { autoPersist: true }, HOME_PATH);
    } catch (e) {
      homeStore = 'not available';
      console.warn('fmrb: /home will not persist (mount failed):', e);
      mod.removeRunDependency(HOME_DEP);
      showStorageState();
      return;
    }
    mod.FS.syncfs(true, (err) => {
      if (err) {
        homeStore = 'not available';
        console.warn('fmrb: /home could not be read back:', err);
      } else {
        homeStore = 'persistent';
      }
      mod.removeRunDependency(HOME_DEP);
      showStorageState();
    });
  });
}
moduleConfig.preRun.push(mountHome);

// What the page says about all this. "persistent" only means the files come
// back on a reload; whether the browser may throw them away later is the
// separate question navigator.storage.persist() answers.
const storageStateEl = document.getElementById('storage-state');
let storageDurable = null;
let storageUsage = '';
function showStorageState() {
  if (!storageStateEl) return;
  let text;
  if (homeStore === 'persistent') {
    text = 'kept in this browser';
    if (storageDurable === true) text += ', protected from eviction';
    else if (storageDurable === false) text += ', but the browser may reclaim it';
  } else if (homeStore === 'second tab' || homeWriter === false) {
    text = 'another tab has them open -- changes here are NOT saved';
  } else {
    text = 'not saved (this browser refuses storage; the tab keeps them only ' +
           'while it is open)';
  }
  if (storageUsage) text += ' -- ' + storageUsage;
  storageStateEl.textContent = text;
}
async function measureStorage() {
  try {
    if (navigator.storage && navigator.storage.persist && homeStore === 'persistent') {
      storageDurable = await navigator.storage.persisted();
      if (!storageDurable) storageDurable = await navigator.storage.persist();
    }
    if (navigator.storage && navigator.storage.estimate) {
      const est = await navigator.storage.estimate();
      if (est && est.usage != null) {
        storageUsage = 'using ' + Math.max(1, Math.round(est.usage / 1024)) + ' KB';
      }
    }
  } catch (e) {
    console.warn('fmrb: storage estimate unavailable:', e);
  }
  showStorageState();
}

// ---- taking /home with you ---------------------------------------------
//
// Browser storage is not a safe place to keep the only copy: a private
// window never keeps it, and a browser may reclaim it after a while of not
// visiting. So the page can hand the whole of /home over as a tar and take
// one back. tar (not zip) because it is a few lines to write and to read,
// and because the device speaks it too.

function tarOctal(value, length) {
  let s = value.toString(8);
  while (s.length < length - 1) s = '0' + s;
  return s + '\0';
}

function tarHeader(name, size, isDir) {
  const h = new Uint8Array(512);
  const put = (offset, text) => {
    for (let i = 0; i < text.length; i++) h[offset + i] = text.charCodeAt(i);
  };
  // 100 bytes of name is all a plain tar header has; longer paths would need
  // the prefix field, and nothing in /home comes close.
  put(0, name.slice(0, 100));
  put(100, tarOctal(isDir ? 0o755 : 0o644, 8));
  put(108, tarOctal(0, 8));
  put(116, tarOctal(0, 8));
  put(124, tarOctal(size, 12));
  put(136, tarOctal(Math.floor(Date.now() / 1000), 12));
  put(148, '        ');            // checksum field counts as spaces
  put(156, isDir ? '5' : '0');
  put(257, 'ustar\0' + '00');
  let sum = 0;
  for (let i = 0; i < 512; i++) sum += h[i];
  put(148, sum.toString(8).padStart(6, '0') + '\0 ');
  return h;
}

// Walk /home and return [{name, data|null, isDir}] with names relative to it.
function homeEntries(mod) {
  const out = [];
  const walk = (dir, prefix) => {
    let names;
    try {
      names = mod.FS.readdir(dir);
    } catch (e) {
      return;
    }
    for (const name of names) {
      if (name === '.' || name === '..') continue;
      const path = dir + '/' + name;
      const rel = prefix + name;
      let st;
      try {
        st = mod.FS.stat(path);
      } catch (e) {
        continue;
      }
      if (mod.FS.isDir(st.mode)) {
        out.push({ name: rel + '/', isDir: true });
        walk(path, rel + '/');
      } else if (mod.FS.isFile(st.mode)) {
        out.push({ name: rel, isDir: false, data: mod.FS.readFile(path) });
      }
    }
  };
  walk(HOME_PATH, '');
  return out;
}

function exportHome() {
  if (!M) return;
  const entries = homeEntries(M);
  const parts = [];
  for (const e of entries) {
    const size = e.isDir ? 0 : e.data.length;
    parts.push(tarHeader(e.name, size, e.isDir));
    if (!e.isDir) {
      parts.push(e.data);
      const pad = (512 - (size % 512)) % 512;
      if (pad) parts.push(new Uint8Array(pad));
    }
  }
  parts.push(new Uint8Array(1024));  // two empty blocks end a tar
  const blob = new Blob(parts, { type: 'application/x-tar' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  const now = new Date();
  const stamp = now.toISOString().slice(0, 10).replace(/-/g, '');
  a.download = 'fmrb-home-' + stamp + '.tar';
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  statusLine.textContent = 'wrote ' + entries.length + ' entries to a .tar';
}

// A tar from anywhere at all: every name is rebuilt from safe parts, so
// nothing in the archive can name a path outside /home.
function safeRelative(name) {
  const parts = name.split('/').filter((p) => p !== '' && p !== '.' && p !== '..');
  return parts.join('/');
}

function importTar(mod, bytes) {
  let offset = 0;
  let files = 0;
  const decoder = new TextDecoder();
  while (offset + 512 <= bytes.length) {
    const head = bytes.subarray(offset, offset + 512);
    if (head.every((b) => b === 0)) break;
    const rawName = decoder.decode(head.subarray(0, 100)).replace(/\0.*$/, '');
    const sizeField = decoder.decode(head.subarray(124, 136)).replace(/[\0 ]/g, '');
    const size = parseInt(sizeField, 8) || 0;
    const type = String.fromCharCode(head[156]);
    offset += 512;
    const rel = safeRelative(rawName);
    if (rel) {
      const full = HOME_PATH + '/' + rel;
      if (type === '5') {
        try { mod.FS.mkdirTree(full); } catch (e) { /* already there */ }
      } else if (type === '0' || type === '\0' || type === '') {
        const slash = full.lastIndexOf('/');
        try { mod.FS.mkdirTree(full.slice(0, slash)); } catch (e) { /* already there */ }
        mod.FS.writeFile(full, bytes.subarray(offset, offset + size));
        files++;
      }
    }
    offset += Math.ceil(size / 512) * 512;
  }
  return files;
}

function removeHomeContents(mod) {
  const entries = homeEntries(mod).reverse();  // children before their parents
  for (const e of entries) {
    const full = HOME_PATH + '/' + e.name.replace(/\/$/, '');
    try {
      if (e.isDir) mod.FS.rmdir(full);
      else mod.FS.unlink(full);
    } catch (e2) {
      console.warn('fmrb: cannot remove ' + full + ':', e2);
    }
  }
}

// autoPersist writes back on its own after every change, but a tab that is
// hidden or closed may go away before that timer runs; ask once more then.
// beforeunload is not used on purpose -- the write is asynchronous and the
// page is already gone by the time it would finish.
function flushHome() {
  if (homeStore !== 'persistent' || !M) return;
  try {
    M.FS.syncfs(false, (err) => {
      if (err) console.warn('fmrb: /home flush failed:', err);
    });
  } catch (e) {
    console.warn('fmrb: /home flush failed:', e);
  }
}
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'hidden') flushHome();
});
window.addEventListener('pagehide', flushHome);

const exportBtn = document.getElementById('home-export');
const importBtn = document.getElementById('home-import-btn');
const importInput = document.getElementById('home-import');
const resetBtn = document.getElementById('home-reset');
function enableStorageButtons() {
  for (const b of [exportBtn, importBtn, resetBtn]) if (b) b.disabled = false;
}
if (exportBtn) exportBtn.addEventListener('click', exportHome);
if (importBtn && importInput) {
  importBtn.addEventListener('click', () => importInput.click());
  importInput.addEventListener('change', async () => {
    const file = importInput.files && importInput.files[0];
    importInput.value = '';
    if (!file || !M) return;
    const bytes = new Uint8Array(await file.arrayBuffer());
    let files;
    try {
      files = importTar(M, bytes);
    } catch (e) {
      statusLine.textContent = 'that file could not be read as a .tar';
      console.warn('fmrb: import failed:', e);
      return;
    }
    flushHome();
    statusLine.textContent = 'restored ' + files + ' file(s) into /home -- ' +
      'reload the page to let the machine see them';
    measureStorage();
  });
}
if (resetBtn) {
  resetBtn.addEventListener('click', () => {
    if (!M) return;
    if (!window.confirm('Erase everything in /home? Download it first if you ' +
                        'want to keep it.')) return;
    removeHomeContents(M);
    flushHome();
    statusLine.textContent = '/home is empty again -- reload the page';
    measureStorage();
  });
}

// ?fsprobe=1 -- a development measurement, not part of the machine.
//
// Two questions decide how storage can be made to persist
// (doc/wasm/instruction_storage.md T0):
//
//   A. is the page's FS the same one the machine writes to?  The machine
//      rewrites /flash/etc/system_conf.toml at startup (page_settings_wasm.c),
//      so reading display_width back out through the page's FS answers it.
//   B. does a preRun write survive?  The file packager appends its own
//      unpack to Module.preRun, so anything the page writes there may be
//      overwritten a moment later. A marker written in preRun says which.
//
// Every line goes to console.log so headless Chrome can collect them.
const FSPROBE = new URLSearchParams(location.search).get('fsprobe') === '1';
const CONF_PATH = '/flash/etc/system_conf.toml';
const PROBE_MARK = '# fsprobe-marker';
// Headless Chrome reports through --dump-dom far more reliably than through
// its console log, so every probe line lands in the page as well.
function probeLog(line) {
  console.log(line);
  let box = document.getElementById('fsprobe-out');
  if (!box) {
    box = document.createElement('pre');
    box.id = 'fsprobe-out';
    document.body.appendChild(box);
  }
  box.textContent += line + '\n';
}
if (FSPROBE) {
  moduleConfig.preRun.push((mod) => {
    // B: write a marker into a file the package also carries. Reading it
    // first tells us whether the page even sees the packed tree yet.
    let before = null;
    try {
      before = mod.FS.readFile(CONF_PATH, { encoding: 'utf8' });
    } catch (e) {
      before = null;
    }
    probeLog('FSPROBE preRun: conf readable before unpack = ' +
             (before === null ? 'no' : 'yes (' + before.length + ' bytes)'));
    try {
      mod.FS.writeFile(CONF_PATH, PROBE_MARK + '\n' + (before || ''));
      probeLog('FSPROBE preRun: marker written');
    } catch (e) {
      probeLog('FSPROBE preRun: marker write failed: errno=' + e.errno);
    }
    // The seam T2 needs: /home is in no package, so the page can make it
    // here and put something in it. If that survives to the end of the boot,
    // a mount can go in the same place.
    try {
      mod.FS.mkdirTree('/flash/home');
      mod.FS.writeFile('/flash/home/fsprobe.txt', 'page-preRun');
      probeLog('FSPROBE preRun: /flash/home made and written');
    } catch (e) {
      probeLog('FSPROBE preRun: /flash/home failed: errno=' + e.errno);
    }
  });
}

// ?fsprobe=1 also checks the one-writer rule, by loading a second copy of
// this page in an iframe (same origin, so the same Web Lock) and reading the
// line it shows. The child does not autostart -- it only asks for the lock.
function fsprobeSecondTab() {
  return new Promise((resolve) => {
    const f = document.createElement('iframe');
    f.style.display = 'none';
    f.src = 'index.html?secondtab=1';
    f.addEventListener('load', () => {
      setTimeout(() => {
        let text = '(unreadable)';
        try {
          text = f.contentDocument.getElementById('storage-state').textContent;
        } catch (e) { /* keep the placeholder */ }
        probeLog('FSPROBE lock: second tab says "' + text + '"');
        f.remove();
        resolve();
      }, 1500);
    });
    document.body.appendChild(f);
  });
}

function fsprobeReport(mod) {
  let text = null;
  try {
    text = mod.FS.readFile(CONF_PATH, { encoding: 'utf8' });
  } catch (e) {
    probeLog('FSPROBE result: page cannot read ' + CONF_PATH + ': ' + e);
    return;
  }
  const width = (text.match(/^display_width\s*=\s*(\d+)/m) || [])[1];
  probeLog('FSPROBE result: display_width=' + width +
           ' asked=' + currentResolution() +
           ' marker=' + (text.indexOf(PROBE_MARK) === 0 ? 'kept' : 'gone'));
  // A second, direct reading of A: a directory only the machine creates.
  try {
    mod.FS.stat('/flash/home');
    probeLog('FSPROBE result: /flash/home visible to the page = yes');
  } catch (e) {
    probeLog('FSPROBE result: /flash/home visible to the page = no');
  }
  // And whether what the page put there before the boot is still there.
  let kept = null;
  try {
    kept = mod.FS.readFile('/flash/home/fsprobe.txt', { encoding: 'utf8' });
  } catch (e) {
    kept = null;
  }
  probeLog('FSPROBE result: page file in /home after boot = ' +
           (kept === null ? 'gone' : JSON.stringify(kept)));
  probeLog('FSPROBE result: home store = ' + homeStore);
  // The three buttons are useless while they are disabled, and a disabled
  // button swallows the click without a sound -- which is exactly how this
  // shipped broken once.
  probeLog('FSPROBE result: buttons enabled = ' +
           ['home-export', 'home-import-btn', 'home-reset']
             .map((id) => id + '=' + !document.getElementById(id).disabled)
             .join(' '));
  // T4: write a couple of files, tar them in memory, wipe, restore, look.
  try {
    mod.FS.writeFile(HOME_PATH + '/t4.txt', 'hello');
    mod.FS.mkdirTree(HOME_PATH + '/sub');
    mod.FS.writeFile(HOME_PATH + '/sub/deep.txt', 'nested');
    const entries = homeEntries(mod);
    const parts = [];
    for (const e of entries) {
      const size = e.isDir ? 0 : e.data.length;
      parts.push(tarHeader(e.name, size, e.isDir));
      if (!e.isDir) {
        parts.push(e.data);
        const pad = (512 - (size % 512)) % 512;
        if (pad) parts.push(new Uint8Array(pad));
      }
    }
    // An entry that tries to climb out, appended by hand.
    const evil = new TextEncoder().encode('owned');
    parts.push(tarHeader('../escaped.txt', evil.length, false), evil,
               new Uint8Array(512 - evil.length));
    parts.push(new Uint8Array(1024));
    let total = 0;
    for (const p of parts) total += p.length;
    const tar = new Uint8Array(total);
    let at = 0;
    for (const p of parts) { tar.set(p, at); at += p.length; }
    removeHomeContents(mod);
    probeLog('FSPROBE t4: after erase /home holds ' +
             JSON.stringify(mod.FS.readdir(HOME_PATH)));
    const n = importTar(mod, tar);
    probeLog('FSPROBE t4: restored ' + n + ' file(s), /home holds ' +
             JSON.stringify(mod.FS.readdir(HOME_PATH)) +
             ' sub=' + JSON.stringify(mod.FS.readdir(HOME_PATH + '/sub')) +
             ' deep=' + mod.FS.readFile(HOME_PATH + '/sub/deep.txt',
                                        { encoding: 'utf8' }));
    let escaped = 'no';
    try { mod.FS.stat('/flash/escaped.txt'); escaped = 'YES'; } catch (e) { }
    probeLog('FSPROBE t4: escaped out of /home = ' + escaped +
             ', landed as ' + JSON.stringify(mod.FS.readdir(HOME_PATH)));
    // leave nothing behind
    removeHomeContents(mod);
  } catch (e) {
    probeLog('FSPROBE t4: failed: ' + e);
  }

  // The probe's own file must not settle into the user's /home.
  try { mod.FS.unlink('/flash/home/fsprobe.txt'); } catch (e) { /* fine */ }
  // What the MACHINE wrote into the mounted /home (the T2 path: worker ->
  // proxied syscall -> IDBFS mount -> IndexedDB).
  let machine = null;
  try {
    machine = mod.FS.readFile('/flash/home/machine_probe.txt', { encoding: 'utf8' });
  } catch (e) {
    machine = null;
  }
  probeLog('FSPROBE result: machine_probe = ' + (machine === null ? 'absent' : machine));
  try {
    probeLog('FSPROBE result: /home holds ' +
             JSON.stringify(mod.FS.readdir('/flash/home')));
  } catch (e) {
    probeLog('FSPROBE result: /home unreadable: errno=' + e.errno);
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
  // The machine rewrites the settings file early in its own boot; give it a
  // moment before reading it back (the probe is a measurement, not a race).
  if (FSPROBE) setTimeout(() => fsprobeReport(mod), 3000);
  if (FSPROBE) setTimeout(fsprobeSecondTab, 6000);
  measureStorage();
  enableStorageButtons();
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

// ---- ?drive=1: let a tool outside the browser work the machine -----------
//
// The page asks the development server for a command, does it, and posts the
// answer back (tools/fmrb_web.rb is the other end). Everything a hand can do
// here -- click, type, look at the screen, look at a file -- can then be
// scripted, which is what the Linux sim has had all along and the browser
// had not.
//
// Events go into the same ring the real mouse and keyboard use, so a driven
// run is indistinguishable from a used one on the machine's side. The
// scancodes come from the tool (the firmware's own keymap, layout and all);
// the page only carries them.
const DRIVE = new URLSearchParams(location.search).get('drive') === '1';

function driveSleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function runDriveCommand(cmd) {
  switch (cmd.op) {
    case 'input': {
      for (const ev of cmd.events || []) {
        const [type, a, b, c, d, delay] = ev;
        if (delay) await driveSleep(delay);
        pushInput(type, a || 0, b || 0, c || 0, d || 0);
      }
      return { events: (cmd.events || []).length };
    }
    case 'screenshot': {
      // The canvas holds exactly the frame the firmware produced, at its own
      // resolution -- not the zoomed CSS size, which is what a browser-level
      // screenshot would give.
      const url = canvas.toDataURL('image/png');
      return { width: canvas.width, height: canvas.height,
               png: url.slice(url.indexOf(',') + 1) };
    }
    case 'status':
      return {
        running: !!M,
        width: canvas.width, height: canvas.height,
        frame: M ? M._fmrb_wasm_frame_seq() : 0,
        home: homeStore,
        durable: storageDurable,
      };
    case 'fs': {
      if (!M) throw new Error('the machine is not running yet');
      const path = cmd.path || '';
      if (cmd.action === 'ls') {
        const out = [];
        for (const name of M.FS.readdir(path)) {
          if (name === '.' || name === '..') continue;
          const st = M.FS.stat(path + '/' + name);
          out.push({ name, dir: M.FS.isDir(st.mode), size: st.size });
        }
        return { entries: out };
      }
      if (cmd.action === 'cat') {
        const data = M.FS.readFile(path);
        let bin = '';
        for (let i = 0; i < data.length; i += 0x8000) {
          bin += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
        }
        return { size: data.length, base64: btoa(bin) };
      }
      if (cmd.action === 'put') {
        const raw = atob(cmd.base64 || '');
        const data = new Uint8Array(raw.length);
        for (let i = 0; i < raw.length; i++) data[i] = raw.charCodeAt(i);
        const slash = path.lastIndexOf('/');
        if (slash > 0) M.FS.mkdirTree(path.slice(0, slash));
        M.FS.writeFile(path, data);
        flushHome();
        return { size: data.length };
      }
      if (cmd.action === 'rm') {
        M.FS.unlink(path);
        flushHome();
        return { removed: path };
      }
      throw new Error('unknown fs action: ' + cmd.action);
    }
    case 'reload':
      // Answer first; the page is gone a moment later.
      setTimeout(() => location.reload(), 200);
      return { reloading: true };
    default:
      throw new Error('unknown op: ' + cmd.op);
  }
}

async function driveLoop() {
  for (;;) {
    let cmd = null;
    try {
      const res = await fetch('drive/cmd');
      if (res.status === 200) cmd = await res.json();
    } catch (e) {
      await driveSleep(500);      // the server went away; keep trying
      continue;
    }
    if (!cmd) continue;           // 204: nothing waiting, ask again
    let body;
    try {
      body = { id: cmd.id, result: await runDriveCommand(cmd) };
    } catch (e) {
      body = { id: cmd.id, error: String(e && e.message ? e.message : e) };
    }
    try {
      await fetch('drive/res', { method: 'POST', body: JSON.stringify(body) });
    } catch (e) {
      console.warn('fmrb: cannot report the result:', e);
    }
  }
}
if (DRIVE) driveLoop();

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
