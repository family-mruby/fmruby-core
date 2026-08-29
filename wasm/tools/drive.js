// drive.js - boot the wasm core under node, inject input events, dump frames.
// The headless verification loop for the wasm target: what sim_input +
// sim_screenshot are to the Linux simulation.
//
//   node wasm/tools/drive.js <script...>
//
// Script commands (space separated, executed in order):
//   wait:N          sleep N ms
//   move:X,Y        mouse move
//   click:X,Y[,B]   button down+up at X,Y (B: 1=left 3=right, default 1)
//   key:SCANCODE[,MOD]  key down+up (HID usage id; MOD = FMRB_KEYMAP_MOD bits)
//   text:STR        type ASCII text (letters, digits, space, minimal symbols)
//   shot:FILE       dump the current frame as PNG
//   rec:MS[,FILE]   record the audio ring for MS ms (16-bit mono WAV when
//                   FILE given) and report per-100ms RMS + dominant frequency
//
// Example:
//   node wasm/tools/drive.js wait:20000 shot:a.png click:16,232 wait:2000 shot:b.png
'use strict';

const path = require('path');
const zlib = require('zlib');
const fs = require('fs');

function crcOf(...bufs) {
  let crc = 0xFFFFFFFF;
  for (const b of bufs) {
    for (let i = 0; i < b.length; i++) {
      let c = (crc ^ b[i]) & 0xFF;
      for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
      crc = (crc >>> 8) ^ c;
    }
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const t = Buffer.from(type, 'ascii');
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crcOf(t, data));
  return Buffer.concat([len, t, data, crc]);
}

function writePng(w, h, rgba, file) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  fs.writeFileSync(file, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]));
}

// US keyboard: char -> [HID scancode, needs_shift]. Enough for smoke tests;
// the browser page uses the full keymap.js instead.
const US = {};
for (let i = 0; i < 26; i++) US[String.fromCharCode(97 + i)] = [4 + i, false];
for (let i = 0; i < 26; i++) US[String.fromCharCode(65 + i)] = [4 + i, true];
'1234567890'.split('').forEach((ch, i) => { US[ch] = [30 + i, false]; });
US[' '] = [44, false];
US['\n'] = [40, false];
US['.'] = [55, false];
US['/'] = [56, false];
US['-'] = [45, false];
US['_'] = [45, true];
US['='] = [46, false];
US['+'] = [46, true];
US[','] = [54, false];
US[':'] = [51, true];
US[';'] = [51, false];
US['('] = [38, true];
US[')'] = [39, true];
US['"'] = [52, true];
US["'"] = [52, false];

const Module = require(path.resolve(__dirname, '../build/core.js'));

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function push(type, a, b, c, d) {
  const ring = Module._fmrb_wasm_input_ring();
  const events = Module._fmrb_wasm_input_ring_events();
  const wrPtr = Module._fmrb_wasm_input_wr_ptr();
  const heap = Module.HEAPU32;
  const wr = Atomics.load(heap, wrPtr >> 2);
  const rec = (ring >> 2) + (wr % events) * 5;
  heap[rec] = type;
  heap[rec + 1] = a >>> 0;
  heap[rec + 2] = b >>> 0;
  heap[rec + 3] = c >>> 0;
  heap[rec + 4] = d >>> 0;
  Atomics.add(heap, wrPtr >> 2, 1);
}

function shot(file) {
  const w = Module._fmrb_wasm_frame_width();
  const h = Module._fmrb_wasm_frame_height();
  const seq = Module._fmrb_wasm_frame_seq();
  const ptr = Module._fmrb_wasm_frame_rgba();
  if (!w || !h || !ptr) throw new Error(`no frame yet (seq=${seq})`);
  const rgba = Buffer.from(new Uint8Array(Module.HEAPU8.buffer, ptr, w * h * 4));
  writePng(w, h, rgba, file);
  console.log(`drive: shot ${file} (${w}x${h} seq=${seq})`);
}

function ringView() {
  const ptr = Module._fmrb_wasm_audio_ring();
  const n = Module._fmrb_wasm_audio_ring_size();
  return { view: new Int16Array(Module.HEAPU8.buffer, ptr, n), n };
}

