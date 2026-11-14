// ============================================================
// BMP332 — 8-bit indexed BMP encoder/decoder for Family mruby
//
// File layout matches what fmrb_bmp332.c (device side) accepts:
//   14B BITMAPFILEHEADER + 40B BITMAPINFOHEADER + 256*4B palette + pixel data.
// Pixel index bytes are written directly to the framebuffer on device as
// RGB332 values, so the BMP palette is only consumed by desktop image viewers.
// We always write the canonical RGB332 palette for round-trip correctness.
// ============================================================

const RGB332_PIXEL_OFFSET = 14 + 40 + 256 * 4;   // 1078

// Pre-computed RGB332 -> 24bit RGB lookup for canvas rendering and palette swatches.
// index i (0..255): RRR GGG BB  -> R = (i>>5)&7, G = (i>>2)&7, B = i&3.
const RGB332_RGB = (() => {
  const tbl = new Uint8Array(256 * 3);
  for (let i = 0; i < 256; i++) {
    const r3 = (i >> 5) & 7;
    const g3 = (i >> 2) & 7;
    const b2 = i & 3;
    tbl[i * 3 + 0] = Math.round(r3 * 255 / 7);
    tbl[i * 3 + 1] = Math.round(g3 * 255 / 7);
    tbl[i * 3 + 2] = Math.round(b2 * 255 / 3);
  }
  return tbl;
})();

function rgb332ToCss(i) {
  const r = RGB332_RGB[i * 3 + 0];
  const g = RGB332_RGB[i * 3 + 1];
  const b = RGB332_RGB[i * 3 + 2];
  return 'rgb(' + r + ',' + g + ',' + b + ')';
}

// Find the nearest RGB332 index for an arbitrary 24-bit (R,G,B). Used when
// loading a non-RGB332 8-bit BMP and we need to remap to the device palette.
function rgbToRgb332Index(r, g, b) {
  const r3 = Math.round(r * 7 / 255) & 7;
  const g3 = Math.round(g * 7 / 255) & 7;
  const b2 = Math.round(b * 3 / 255) & 3;
  return (r3 << 5) | (g3 << 2) | b2;
}

// Build the canonical 1024-byte BMP palette (256 entries of BGRA).
function buildRgb332Palette() {
  const pal = new Uint8Array(256 * 4);
  for (let i = 0; i < 256; i++) {
    pal[i * 4 + 0] = RGB332_RGB[i * 3 + 2]; // B
    pal[i * 4 + 1] = RGB332_RGB[i * 3 + 1]; // G
    pal[i * 4 + 2] = RGB332_RGB[i * 3 + 0]; // R
    pal[i * 4 + 3] = 0;                     // A (reserved)
  }
  return pal;
}

function readU32LE(buf, off) {
  return buf[off] | (buf[off + 1] << 8) | (buf[off + 2] << 16) | (buf[off + 3] << 24);
}
function readI32LE(buf, off) {
  const u = readU32LE(buf, off);
  return u | 0;  // force signed
}
function readU16LE(buf, off) { return buf[off] | (buf[off + 1] << 8); }

function writeU32LE(buf, off, v) {
  buf[off]     =  v        & 0xff;
  buf[off + 1] = (v >>> 8)  & 0xff;
  buf[off + 2] = (v >>> 16) & 0xff;
  buf[off + 3] = (v >>> 24) & 0xff;
}
function writeI32LE(buf, off, v) { writeU32LE(buf, off, v >>> 0); }
function writeU16LE(buf, off, v) {
  buf[off]     =  v       & 0xff;
  buf[off + 1] = (v >>> 8) & 0xff;
}

