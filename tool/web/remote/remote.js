// Family mruby remote desktop viewer.
// Phase 1: MJPEG <img src="/stream"> + binary input over WebSocket /ws.
// The device encodes 432px-wide frames (16px alignment); only the left
// 426px are shown. Cursor is drawn client-side from "cur" messages.
// The view is 2x windowed and aspect-preserving fill in fullscreen; every
// size below derives from `scale`, so both modes share one code path.
'use strict';

const VIRT_W = 426, VIRT_H = 240;
const WIN_SCALE = 2;          // scale of the normal (windowed) view

const wrap = document.getElementById('wrap');
const view = document.getElementById('view');
const img = document.getElementById('screen');
const canvas = document.getElementById('canvas');
const cursorEl = document.getElementById('cursor');
const connEl = document.getElementById('conn');
const modeEl = document.getElementById('mode');
const statsEl = document.getElementById('stats');
const fsBtn = document.getElementById('fsbtn');

let encW = 432;
let scale = WIN_SCALE;
let ws = null;
let wsReady = false;
let lastMoveSent = 0;
let useH264 = false;
let videoStarted = false;
let lastCursor = { x: 0, y: 0, v: false };

// --- Layout: one scale factor drives the view, the cursor and hit testing ---

function isFullscreen() {
  return document.fullscreenElement === wrap;
}

// Fullscreen fills as much of the screen as the 426x240 aspect ratio allows
// (16:9 leaves only a hairline letterbox); windowed stays at the fixed 2x.
function computeScale() {
  if (!isFullscreen()) return WIN_SCALE;
  const s = Math.min(window.innerWidth / VIRT_W, window.innerHeight / VIRT_H);
  return s > 0 ? s : WIN_SCALE;
}

function layout() {
  scale = computeScale();
  const vw = Math.round(VIRT_W * scale);
  const vh = Math.round(VIRT_H * scale);
  if (isFullscreen()) {
    wrap.style.width = '';
    wrap.style.height = '';
  } else {
    wrap.style.width = vw + 'px';
    wrap.style.height = vh + 'px';
  }
  view.style.width = vw + 'px';
  view.style.height = vh + 'px';
  // Nearest-neighbour only looks right at whole multiples; a fractional
  // scale would give a pixel grid of uneven thickness, so smooth there.
  const rendering =
    Math.abs(scale - Math.round(scale)) < 0.001 ? 'pixelated' : 'auto';
  const sw = Math.round(encW * scale) + 'px';
  for (const el of [img, canvas]) {
    el.style.width = sw;
    el.style.height = vh + 'px';
    el.style.imageRendering = rendering;
  }
  // The 10x16 CSS px arrow of the 2x view is 5x8 device pixels
  cursorEl.style.width = (5 * scale) + 'px';
  cursorEl.style.height = (8 * scale) + 'px';
  drawCursor();
}

function drawCursor() {
  if (!lastCursor.v) {
    cursorEl.style.display = 'none';
    return;
  }
  cursorEl.style.display = 'block';
  cursorEl.style.left = (lastCursor.x * scale) + 'px';
  cursorEl.style.top = (lastCursor.y * scale) + 'px';
}

function setupScreen() {
  if (useH264) {
    img.style.display = 'none';
    img.src = '';
    canvas.style.display = 'block';
    canvas.width = encW;
    canvas.height = VIRT_H;
    modeEl.textContent = 'mode: h264';
  } else {
    canvas.style.display = 'none';
    img.style.display = 'block';
    img.src = '/stream';
    modeEl.textContent = 'mode: mjpeg';
  }
  layout();
}

// --- Fullscreen ---

// Esc is the browser's fullscreen exit; the Keyboard Lock API hands it to
// the device instead (holding Esc still exits). Secure context only - the
// same requirement the H.264 path already has.
function lockKeys() {
  if (!navigator.keyboard || !navigator.keyboard.lock) return;
  navigator.keyboard.lock(['Escape']).catch(() => {});
}

function unlockKeys() {
  if (navigator.keyboard && navigator.keyboard.unlock) navigator.keyboard.unlock();
}

function toggleFullscreen() {
  if (isFullscreen()) {
    document.exitFullscreen().catch(() => {});
  } else {
    wrap.requestFullscreen().then(lockKeys).catch((e) => {
      console.warn('fullscreen refused', e);
    });
  }
}

document.addEventListener('fullscreenchange', () => {
  if (!isFullscreen()) unlockKeys();
  fsBtn.textContent = isFullscreen() ? 'Exit fullscreen' : 'Fullscreen';
  layout();
  wrap.focus();
});

window.addEventListener('resize', layout);
fsBtn.addEventListener('click', toggleFullscreen);