async function record(ms, file) {
  const { view, n } = ringView();
  const rate = Module._fmrb_wasm_audio_rate();
  // Start from the oldest sample the ring still holds, so a sound that
  // played just before (the boot beep) is part of the recording.
  const wr0 = Module._fmrb_wasm_audio_wr();
  let rd = wr0 > n ? wr0 - n : 0;
  const start = rd;
  const out = [];
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) {
    await sleep(16);
    const wr = Module._fmrb_wasm_audio_wr();
    if (wr - rd > n) rd = wr - n;   // dropped some; keep going
    for (; rd !== wr; rd++) out.push(view[rd % n]);
  }
  const samples = Int16Array.from(out);
  if (file) {
    const hdr = Buffer.alloc(44);
    hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + samples.length * 2, 4);
    hdr.write('WAVEfmt ', 8); hdr.writeUInt32LE(16, 16);
    hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(1, 22);
    hdr.writeUInt32LE(rate, 24); hdr.writeUInt32LE(rate * 2, 28);
    hdr.writeUInt16LE(2, 32); hdr.writeUInt16LE(16, 34);
    hdr.write('data', 36); hdr.writeUInt32LE(samples.length * 2, 40);
    fs.writeFileSync(file, Buffer.concat([hdr, Buffer.from(samples.buffer)]));
  }
  // per-100ms window: RMS + dominant frequency by mean-crossing count
  const win = Math.floor(rate / 10);
  for (let o = 0; o + win <= samples.length; o += win) {
    let sum = 0, sq = 0;
    for (let i = 0; i < win; i++) { sum += samples[o + i]; }
    const mean = sum / win;
    let crossings = 0, prev = samples[o] - mean;
    for (let i = 1; i < win; i++) {
      const v = samples[o + i] - mean;
      sq += v * v;
      if ((v > 0) !== (prev > 0)) crossings++;
      prev = v;
    }
    const rms = Math.sqrt(sq / win);
    const freq = crossings * rate / (2 * win);
    if (rms > 100) {
      console.log(`drive: audio ${Math.round(o / rate * 1000)}ms rms=${rms.toFixed(0)} freq~${freq.toFixed(0)}Hz`);
    }
  }
  console.log(`drive: rec done (${samples.length} samples from wr=${start})`);
}

async function main() {
  const cmds = process.argv.slice(2);
  if (cmds.length === 0) {
    console.error('drive: no commands (see the file header)');
    process.exit(2);
  }
  for (const cmd of cmds) {
    const [op, rest] = [cmd.slice(0, cmd.indexOf(':')), cmd.slice(cmd.indexOf(':') + 1)];
    if (op === 'wait') {
      await sleep(parseInt(rest, 10));
    } else if (op === 'move') {
      const [x, y] = rest.split(',').map(Number);
      push(4, x, y, 0, 0);
    } else if (op === 'click') {
      const [x, y, b] = rest.split(',').map(Number);
      push(4, x, y, 0, 0);
      await sleep(60);
      push(3, x, y, b || 1, 1);
      await sleep(60);
      push(3, x, y, b || 1, 0);
    } else if (op === 'key') {
      const [sc, mod] = rest.split(',').map(Number);
      push(1, sc, mod || 0, 0, 0);
      await sleep(40);
      push(2, sc, mod || 0, 0, 0);
    } else if (op === 'text') {
      for (const ch of rest) {
        const ent = US[ch];
        if (!ent) {
          // Say so: a silently dropped character turns into a path that does
          // not exist, and the failure looks like the app's fault.
          console.error(`drive: text has no key for ${JSON.stringify(ch)}; skipped`);
          continue;
        }
        const mod = ent[1] ? 0x01 : 0; // FMRB_KEYMAP_MOD_LSHIFT
        push(1, ent[0], mod, 0, 0);
        await sleep(30);
        push(2, ent[0], mod, 0, 0);
        await sleep(30);
      }
    } else if (op === 'rec') {
      const [ms, file] = rest.split(',');
      await record(parseInt(ms, 10), file);
    } else if (op === 'shot') {
      shot(rest);
    } else {
      console.error(`drive: unknown command ${cmd}`);
    }
  }
  process.exit(0);
}

main().catch((e) => { console.error('drive failed:', e); process.exit(1); });
