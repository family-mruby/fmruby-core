// Family mruby remote desktop viewer.
// Phase 1: MJPEG <img src="/stream"> + binary input over WebSocket /ws.
// The device encodes 432px-wide frames (16px alignment); only the left
// 426px are shown. Cursor is drawn client-side from "cur" messages.
'use strict';

const VIRT_W = 426, VIRT_H = 240;
const SCALE = 2;

const wrap = document.getElementById('wrap');
const img = document.getElementById('screen');
const canvas = document.getElementById('canvas');
const cursorEl = document.getElementById('cursor');
const connEl = document.getElementById('conn');
const modeEl = document.getElementById('mode');
const statsEl = document.getElementById('stats');

let encW = 432;
let ws = null;
let wsReady = false;
let lastMoveSent = 0;
let useH264 = false;
let videoStarted = false;

function setupScreen() {
  wrap.style.width = (VIRT_W * SCALE) + 'px';
  wrap.style.height = (VIRT_H * SCALE) + 'px';
  if (useH264) {
    img.style.display = 'none';
    img.src = '';
    canvas.style.display = 'block';
    canvas.width = encW;
    canvas.height = VIRT_H;
    canvas.style.width = (encW * SCALE) + 'px';
    canvas.style.height = (VIRT_H * SCALE) + 'px';
    modeEl.textContent = 'mode: h264';
  } else {
    canvas.style.display = 'none';
    img.style.display = 'block';
    img.style.width = (encW * SCALE) + 'px';
    img.style.height = (VIRT_H * SCALE) + 'px';
    img.src = '/stream';
    modeEl.textContent = 'mode: mjpeg';
  }
}

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
    // Annex B mode: no description
    decoder.configure({ codec: 'avc1.42e01e', optimizeForLatency: true });
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
      if (msg.v) {
        cursorEl.style.display = 'block';
        cursorEl.style.left = (msg.x * SCALE) + 'px';
        cursorEl.style.top = (msg.y * SCALE) + 'px';
      } else {
        cursorEl.style.display = 'none';
      }
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

function sendKey(state, scancode, mod) {
  const b = new ArrayBuffer(4);
  const v = new DataView(b);
  v.setUint8(0, 0x03);
  v.setUint8(1, state);
  v.setUint8(2, scancode);
  v.setUint8(3, mod);
  wsSend(b);
}

function eventCoords(e) {
  const r = wrap.getBoundingClientRect();
  let x = Math.floor((e.clientX - r.left) * VIRT_W / (VIRT_W * SCALE));
  let y = Math.floor((e.clientY - r.top) * VIRT_H / (VIRT_H * SCALE));
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

wrap.addEventListener('contextmenu', (e) => e.preventDefault());

wrap.addEventListener('keydown', (e) => {
  if (e.repeat) return;   // firmware expects report-change semantics
  const sc = RD_HID_KEYMAP[e.code];
  if (!sc) return;
  sendKey(1, sc, rdModMask(e));
  e.preventDefault();
});

wrap.addEventListener('keyup', (e) => {
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