// --- H.264 path: /ws_video + WebCodecs VideoDecoder -> canvas ---

let videoDeadCount = 0;   // consecutive /ws_video closes with no data

function startVideo() {
  if (videoStarted) return;
  videoStarted = true;
  const ctx = canvas.getContext('2d');
  let decoder = null;
  let gotKey = false;
  let gotData = false;

  function fallbackToMjpeg() {
    console.warn('H.264 path failed, falling back to MJPEG');
    useH264 = false;
    videoStarted = false;
    setupScreen();
  }

  function newDecoder() {
    gotKey = false;
    decoder = new VideoDecoder({
      output: (f) => { ctx.drawImage(f, 0, 0); f.close(); },
      error: (e) => { console.error('decoder error', e); fallbackToMjpeg(); },
    });
    // Annex B mode: no description. Prefer the software decoder: at this
    // resolution it is trivially fast and has no multi-frame pipeline
    // latency (hardware decoders can hold frames back for seconds on a
    // low-fps stream).
    const base = { codec: 'avc1.42e01e', optimizeForLatency: true };
    const sw = Object.assign({ hardwareAcceleration: 'prefer-software' }, base);
    VideoDecoder.isConfigSupported(sw)
      .then((r) => decoder.configure(r.supported ? sw : base))
      .catch(() => decoder.configure(base));
  }
  newDecoder();

  const vws = new WebSocket('ws://' + location.host + '/ws_video');
  vws.binaryType = 'arraybuffer';
  vws.onopen = () => {
    // Ask for an IDR so decoding can start immediately
    vws.send(new Uint8Array([0x04]).buffer);
  };
  vws.onclose = () => {
    if (!useH264) return;
    // Server closes /ws_video without data when its encoder is broken:
    // give up after a few dead connections instead of retrying forever
    videoDeadCount = gotData ? 0 : videoDeadCount + 1;
    if (videoDeadCount >= 3) {
      fallbackToMjpeg();
      return;
    }
    videoStarted = false;
    setTimeout(startVideo, 2000);
  };
  vws.onerror = () => vws.close();
  vws.onmessage = (ev) => {
    if (typeof ev.data === 'string') return;
    gotData = true;
    const d = new DataView(ev.data);
    if (d.getUint8(0) !== 0x01) return;
    const key = (d.getUint8(1) & 1) !== 0;
    const w = d.getUint16(2, true);
    const pts = d.getUint32(4, true);
    if (w !== encW) { encW = w; setupScreen(); }
    if (decoder.state !== 'configured') {
      // configure() is async; if we had to drop a keyframe, ask again
      if (key) vws.send(new Uint8Array([0x04]).buffer);
      return;
    }
    if (!gotKey && !key) return;   // wait for the first keyframe
    gotKey = true;
    try {
      decoder.decode(new EncodedVideoChunk({
        type: key ? 'key' : 'delta',
        timestamp: pts * 1000,
        data: new Uint8Array(ev.data, 8),
      }));
    } catch (e) {
      console.error('decode failed', e);
      try { decoder.close(); } catch (_) {}
      newDecoder();
      vws.send(new Uint8Array([0x04]).buffer);
    }
  };
}

