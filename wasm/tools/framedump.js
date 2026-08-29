// framedump.js - boot the wasm core under node, wait, and dump the RGBA frame
// the display backend keeps into a PNG. Development aid for the headless
// verification loop (doc/wasm/ P4a): "what would the browser show right now?"
//
//   node wasm/tools/framedump.js [seconds] [out.png]
//
// Run from the repo root (NODERAWFS resolves flash/ against the cwd).
'use strict';

const path = require('path');
const zlib = require('zlib');
const fs = require('fs');

const seconds = parseInt(process.argv[2] || '20', 10);
const outPath = process.argv[3] || 'wasm/build/frame.png';

function crc32(buf, seed) {
  let c, crc = seed ^ 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) {
    c = (crc ^ buf[i]) & 0xFF;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
// crc32 above processes one byte at a time with the table folded in; fine for
// one small image.
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
    raw[y * (w * 4 + 1)] = 0; // filter: none
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 6;  // color type RGBA
  const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  fs.writeFileSync(file, png);
}

const Module = require(path.resolve(__dirname, '../build/core.js'));

setTimeout(() => {
  try {
    const w = Module._fmrb_wasm_frame_width();
    const h = Module._fmrb_wasm_frame_height();
    const seq = Module._fmrb_wasm_frame_seq();
    const ptr = Module._fmrb_wasm_frame_rgba();
    if (!w || !h || !ptr) {
      console.error(`framedump: no frame yet (w=${w} h=${h} seq=${seq})`);
      process.exit(2);
    }
    // wasm memory is a SharedArrayBuffer under pthreads; copy before encoding.
    const rgba = Buffer.from(new Uint8Array(Module.HEAPU8.buffer, ptr, w * h * 4));
    writePng(w, h, rgba, outPath);
    console.log(`framedump: ${outPath} ${w}x${h} seq=${seq}`);
    process.exit(0);
  } catch (e) {
    console.error('framedump failed:', e);
    process.exit(1);
  }
}, seconds * 1000);