// Decode an 8-bit indexed BMP (1..256 wide/tall) into top-down RGB332 indices.
// Throws on malformed input. Returns { width, height, pixels: Uint8Array }.
//
// If the BMP carries a non-RGB332 palette (e.g. a paint-program export), we
// remap each index through the file palette to its nearest RGB332 cell so the
// edited result still displays correctly on device.
function decodeBmp332(arrayBuffer) {
  const buf = new Uint8Array(arrayBuffer);
  if (buf.length < 54) throw new Error('BMP too small');
  if (buf[0] !== 0x42 || buf[1] !== 0x4d) throw new Error('Not a BMP file');

  const pixelOffset = readU32LE(buf, 10);
  const dibSize     = readU32LE(buf, 14);
  if (dibSize < 40) throw new Error('Unsupported DIB header size: ' + dibSize);

  let width  = readI32LE(buf, 18);
  let height = readI32LE(buf, 22);
  const bpp         = readU16LE(buf, 28);
  const compression = readU32LE(buf, 30);

  if (bpp !== 8) throw new Error('Only 8-bit indexed BMP supported (got ' + bpp + 'bpp)');
  if (compression !== 0) throw new Error('Compressed BMP not supported');
  if (width <= 0 || width > 256) throw new Error('Width out of range: ' + width);

  let bottomUp = true;
  if (height < 0) { height = -height; bottomUp = false; }
  if (height <= 0 || height > 256) throw new Error('Height out of range: ' + height);

  const rowSize = (width + 3) & ~3;
  if (pixelOffset + rowSize * height > buf.length) throw new Error('Pixel data truncated');

  // Read palette to detect whether it is the RGB332 canonical layout. The
  // palette starts at 14 + dibSize.
  const palOff = 14 + dibSize;
  const paletteLen = Math.min(256, Math.floor((pixelOffset - palOff) / 4));
  let needsRemap = false;
  const indexMap = new Uint8Array(256);
  for (let i = 0; i < 256; i++) indexMap[i] = i;
  for (let i = 0; i < paletteLen; i++) {
    const b = buf[palOff + i * 4 + 0];
    const g = buf[palOff + i * 4 + 1];
    const r = buf[palOff + i * 4 + 2];
    const expectedR = RGB332_RGB[i * 3 + 0];
    const expectedG = RGB332_RGB[i * 3 + 1];
    const expectedB = RGB332_RGB[i * 3 + 2];
    if (r !== expectedR || g !== expectedG || b !== expectedB) {
      needsRemap = true;
      indexMap[i] = rgbToRgb332Index(r, g, b);
    }
  }

  // Extract pixels top-down, strip row padding, optionally remap.
  const pixels = new Uint8Array(width * height);
  for (let y = 0; y < height; y++) {
    const srcRow = bottomUp ? (height - 1 - y) : y;
    const srcStart = pixelOffset + srcRow * rowSize;
    if (needsRemap) {
      for (let x = 0; x < width; x++) {
        pixels[y * width + x] = indexMap[buf[srcStart + x]];
      }
    } else {
      for (let x = 0; x < width; x++) {
        pixels[y * width + x] = buf[srcStart + x];
      }
    }
  }
  return { width, height, pixels, remapped: needsRemap };
}

// Encode top-down RGB332 pixels into an ArrayBuffer (.bmp file content).
// Always emits the canonical RGB332 palette. Output is bottom-up to match the
// convention preferred by desktop viewers; the device parser accepts both.
function encodeBmp332(width, height, pixels) {
  if (width <= 0 || width > 256)  throw new Error('Width out of range: ' + width);
  if (height <= 0 || height > 256) throw new Error('Height out of range: ' + height);
  if (pixels.length < width * height) throw new Error('Pixel buffer too short');

  const rowSize = (width + 3) & ~3;
  const pixelBytes = rowSize * height;
  const fileSize = RGB332_PIXEL_OFFSET + pixelBytes;
  const out = new Uint8Array(fileSize);

  // BITMAPFILEHEADER (14 bytes)
  out[0] = 0x42; out[1] = 0x4d;            // 'BM'
  writeU32LE(out,  2, fileSize);
  writeU16LE(out,  6, 0);                  // reserved1
  writeU16LE(out,  8, 0);                  // reserved2
  writeU32LE(out, 10, RGB332_PIXEL_OFFSET);

  // BITMAPINFOHEADER (40 bytes)
  writeU32LE(out, 14, 40);                 // biSize
  writeI32LE(out, 18, width);
  writeI32LE(out, 22, height);             // positive = bottom-up
  writeU16LE(out, 26, 1);                  // biPlanes
  writeU16LE(out, 28, 8);                  // biBitCount
  writeU32LE(out, 30, 0);                  // biCompression = BI_RGB
  writeU32LE(out, 34, pixelBytes);         // biSizeImage
  writeU32LE(out, 38, 2835);               // biXPelsPerMeter (= 72 dpi)
  writeU32LE(out, 42, 2835);               // biYPelsPerMeter
  writeU32LE(out, 46, 256);                // biClrUsed
  writeU32LE(out, 50, 0);                  // biClrImportant

  // Palette
  out.set(buildRgb332Palette(), 54);

  // Pixels (bottom-up, padded rows)
  for (let y = 0; y < height; y++) {
    const dstRow = height - 1 - y;
    const dstStart = RGB332_PIXEL_OFFSET + dstRow * rowSize;
    for (let x = 0; x < width; x++) {
      out[dstStart + x] = pixels[y * width + x];
    }
    // padding bytes are already 0 from Uint8Array init
  }
  return out.buffer;
}