function wsConnect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => {
    wsReady = true;
    connEl.textContent = 'connected';
    connEl.className = 'ok';
  };
  ws.onclose = () => {
    wsReady = false;
    connEl.textContent = 'disconnected (retrying...)';
    connEl.className = 'ng';
    setTimeout(wsConnect, 2000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = (ev) => {
    if (typeof ev.data !== 'string') return;
    let msg;
    try { msg = JSON.parse(ev.data); } catch (_) { return; }
    if (msg.t === 'info') {
      if (msg.encw) { encW = msg.encw; }
      useH264 = !!msg.h264 && typeof window.VideoDecoder === 'function';
      setupScreen();
      if (useH264) startVideo();
    } else if (msg.t === 'cur') {
      lastCursor = { x: msg.x, y: msg.y, v: !!msg.v };
      drawCursor();
    } else if (msg.t === 'stat') {
      statsEl.textContent = msg.fps + ' fps / ' + msg.kbps + ' kbps';
    }
  };
}

function wsSend(buf) {
  if (wsReady && ws.readyState === WebSocket.OPEN) ws.send(buf);
}

function sendMouseMove(x, y) {
  const b = new ArrayBuffer(5);
  const v = new DataView(b);
  v.setUint8(0, 0x01);
  v.setInt16(1, x, true);
  v.setInt16(3, y, true);
  wsSend(b);
}

function sendMouseButton(x, y, btn, state) {
  const b = new ArrayBuffer(7);
  const v = new DataView(b);
  v.setUint8(0, 0x02);
  v.setInt16(1, x, true);
  v.setInt16(3, y, true);
  v.setUint8(5, btn);
  v.setUint8(6, state);
  wsSend(b);
}

function sendMouseWheel(x, y, notches) {
  const b = new ArrayBuffer(6);
  const v = new DataView(b);
  v.setUint8(0, 0x06);
  v.setInt16(1, x, true);
  v.setInt16(3, y, true);
  v.setInt8(5, notches);
  wsSend(b);
}

function sendKey(state, scancode, mod) {
  const b = new ArrayBuffer(4);
  const v = new DataView(b);
  v.setUint8(0, 0x03);
  v.setUint8(1, state);
  v.setUint8(2, scancode);
  v.setUint8(3, mod);
  wsSend(b);
}

// Hit testing goes against #view, not #wrap: in fullscreen #wrap covers the
// whole screen including the letterbox, so its origin is not the picture's.
function eventCoords(e) {
  const r = view.getBoundingClientRect();
  let x = Math.floor((e.clientX - r.left) * VIRT_W / r.width);
  let y = Math.floor((e.clientY - r.top) * VIRT_H / r.height);
  x = Math.max(0, Math.min(VIRT_W - 1, x));
  y = Math.max(0, Math.min(VIRT_H - 1, y));
  return [x, y];
}

// Browser button (0=L,1=M,2=R) -> device SDL numbering (1=L,2=M,3=R)
const BTN_MAP = { 0: 1, 1: 2, 2: 3 };

wrap.addEventListener('mousemove', (e) => {
  const now = performance.now();
  if (now - lastMoveSent < 30) return;   // ~33 msg/s max
  lastMoveSent = now;
  const [x, y] = eventCoords(e);
  sendMouseMove(x, y);
});

wrap.addEventListener('mousedown', (e) => {
  wrap.focus();
  const [x, y] = eventCoords(e);
  const btn = BTN_MAP[e.button];
  if (btn) sendMouseButton(x, y, btn, 1);
  e.preventDefault();
});

wrap.addEventListener('mouseup', (e) => {
  const [x, y] = eventCoords(e);
  const btn = BTN_MAP[e.button];
  if (btn) sendMouseButton(x, y, btn, 0);
  e.preventDefault();
});

// The wheel, in notches. A browser reports pixels, lines or pages depending
// on the device and the platform, and positive deltaY means towards the user --
// the opposite of the sign the machine uses -- so the conversion happens here
// rather than on the device. Fractions from a trackpad are accumulated so a
// slow two-finger drag still delivers whole notches. Same rule as the wasm
// page (wasm/web/main.js), deliberately: one wheel, one meaning.
let wheelAccum = 0;
wrap.addEventListener('wheel', (e) => {
  e.preventDefault();
  let lines = e.deltaY;
  if (e.deltaMode === 1) lines *= 16;        // already lines: one row ~16px
  else if (e.deltaMode === 2) lines *= 400;  // pages
  wheelAccum += -lines / 100;                // 100px per notch, sign flipped
  let notches = wheelAccum > 0 ? Math.floor(wheelAccum) : Math.ceil(wheelAccum);
  if (notches === 0) return;
  wheelAccum -= notches;
  if (notches > 127) notches = 127;
  if (notches < -127) notches = -127;
  const [x, y] = eventCoords(e);
  sendMouseWheel(x, y, notches);
}, { passive: false });

wrap.addEventListener('contextmenu', (e) => e.preventDefault());

// Keys swallowed by a local shortcut: their release must be swallowed too,
// or the device gets a key up with no matching key down.
const suppressedKeys = new Set();

wrap.addEventListener('keydown', (e) => {
  // Reserved locally: the browser keeps F11 for itself, so fullscreen needs
  // a shortcut of our own. Everything else goes to the device.
  if (e.code === 'KeyF' && e.ctrlKey && e.altKey) {
    suppressedKeys.add(e.code);
    if (!e.repeat) toggleFullscreen();
    e.preventDefault();
    return;
  }
  if (e.repeat) return;   // firmware expects report-change semantics
  const sc = RD_HID_KEYMAP[e.code];
  if (!sc) return;
  sendKey(1, sc, rdModMask(e));
  e.preventDefault();
});

wrap.addEventListener('keyup', (e) => {
  if (suppressedKeys.delete(e.code)) {
    e.preventDefault();
    return;
  }
  const sc = RD_HID_KEYMAP[e.code];
  if (!sc) return;
  sendKey(0, sc, rdModMask(e));
  e.preventDefault();
});

setupScreen();
wsConnect();
setInterval(() => {
  fetch('/status').then(r => r.json()).then(s => {
    if (s.streaming) statsEl.textContent = s.fps + ' fps / ' + s.kbps + ' kbps';
  }).catch(() => {});
}, 3000);
